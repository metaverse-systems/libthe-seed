// Tests of MachOParser against genuine Mac programs.
//
// The samples in tests/fixtures are written by clang, ld64.lld and llvm-lipo.
// Every expected value below is the one those LLVM tools print for the same
// file, not a value taken from the library:
//   llvm-lipo -archs FILE                       architectures and their order
//   llvm-objdump --macho --universal-headers F  slice cputype, cpusubtype, offset, size, align
//   llvm-otool -h FILE / -l FILE                cputype, cpusubtype, LC_CODE_SIGNATURE dataoff/datasize,
//                                               LC_LOAD_DYLIB names
//   llvm-otool -L FILE                          the library list (LC_ID_DYLIB is the library's own name
//                                               and is not a dependency)
// The signature offset of a slice is relative to the start of its slice.
//
// Byte order. A thin program is little-endian from its first byte (CF FA ED FE);
// a universal file is big-endian in its table (CA FE BA BE or CA FE BA BF) and
// holds little-endian slices. The synthetic images below follow the same
// layout.

#include "TestPaths.hpp"
#include "DepFixtures.hpp"
#include "MachOReference.hpp"

#include <libthe-seed/MachOParser.hpp>

#include "internal/BoundedBytes.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace dep = seedtest::dep;
using Names = std::vector<std::string>;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kCpuX86_64 = 0x01000007;
constexpr std::uint32_t kCpuArm64 = 0x0100000C;
constexpr std::uint32_t kSubX86_64 = 0x80000003; // CPU_SUBTYPE_X86_64_ALL with CPU_SUBTYPE_LIB64
constexpr std::uint32_t kSubArm64 = 0;           // CPU_SUBTYPE_ARM64_ALL

constexpr const char *kLibSystem = "/usr/lib/libSystem.B.dylib";

struct SliceFact
{
    std::uint32_t cpu_type;
    std::uint32_t cpu_subtype;
    std::uint64_t offset;
    std::uint64_t size;
    bool is_signed;
    std::uint64_t signature_offset; // relative to the slice
    std::uint64_t signature_size;
};

struct SampleFact
{
    const char *name;
    MachOParser::Format format;
    std::vector<SliceFact> slices;
    Names dependencies;
};

const std::vector<SampleFact> &Samples()
{
    using F = MachOParser::Format;
    static const std::vector<SampleFact> samples = {
        {"tiny-macho-x86_64", F::MachO64, {{kCpuX86_64, kSubX86_64, 0, 4248, false, 0, 0}}, {kLibSystem}},
        {"tiny-macho-arm64", F::MachO64, {{kCpuArm64, kSubArm64, 0, 16536, false, 0, 0}}, {kLibSystem}},
        {"tiny-macho-universal",
         F::Fat,
         {{kCpuX86_64, kSubX86_64, 4096, 4248, false, 0, 0}, {kCpuArm64, kSubArm64, 16384, 16536, false, 0, 0}},
         {kLibSystem}},
        {"tiny-macho-universal64",
         F::Fat,
         {{kCpuX86_64, kSubX86_64, 4096, 4248, false, 0, 0}, {kCpuArm64, kSubArm64, 16384, 16536, false, 0, 0}},
         {kLibSystem}},
        {"tiny-macho-arm64-adhoc", F::MachO64, {{kCpuArm64, kSubArm64, 0, 16848, true, 16544, 304}}, {kLibSystem}},
        {"tiny-macho-x86_64-adhoc", F::MachO64, {{kCpuX86_64, kSubX86_64, 0, 4464, true, 4256, 208}}, {kLibSystem}},
        {"tiny-macho-universal-adhoc",
         F::Fat,
         {{kCpuX86_64, kSubX86_64, 4096, 4464, true, 4256, 208}, {kCpuArm64, kSubArm64, 16384, 16848, true, 16544, 304}},
         {kLibSystem}},
        {"tiny-macho-x86_64-nospace", F::MachO64, {{kCpuX86_64, kSubX86_64, 0, 4248, false, 0, 0}}, {kLibSystem}},
        {"tiny-macho-x86_64-exactfit", F::MachO64, {{kCpuX86_64, kSubX86_64, 0, 4248, false, 0, 0}}, {kLibSystem}},
        // A library: its LC_ID_DYLIB (its own name) is not a dependency.
        {"tiny-macho-dylib-arm64", F::MachO64, {{kCpuArm64, kSubArm64, 0, 16472, false, 0, 0}}, {kLibSystem}},
        // Synthetic (the adhoc x86-64 program with 16 bytes appended): reading it is legal.
        {"tiny-macho-x86_64-data-after-sig", F::MachO64, {{kCpuX86_64, kSubX86_64, 0, 4480, true, 4256, 208}}, {kLibSystem}},
    };
    return samples;
}

Bytes LoadBytes(const std::string &name)
{
    std::ifstream in(seedtest::FixturePath(name), std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string WriteImage(const seedtest::ScratchDir &scratch, const Bytes &bytes)
{
    return dep::WriteFile(scratch.Path(), "input.bin", bytes).string();
}

// The checks on the signature facts of a slice. The fields are additive in the
// public type; where a field is absent the check fails with a message that
// names it instead of the test failing to compile.
template <typename Slice>
void CheckSignatureFacts(const Slice &slice, const SliceFact &fact)
{
    if constexpr(requires { slice.is_signed; slice.signature_offset; slice.signature_size; })
    {
        CHECK(slice.is_signed == fact.is_signed);
        if(fact.is_signed)
        {
            CHECK(slice.signature_offset == fact.signature_offset);
            CHECK(slice.signature_size == fact.signature_size);
        }
    }
    else
    {
        FAIL_CHECK("ArchSlice has no is_signed, signature_offset and signature_size fields");
    }
}

template <typename Slice>
void CheckSupported(const Slice &slice)
{
    if constexpr(requires { slice.supported; slice.unsupported_reason; })
    {
        CHECK(slice.supported);
        CHECK(slice.unsupported_reason.empty());
    }
    else
    {
        FAIL_CHECK("ArchSlice has no supported and unsupported_reason fields");
    }
}

// Requires a std::runtime_error whose message starts with "Mach-O: " and
// contains the text.
template <typename F>
void RequireRejected(F &&callable, const std::string &text)
{
    try
    {
        callable();
    }
    catch(const std::runtime_error &error)
    {
        const std::string message = error.what();
        INFO("message: " << message);
        CHECK(message.rfind("Mach-O: ", 0) == 0);
        CHECK(message.find(text) != std::string::npos);
        return;
    }
    FAIL("expected a rejection containing: " << text);
}

// Big-endian and little-endian reads composed byte by byte, so they cannot
// depend on the host.
std::uint64_t ComposeBE(const Bytes &bytes, std::uint64_t at, int width)
{
    std::uint64_t value = 0;
    for(int i = 0; i < width; ++i)
    {
        value = (value << 8) | bytes.at(at + i);
    }
    return value;
}

std::uint64_t ComposeLE(const Bytes &bytes, std::uint64_t at, int width)
{
    std::uint64_t value = 0;
    for(int i = width - 1; i >= 0; --i)
    {
        value = (value << 8) | bytes.at(at + i);
    }
    return value;
}

}

TEST_CASE("MachOParser::DetectFormat identifies x86_64 Mach-O", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::MachO64);
}

TEST_CASE("MachOParser::DetectFormat identifies arm64 Mach-O", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-arm64");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::MachO64);
}

TEST_CASE("MachOParser::DetectFormat identifies fat Mach-O", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-universal");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::Fat);
}

TEST_CASE("MachOParser::DetectFormat returns NotMachO for PE", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::NotMachO);
}

TEST_CASE("MachOParser::DetectFormat returns NotMachO for plain text", "[MachOParser]")
{
    auto path = seedtest::FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::NotMachO);
}

TEST_CASE("MachOParser::DetectFormat classifies every genuine sample", "[MachOParser][samples]")
{
    for(const SampleFact &sample : Samples())
    {
        DYNAMIC_SECTION(sample.name)
        {
            const std::string path = seedtest::FixturePath(sample.name);
            CHECK(MachOParser::DetectFormat(path) == sample.format);
            CHECK(MachOParser::IsMachO(path));
            CHECK(MachOParser::IsFatBinary(path) == (sample.format == MachOParser::Format::Fat));
        }
    }
}

TEST_CASE("MachOParser::IsMachO returns true for Mach-O files", "[MachOParser]")
{
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny-macho-x86_64")) == true);
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny-macho-arm64")) == true);
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny-macho-universal")) == true);
}

TEST_CASE("MachOParser::IsMachO returns false for non-Mach-O files", "[MachOParser]")
{
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny.exe")) == false);
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("plain.txt")) == false);
}

TEST_CASE("MachOParser::IsFatBinary returns true only for fat binaries", "[MachOParser]")
{
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-universal")) == true);
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-universal64")) == true);
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-x86_64")) == false);
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-arm64")) == false);
}

// The name of this case is recorded in known-gaps.txt (it failed with
// "Truncated fat_arch entry" before the container table was read big-endian).
TEST_CASE("MachOParser::GetArchSlices returns slices for fat binary", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-universal");
    REQUIRE(std::filesystem::exists(path));

    auto slices = MachOParser::GetArchSlices(path);
    REQUIRE(slices.size() == 2);

    // llvm-lipo -archs: x86_64 arm64, in that order.
    CHECK(slices[0].cpu_type == kCpuX86_64);
    CHECK(slices[0].cpu_subtype == kSubX86_64);
    CHECK(slices[0].offset == 4096);
    CHECK(slices[0].size == 4248);

    CHECK(slices[1].cpu_type == kCpuArm64);
    CHECK(slices[1].cpu_subtype == kSubArm64);
    CHECK(slices[1].offset == 16384);
    CHECK(slices[1].size == 16536);
}

TEST_CASE("MachOParser::GetArchSlices returns single slice for non-fat", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(path));

    auto slices = MachOParser::GetArchSlices(path);
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].cpu_type == kCpuX86_64);
    CHECK(slices[0].cpu_subtype == kSubX86_64);
    CHECK(slices[0].offset == 0);
    CHECK(slices[0].size == 4248);
}

TEST_CASE("MachOParser::GetArchSlices reports architectures and signature facts of every sample",
          "[MachOParser][samples]")
{
    for(const SampleFact &sample : Samples())
    {
        DYNAMIC_SECTION(sample.name)
        {
            const auto slices = MachOParser::GetArchSlices(seedtest::FixturePath(sample.name));
            REQUIRE(slices.size() == sample.slices.size());
            for(std::size_t i = 0; i < slices.size(); ++i)
            {
                INFO("slice " << i);
                const SliceFact &fact = sample.slices[i];
                CHECK(slices[i].cpu_type == fact.cpu_type);
                CHECK(slices[i].cpu_subtype == fact.cpu_subtype);
                CHECK(slices[i].offset == fact.offset);
                CHECK(slices[i].size == fact.size);
                CheckSignatureFacts(slices[i], fact);
                CheckSupported(slices[i]);
            }
        }
    }
}

TEST_CASE("MachOParser::GetArchSlices reads the 64-bit offset container", "[MachOParser][samples]")
{
    // llvm-objdump --macho --universal-headers prints fat_magic FAT_MAGIC_64
    // and the same two slices as the 32-bit container.
    const Bytes bytes = LoadBytes("tiny-macho-universal64");
    REQUIRE(bytes.size() >= 4);
    CHECK(ComposeBE(bytes, 0, 4) == 0xCAFEBABF);

    const auto slices = MachOParser::GetArchSlices(seedtest::FixturePath("tiny-macho-universal64"));
    REQUIRE(slices.size() == 2);
    CHECK(slices[0].cpu_type == kCpuX86_64);
    CHECK(slices[0].offset == 4096);
    CHECK(slices[0].size == 4248);
    CHECK(slices[1].cpu_type == kCpuArm64);
    CHECK(slices[1].offset == 16384);
    CHECK(slices[1].size == 16536);
}

TEST_CASE("MachOParser::ListDependencies lists the references of every genuine sample",
          "[MachOParser][samples]")
{
    for(const SampleFact &sample : Samples())
    {
        DYNAMIC_SECTION(sample.name)
        {
            CHECK(MachOParser::ListDependencies(seedtest::FixturePath(sample.name)) == sample.dependencies);
        }
    }
}

TEST_CASE("MachOParser reads a library without listing its own name", "[MachOParser][samples]")
{
    // llvm-otool -l shows LC_ID_DYLIB (the library's own install name) and one LC_LOAD_DYLIB.
    const Names names = MachOParser::ListDependencies(seedtest::FixturePath("tiny-macho-dylib-arm64"));
    REQUIRE(names.size() == 1);
    CHECK(names[0] == kLibSystem);
}

TEST_CASE("MachOParser reads the same values from explicit byte orders on any host", "[MachOParser][order]")
{
    // The reads below name their byte order; none uses the byte order of this
    // machine, so a big-endian host reads the same values. The parser's answers
    // are compared with fields composed from the raw bytes.
    namespace si = seed::internal;
    using si::ByteOrder;

    const Bytes universal = LoadBytes("tiny-macho-universal");
    const si::ByteSpan span(universal, "Mach-O");

    CHECK(span.Read<std::uint32_t>(0, ByteOrder::Big, "magic") == 0xCAFEBABE);
    CHECK(span.Read<std::uint32_t>(0, ByteOrder::Little, "magic") == 0xBEBAFECA);
    CHECK(span.Read<std::uint32_t>(4, ByteOrder::Big, "slice count") == 2);
    CHECK(span.Read<std::uint32_t>(8, ByteOrder::Big, "cputype") == kCpuX86_64);
    CHECK(span.Read<std::uint32_t>(16, ByteOrder::Big, "offset") == 4096);
    CHECK(span.Read<std::uint32_t>(20, ByteOrder::Big, "size") == 4248);
    CHECK(span.Read<std::uint32_t>(24, ByteOrder::Big, "align") == 12);

    // The slice is little-endian: FEEDFACF read from CF FA ED FE.
    CHECK(span.Read<std::uint32_t>(4096, ByteOrder::Little, "slice magic") == 0xFEEDFACF);
    CHECK(span.Read<std::uint32_t>(4096, ByteOrder::Big, "slice magic") == 0xCFFAEDFE);
    CHECK(span.Read<std::uint32_t>(4096 + 4, ByteOrder::Little, "cputype") == kCpuX86_64);
    CHECK(span.Read<std::uint32_t>(4096 + 16, ByteOrder::Little, "ncmds") == 13);
    CHECK(span.Read<std::uint32_t>(4096 + 20, ByteOrder::Little, "sizeofcmds") == 648);

    const Bytes wide = LoadBytes("tiny-macho-universal64");
    const si::ByteSpan wide_span(wide, "Mach-O");
    CHECK(wide_span.Read<std::uint32_t>(0, ByteOrder::Big, "magic") == 0xCAFEBABF);
    CHECK(wide_span.Read<std::uint64_t>(16, ByteOrder::Big, "offset") == 4096);
    CHECK(wide_span.Read<std::uint64_t>(24, ByteOrder::Big, "size") == 4248);
    CHECK(wide_span.Read<std::uint32_t>(32, ByteOrder::Big, "align") == 12);

    // The parser agrees with the bytes composed by hand, for both containers.
    for(const char *name : {"tiny-macho-universal", "tiny-macho-universal64"})
    {
        DYNAMIC_SECTION(name)
        {
            const Bytes bytes = LoadBytes(name);
            const bool is_wide = ComposeBE(bytes, 0, 4) == 0xCAFEBABF;
            const std::uint64_t entry = is_wide ? 32 : 20;
            const int field = is_wide ? 8 : 4;
            const auto slices = MachOParser::GetArchSlices(seedtest::FixturePath(name));
            REQUIRE(slices.size() == ComposeBE(bytes, 4, 4));
            for(std::size_t i = 0; i < slices.size(); ++i)
            {
                const std::uint64_t at = 8 + i * entry;
                CHECK(slices[i].cpu_type == ComposeBE(bytes, at, 4));
                CHECK(slices[i].cpu_subtype == ComposeBE(bytes, at + 4, 4));
                CHECK(slices[i].offset == ComposeBE(bytes, at + 8, field));
                CHECK(slices[i].size == ComposeBE(bytes, at + 8 + field, field));
                // The slice itself: little-endian cputype after CF FA ED FE.
                CHECK(ComposeLE(bytes, slices[i].offset, 4) == 0xFEEDFACF);
                CHECK(ComposeLE(bytes, slices[i].offset + 4, 4) == slices[i].cpu_type);
            }
        }
    }
}

TEST_CASE("MachOParser rejects a Java class header as not a Mach-O file", "[MachOParser][safety]")
{
    // CA FE BA BE is also the first word of a Java class file; the bytes after it
    // are a minor and major version (0 and 52), not a slice count and a table.
    seedtest::ScratchDir scratch("macho-java");
    const Bytes bytes = machoref::JavaClassHeader();
    const std::string path = WriteImage(scratch, bytes);
    RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, "not a Mach-O");
    RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, "not a Mach-O");
}

TEST_CASE("MachOParser rejects a file shorter than its header", "[MachOParser][safety]")
{
    seedtest::ScratchDir scratch("macho-short");
    const Bytes thin = LoadBytes("tiny-macho-x86_64");
    for(std::size_t length = 4; length < 32; ++length)
    {
        DYNAMIC_SECTION("program cut to " << length << " bytes")
        {
            const Bytes cut(thin.begin(), thin.begin() + static_cast<std::ptrdiff_t>(length));
            const std::string path = WriteImage(scratch, cut);
            RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, "header");
            RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, "header");
        }
    }
    const Bytes fat = LoadBytes("tiny-macho-universal");
    for(std::size_t length = 4; length < 8; ++length)
    {
        DYNAMIC_SECTION("universal file cut to " << length << " bytes")
        {
            const Bytes cut(fat.begin(), fat.begin() + static_cast<std::ptrdiff_t>(length));
            const std::string path = WriteImage(scratch, cut);
            RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, "Truncated fat header");
            RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, "Truncated fat header");
        }
    }
}

TEST_CASE("MachOParser rejects truncated slice tables of both container forms", "[MachOParser][safety]")
{
    seedtest::ScratchDir scratch("macho-table");
    struct Case
    {
        const char *sample;
        std::size_t length; // inside the table that needs 48 or 72 bytes
    };
    for(const Case &c : {Case{"tiny-macho-universal", 47}, Case{"tiny-macho-universal", 8},
                         Case{"tiny-macho-universal64", 71}, Case{"tiny-macho-universal64", 40}})
    {
        DYNAMIC_SECTION(c.sample << " cut to " << c.length << " bytes")
        {
            const Bytes whole = LoadBytes(c.sample);
            const Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(c.length));
            const std::string path = WriteImage(scratch, cut);
            RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, "Truncated fat_arch entry table");
            RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, "Truncated fat_arch entry table");
        }
    }
}

TEST_CASE("MachOParser rejects overlapping slices read from the table as written", "[MachOParser][safety]")
{
    // The second slice of the genuine universal file is moved to the start of the first.
    seedtest::ScratchDir scratch("macho-overlap");
    Bytes bytes = LoadBytes("tiny-macho-universal");
    const std::uint64_t second_offset_field = 8 + 20 + 8;
    for(int i = 0; i < 4; ++i)
    {
        bytes[second_offset_field + i] = bytes[8 + 8 + i];
    }
    const std::string path = WriteImage(scratch, bytes);
    RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, "overlap");
}

TEST_CASE("MachOParser rejects a program signed by an earlier version with a pointer to the fix",
          "[MachOParser][safety]")
{
    // The signature command of an ad-hoc signed program is overwritten with a
    // command of size zero, the dead entry that repeated signing once left behind.
    seedtest::ScratchDir scratch("macho-dead");
    Bytes bytes = LoadBytes("tiny-macho-x86_64-adhoc");
    const std::uint64_t at = 32 + 664 - 16; // LC_CODE_SIGNATURE is the 14th command and ends the table
    REQUIRE(ComposeLE(bytes, at, 4) == 0x1D);
    for(int i = 0; i < 16; ++i)
    {
        bytes[at + i] = 0;
    }
    const std::string path = WriteImage(scratch, bytes);
    const std::string advice = "this can be a program signed by an earlier version of the-seed; "
                               "rebuild it from the unsigned original";
    RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, "load command 13 size 0 is smaller than 8");
    RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, advice);
    RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, advice);
}

// Library references of every kind, on synthesised files in the byte order the
// platform writes: little-endian fields after the CF FA ED FE magic of a
// program, and a big-endian table in a universal file.
namespace {

dep::Bytes Referencing(const std::vector<dep::MachOReference> &references)
{
    return dep::MachOReferencing(references, dep::MachOFields::LittleAfterMagic);
}

dep::Bytes Universal(const std::vector<dep::Bytes> &slices)
{
    return dep::MachOUniversal(slices, true);
}

}

TEST_CASE("MachOParser::ListDependencies returns all five reference kinds in file order",
          "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::string path = WriteImage(scratch, Referencing({
        {dep::kLoadUpwardDylib, "/lib/upward"},
        {dep::kLoadDylib, "/lib/normal"},
        {dep::kLazyLoadDylib, "/lib/lazy"},
        {dep::kLoadWeakDylib, "/lib/weak"},
        {dep::kReexportDylib, "/lib/reexport"},
    }));

    CHECK(MachOParser::ListDependencies(path) ==
          Names{"/lib/upward", "/lib/normal", "/lib/lazy", "/lib/weak", "/lib/reexport"});
}

TEST_CASE("MachOParser::ListDependencies returns each single kind", "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::vector<std::uint32_t> kinds = {dep::kLoadDylib, dep::kLoadWeakDylib, dep::kReexportDylib,
                                              dep::kLazyLoadDylib, dep::kLoadUpwardDylib};
    for(const std::uint32_t kind : kinds)
    {
        DYNAMIC_SECTION("kind 0x" << std::hex << kind)
        {
            const std::string path = WriteImage(scratch, Referencing({{kind, "/lib/one"}}));
            CHECK(MachOParser::ListDependencies(path) == Names{"/lib/one"});
        }
    }
}

TEST_CASE("MachOParser::ListDependencies returns an empty list for a file with no references",
          "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::string path = WriteImage(scratch, Referencing({}));
    CHECK(MachOParser::ListDependencies(path).empty());
}

TEST_CASE("MachOParser::ListDependencies lists a name repeated across kinds once", "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::string path = WriteImage(scratch, Referencing({
        {dep::kLoadDylib, "/lib/shared"},
        {dep::kLoadWeakDylib, "/lib/other"},
        {dep::kReexportDylib, "/lib/shared"},
        {dep::kLoadDylib, "/lib/shared"},
        {dep::kLazyLoadDylib, "/lib/other"},
    }));

    CHECK(MachOParser::ListDependencies(path) == Names{"/lib/shared", "/lib/other"});
}

TEST_CASE("MachOParser::ListDependencies covers every slice of a fat file once each",
          "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const dep::Bytes first = Referencing({
        {dep::kLoadDylib, "/lib/common"},
        {dep::kLoadWeakDylib, "/lib/first"},
    });
    const dep::Bytes second = Referencing({
        {dep::kLoadDylib, "/lib/common"},
        {dep::kLazyLoadDylib, "/lib/second"},
        {dep::kLoadUpwardDylib, "/lib/third"},
    });

    SECTION("two slices")
    {
        const std::string path = WriteImage(scratch, Universal({first, second}));
        CHECK(MachOParser::ListDependencies(path) ==
              Names{"/lib/common", "/lib/first", "/lib/second", "/lib/third"});
    }

    SECTION("a slice with no references among others")
    {
        const std::string path =
            WriteImage(scratch, Universal({Referencing({}), second, first}));
        CHECK(MachOParser::ListDependencies(path) ==
              Names{"/lib/common", "/lib/second", "/lib/third", "/lib/first"});
    }
}
