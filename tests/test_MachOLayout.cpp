// The shared layout module for Mac programs (internal/MachOLayout.hpp), and the
// test-side checker it is compared with (MachOReference.hpp).
//
// The module is not used by the public classes yet; these tests read the
// genuine samples in tests/fixtures (made by clang and ld64.lld, see
// fixtures/PROVENANCE.md) and header-only synthetic images, and require that:
//   - the first four bytes decide the kind, whatever the host byte order;
//   - universal files are read in both table forms;
//   - the header space left after the load commands is measured to the first
//     content (0, 16 and 32 bytes in the nospace, exactfit and base samples);
//   - big-endian and 32-bit programs are reported as declined, from their
//     first bytes only;
//   - a damaged or truncated file is rejected with a message that starts with
//     "Mach-O: " and nothing is read outside the file.
// Test case names start with "layout:" for the module and "reference:" for the
// checker.

#include "TestPaths.hpp"

#include "MachOReference.hpp"
#include "internal/MachOLayout.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace si = seed::internal;
namespace mr = machoref;

namespace {

mr::Bytes Load(const std::string &name)
{
    std::ifstream in(seedtest::FixturePath(name), std::ios::binary);
    return mr::Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

si::MachOContainer Parse(const mr::Bytes &bytes)
{
    return si::ParseMachOContainer(si::ByteSpan(bytes, "Mach-O"));
}

si::MachOKind Classify(const mr::Bytes &bytes)
{
    return si::ClassifyMachOMagic(si::ByteSpan(bytes, "Mach-O"));
}

// Requires that parsing throws std::runtime_error whose message starts with
// "Mach-O: " and contains the text.
void RequireRejected(const mr::Bytes &bytes, const std::string &text)
{
    try
    {
        (void)Parse(bytes);
    }
    catch(const std::runtime_error &e)
    {
        const std::string message = e.what();
        INFO("message: " << message);
        CHECK(message.rfind("Mach-O: ", 0) == 0);
        CHECK(message.find(text) != std::string::npos);
        return;
    }
    FAIL("parsing was not rejected; expected a message containing: " << text);
}

void Put32LE(mr::Bytes &b, std::size_t at, std::uint32_t v)
{
    for(int i = 0; i < 4; ++i)
    {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void Put32BE(mr::Bytes &b, std::size_t at, std::uint32_t v)
{
    for(int i = 0; i < 4; ++i)
    {
        b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
    }
}

// A small 64-bit little-endian program: header, the given commands (each a
// complete byte string), then zero padding up to `total` bytes.
mr::Bytes Program(const std::vector<mr::Bytes> &commands, std::size_t total = 512,
                  std::uint32_t declared_sizeofcmds = 0xFFFFFFFF)
{
    mr::Bytes b(32, 0);
    b[0] = 0xCF; b[1] = 0xFA; b[2] = 0xED; b[3] = 0xFE;
    Put32LE(b, 4, 0x0100000C);
    Put32LE(b, 12, 2);
    std::uint32_t sum = 0;
    for(const mr::Bytes &c : commands)
    {
        b.insert(b.end(), c.begin(), c.end());
        sum += static_cast<std::uint32_t>(c.size());
    }
    Put32LE(b, 16, static_cast<std::uint32_t>(commands.size()));
    Put32LE(b, 20, declared_sizeofcmds == 0xFFFFFFFF ? sum : declared_sizeofcmds);
    b.resize(total, 0);
    return b;
}

mr::Bytes Command(std::uint32_t cmd, std::uint32_t size, std::uint32_t a = 0, std::uint32_t c = 0)
{
    mr::Bytes b(size < 16 ? 16 : size, 0);
    Put32LE(b, 0, cmd);
    Put32LE(b, 4, size);
    Put32LE(b, 8, a);
    Put32LE(b, 12, c);
    b.resize(size < 8 ? 8 : size);
    return b;
}

const char *const kSamples[] = {
    "tiny-macho-x86_64",          "tiny-macho-arm64",           "tiny-macho-universal",
    "tiny-macho-arm64-adhoc",     "tiny-macho-x86_64-adhoc",    "tiny-macho-universal-adhoc",
    "tiny-macho-x86_64-nospace",  "tiny-macho-x86_64-exactfit", "tiny-macho-universal64",
    "tiny-macho-dylib-arm64",     "tiny-macho-x86_64-data-after-sig"};

constexpr std::uint32_t kCpuX86_64 = 0x01000007;
constexpr std::uint32_t kCpuArm64 = 0x0100000C;

} // namespace

TEST_CASE("layout: the first four bytes decide the kind", "[MachOLayout]")
{
    CHECK(Classify(Load("tiny-macho-x86_64")) == si::MachOKind::Thin64Little);
    CHECK(Classify(Load("tiny-macho-arm64")) == si::MachOKind::Thin64Little);
    CHECK(Classify(Load("tiny-macho-dylib-arm64")) == si::MachOKind::Thin64Little);
    CHECK(Classify(Load("tiny-macho-universal")) == si::MachOKind::Fat32);
    CHECK(Classify(Load("tiny-macho-universal-adhoc")) == si::MachOKind::Fat32);
    CHECK(Classify(Load("tiny-macho-universal64")) == si::MachOKind::Fat64);
    CHECK(Classify(mr::BigEndian64()) == si::MachOKind::Thin64Big);
    CHECK(Classify(mr::BigEndian32()) == si::MachOKind::Thin32Big);
    CHECK(Classify(mr::Little32()) == si::MachOKind::Thin32Little);
    CHECK(Classify(mr::JavaClassHeader()) == si::MachOKind::Fat32);
    CHECK(Classify(Load("tiny.exe")) == si::MachOKind::Unknown);
    CHECK(Classify(Load("plain.txt")) == si::MachOKind::Unknown);
    CHECK(Classify(mr::Bytes{}) == si::MachOKind::Unknown);
    CHECK(Classify(mr::Bytes{0xCF, 0xFA, 0xED}) == si::MachOKind::Unknown);
}

TEST_CASE("layout: thin programs are read from the bytes as written", "[MachOLayout]")
{
    const si::MachOContainer x86 = Parse(Load("tiny-macho-x86_64"));
    REQUIRE(x86.form == si::ContainerForm::Thin);
    REQUIRE(x86.entries.size() == 1);
    CHECK(x86.entries[0].offset == 0);
    CHECK(x86.entries[0].size == Load("tiny-macho-x86_64").size());
    const si::SliceLayout &s = x86.entries[0].slice;
    CHECK(s.support == si::SliceSupport::Supported);
    CHECK(s.cputype == kCpuX86_64);
    CHECK(s.filetype == 2); // MH_EXECUTE
    CHECK(s.ncmds == 13);
    CHECK(s.sizeofcmds == 648);
    CHECK(s.header_end == 680);
    CHECK(s.commands.size() == 13);
    REQUIRE(s.text.has_value());
    REQUIRE(s.linkedit.has_value());
    CHECK(s.text->name == "__TEXT");
    CHECK(s.linkedit->name == "__LINKEDIT");
    CHECK(s.linkedit->fileoff + s.linkedit->filesize == s.size);
    CHECK_FALSE(s.codesig.has_value());
    REQUIRE(s.dylibs.size() == 1);
    CHECK(s.dylibs[0] == "/usr/lib/libSystem.B.dylib");

    const si::SliceLayout arm = Parse(Load("tiny-macho-arm64")).entries[0].slice;
    CHECK(arm.cputype == kCpuArm64);
    CHECK(arm.support == si::SliceSupport::Supported);

    const si::SliceLayout dylib = Parse(Load("tiny-macho-dylib-arm64")).entries[0].slice;
    CHECK(dylib.filetype == 6); // MH_DYLIB
    CHECK(dylib.cputype == kCpuArm64);
}

TEST_CASE("layout: universal containers are read in both table forms", "[MachOLayout]")
{
    const mr::Bytes thin_x86 = Load("tiny-macho-x86_64");
    const mr::Bytes thin_arm = Load("tiny-macho-arm64");
    for(const char *name : {"tiny-macho-universal", "tiny-macho-universal64", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        const si::MachOContainer c = Parse(Load(name));
        CHECK(c.form == (std::string(name) == "tiny-macho-universal64" ? si::ContainerForm::Fat64
                                                                        : si::ContainerForm::Fat32));
        REQUIRE(c.entries.size() == 2);
        CHECK(c.entries[0].cputype == kCpuX86_64);
        CHECK(c.entries[1].cputype == kCpuArm64);
        CHECK(c.entries[0].offset == 4096);
        CHECK(c.entries[1].offset == 16384);
        CHECK(c.entries[0].align == 12);
        CHECK(c.entries[1].align == 14);
        CHECK(c.entries[0].slice.base == c.entries[0].offset);
        CHECK(c.entries[0].slice.cputype == kCpuX86_64);
        CHECK(c.entries[1].slice.cputype == kCpuArm64);
        CHECK(c.entries[0].slice.support == si::SliceSupport::Supported);
    }
    const si::MachOContainer plain = Parse(Load("tiny-macho-universal"));
    CHECK(plain.entries[0].size == thin_x86.size());
    CHECK(plain.entries[1].size == thin_arm.size());
}

TEST_CASE("layout: the signature command of another tool's signature is found", "[MachOLayout]")
{
    for(const char *name : {"tiny-macho-x86_64-adhoc", "tiny-macho-arm64-adhoc"})
    {
        INFO(name);
        const mr::Bytes file = Load(name);
        const si::SliceLayout s = Parse(file).entries[0].slice;
        REQUIRE(s.codesig.has_value());
        const mr::FileReport report = mr::CheckFile(file);
        REQUIRE(report.slices.size() == 1);
        CHECK(s.codesig->dataoff == report.slices[0].dataoff);
        CHECK(s.codesig->datasize == report.slices[0].datasize);
        CHECK(s.codesig->dataoff + s.codesig->datasize == s.size);
        CHECK(s.linkedit->fileoff + s.linkedit->filesize == s.size);
    }
    const si::MachOContainer u = Parse(Load("tiny-macho-universal-adhoc"));
    for(const si::ContainerEntry &e : u.entries)
    {
        REQUIRE(e.slice.codesig.has_value());
        CHECK(e.slice.codesig->dataoff + e.slice.codesig->datasize == e.size);
    }
}

TEST_CASE("layout: header space is measured to the first content", "[MachOLayout]")
{
    CHECK(Parse(Load("tiny-macho-x86_64")).entries[0].slice.FreeHeaderSpace() == 32);
    CHECK(Parse(Load("tiny-macho-x86_64-nospace")).entries[0].slice.FreeHeaderSpace() == 0);
    CHECK(Parse(Load("tiny-macho-x86_64-exactfit")).entries[0].slice.FreeHeaderSpace() == 16);
    CHECK(Parse(Load("tiny-macho-x86_64-nospace")).entries[0].slice.first_content == 680);
    CHECK(Parse(Load("tiny-macho-x86_64-exactfit")).entries[0].slice.first_content == 696);
    // Same answers as the independent tool recorded in macho-reference.txt.
    CHECK(Parse(Load("tiny-macho-arm64")).entries[0].slice.FreeHeaderSpace() == 32);
    CHECK(Parse(Load("tiny-macho-x86_64-adhoc")).entries[0].slice.FreeHeaderSpace() == 32);
}

TEST_CASE("layout: big-endian and 32-bit programs are declined from their first bytes", "[MachOLayout]")
{
    struct Case
    {
        mr::Bytes bytes;
        si::SliceSupport support;
        const char *kind;
    };
    const Case cases[] = {
        {mr::BigEndian64(), si::SliceSupport::BigEndian, "big-endian"},
        {mr::BigEndian32(), si::SliceSupport::ThirtyTwoBitBigEndian, "32-bit big-endian"},
        {mr::Little32(), si::SliceSupport::ThirtyTwoBit, "32-bit"}};
    for(const Case &c : cases)
    {
        INFO(c.kind);
        const si::MachOContainer container = Parse(c.bytes);
        REQUIRE(container.entries.size() == 1);
        const si::SliceLayout &s = container.entries[0].slice;
        CHECK(s.support == c.support);
        CHECK(si::DeclineKind(s.support) == c.kind);
        CHECK(si::DeclineText(s.support).rfind(std::string(c.kind) + " Mac programs are not supported", 0) == 0);
        CHECK(s.commands.empty());
    }
    // Only twelve bytes are looked at.
    mr::Bytes twelve = mr::BigEndian64();
    twelve.resize(12);
    CHECK(Parse(twelve).entries[0].slice.support == si::SliceSupport::BigEndian);
    // The CPU fields are read in the order the magic names.
    CHECK(Parse(mr::BigEndian32()).entries[0].slice.cputype == 18);
    CHECK(Parse(mr::Little32()).entries[0].slice.cputype == 7);
    CHECK(si::DeclineKind(si::SliceSupport::Supported).empty());
}

TEST_CASE("layout: each slice of a universal file is judged separately", "[MachOLayout]")
{
    for(bool wide : {false, true})
    {
        INFO("wide: " << wide);
        const mr::Bytes arm = Load("tiny-macho-arm64");
        const mr::Bytes mixed = mr::Universal({arm, mr::BigEndian64(), mr::Little32()}, wide);
        const si::MachOContainer c = Parse(mixed);
        REQUIRE(c.entries.size() == 3);
        CHECK(c.form == (wide ? si::ContainerForm::Fat64 : si::ContainerForm::Fat32));
        CHECK(c.entries[0].slice.support == si::SliceSupport::Supported);
        CHECK(c.entries[1].slice.support == si::SliceSupport::BigEndian);
        CHECK(c.entries[2].slice.support == si::SliceSupport::ThirtyTwoBit);
        CHECK(c.entries[0].slice.ncmds == 13);
    }
}

TEST_CASE("layout: a Java class file is not a universal file", "[MachOLayout]")
{
    RequireRejected(mr::JavaClassHeader(), "not a Mach-O");
    mr::Bytes small{0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x34};
    small.resize(100, 0);
    RequireRejected(small, "Truncated fat_arch entry table");
    RequireRejected(mr::Bytes{0xCA, 0xFE, 0xBA, 0xBE}, "Truncated fat header");
    RequireRejected(Load("plain.txt"), "not a Mach-O");
    RequireRejected(mr::Bytes{}, "not a Mach-O");
    mr::Bytes zero_count(64, 0);
    zero_count[0] = 0xCA; zero_count[1] = 0xFE; zero_count[2] = 0xBA; zero_count[3] = 0xBE;
    RequireRejected(zero_count, "not a Mach-O");
}

TEST_CASE("layout: every truncation of a sample is rejected without reading outside", "[MachOLayout]")
{
    for(const char *name : kSamples)
    {
        const mr::Bytes whole = Load(name);
        if(std::string(name).find("data-after-sig") != std::string::npos)
        {
            continue; // its last 16 bytes are not covered by any structure
        }
        const std::size_t stride = whole.size() > 8192 ? 61 : 1;
        for(std::size_t length = 0; length < whole.size(); length += stride)
        {
            INFO(name << " cut to " << length);
            const mr::Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
            bool rejected = false;
            try
            {
                (void)Parse(cut);
            }
            catch(const std::runtime_error &e)
            {
                rejected = true;
                CHECK(std::string(e.what()).rfind("Mach-O: ", 0) == 0);
            }
            CHECK(rejected);
        }
        // Cutting at the very end of every slice keeps the file readable only
        // when nothing is lost.
        CHECK_NOTHROW(Parse(whole));
    }
}

TEST_CASE("layout: universal tables that cannot be right are rejected", "[MachOLayout]")
{
    const mr::Bytes good = Load("tiny-macho-universal");
    {
        mr::Bytes bad = good;
        Put32BE(bad, 8 + 20 + 8, 4096 * 100); // slice 1 offset past the end
        RequireRejected(bad, "fat slice 1");
    }
    {
        mr::Bytes bad = good;
        Put32BE(bad, 8 + 20 + 8, 4096); // slice 1 starts where slice 0 starts
        RequireRejected(bad, "overlap");
    }
    {
        mr::Bytes bad = good;
        Put32BE(bad, 8 + 20 + 8, 16); // slice 1 starts inside the table
        RequireRejected(bad, "inside the fat header table");
    }
    {
        mr::Bytes bad = good;
        Put32BE(bad, 4, 1000); // more entries than the file holds
        RequireRejected(bad, "not a Mach-O");
    }
    {
        mr::Bytes bad = good;
        Put32BE(bad, 4, 100);
        bad.resize(500);
        RequireRejected(bad, "Truncated fat_arch entry table");
    }
    {
        mr::Bytes bad = good;
        bad[4096] = 0; // slice 0 no longer starts with a Mach-O magic
        RequireRejected(bad, "not a Mach-O");
    }
}

TEST_CASE("layout: damaged load commands are rejected with a named message", "[MachOLayout]")
{
    const mr::Bytes seg = [] {
        mr::Bytes b = Command(0x19, 72);
        b[8] = '_'; b[9] = '_'; b[10] = 'T'; b[11] = 'E'; b[12] = 'X'; b[13] = 'T';
        return b;
    }();
    CHECK_NOTHROW(Parse(Program({seg})));
    CHECK_NOTHROW(Parse(Program({Command(0x1D, 16, 200, 100)})));
    RequireRejected(Program({Command(0x99, 0)}), "is smaller than 8");
    RequireRejected(Program({Command(0x99, 10)}), "is not a multiple of 4");
    RequireRejected(Program({Command(0x99, 64)}, 512, 32), "runs past the end of the load commands");
    RequireRejected(Program({Command(0x19, 40)}), "LC_SEGMENT_64");
    RequireRejected(Program({Command(0x1D, 8)}), "LC_CODE_SIGNATURE");
    RequireRejected(Program({Command(0x1D, 16, 200, 100), Command(0x1D, 16, 200, 100)}), "second LC_CODE_SIGNATURE");
    RequireRejected(Program({Command(0x1D, 16, 500, 100)}), "extends past the end");
    RequireRejected(Program({Command(0x1D, 16, 40, 16)}), "inside the load commands");
    RequireRejected(Program({Command(0x0C, 16)}), "dylib command");
    RequireRejected(Program({Command(0x0C, 32, 5000)}), "outside the command");
    RequireRejected(Program({Command(0x99, 16), Command(0x99, 16)}, 512, 16),
                    "does not fit in the load command area");
    RequireRejected(Program({Command(0x99, 16)}, 512, 48), "load commands use");
    {
        mr::Bytes b = Program({Command(0x99, 16)});
        Put32LE(b, 16, 1);
        Put32LE(b, 20, 0xFFFFFF00); // sizeofcmds far past the end
        RequireRejected(b, "load commands");
    }
    {
        // Fewer commands than the area: the sizes do not add up to sizeofcmds.
        mr::Bytes b = Program({Command(0x99, 16), Command(0x99, 16)});
        Put32LE(b, 16, 1);
        RequireRejected(b, "load commands use");
    }
    {
        mr::Bytes b = Program({seg});
        Put32LE(b, 32 + 40, 100); // fileoff
        Put32LE(b, 32 + 48, 100000); // filesize past the end
        RequireRejected(b, "file range extends past the end");
    }
    {
        // A section table that does not fit in the command.
        mr::Bytes b = seg;
        Put32LE(b, 64, 3);
        RequireRejected(Program({b}), "sections that do not fit");
    }
}

TEST_CASE("layout: a program shorter than its header is rejected", "[MachOLayout]")
{
    RequireRejected(mr::Bytes{0xCF, 0xFA, 0xED, 0xFE, 0, 0, 0, 0}, "Mach-O header");
    mr::Bytes short_header{0xCF, 0xFA, 0xED, 0xFE};
    short_header.resize(24, 0);
    RequireRejected(short_header, "Mach-O header");
}

TEST_CASE("reference: the SHA-256 written out in the checker matches known answers", "[MachOLayout]")
{
    const mr::Bytes abc{'a', 'b', 'c'};
    CHECK(mr::Hex(mr::Sha256(abc.data(), abc.size())) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(mr::Hex(mr::Sha256(nullptr, 0)) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const mr::Bytes million(1000000, 'a');
    CHECK(mr::Hex(mr::Sha256(million.data(), million.size())) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("reference: the checker accepts the signature another tool wrote", "[MachOLayout]")
{
    for(const char *name : {"tiny-macho-x86_64-adhoc", "tiny-macho-arm64-adhoc", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        const mr::FileReport report = mr::CheckFile(Load(name));
        for(const std::string &p : report.problems)
        {
            INFO(p);
        }
        CHECK(report.Ok());
        REQUIRE_FALSE(report.slices.empty());
        for(const mr::SliceReport &s : report.slices)
        {
            CHECK(s.has_signature);
            CHECK(s.signature_commands == 1);
            CHECK(s.n_code_slots == (s.code_limit + 4095) / 4096);
            REQUIRE(s.page_ok.size() == s.n_code_slots);
            for(bool ok : s.page_ok)
            {
                CHECK(ok);
            }
        }
    }
    const mr::FileReport unsigned_file = mr::CheckFile(Load("tiny-macho-universal"));
    CHECK(unsigned_file.Ok());
    CHECK_FALSE(unsigned_file.slices[0].has_signature);
}

TEST_CASE("reference: the checker finds a changed page and bytes after the signature", "[MachOLayout]")
{
    mr::Bytes file = Load("tiny-macho-x86_64-adhoc");
    file[3000] ^= 0x01; // inside page 0, after the load commands
    const mr::FileReport flipped = mr::CheckFile(file);
    REQUIRE(flipped.slices.size() == 1);
    REQUIRE(flipped.slices[0].page_ok.size() == 2);
    CHECK_FALSE(flipped.slices[0].page_ok[0]);
    CHECK(flipped.slices[0].page_ok[1]);
    CHECK_FALSE(flipped.Ok());

    const mr::FileReport after = mr::CheckFile(Load("tiny-macho-x86_64-data-after-sig"));
    CHECK_FALSE(after.Ok());

    CHECK(mr::FirstDifferenceOutside(Load("tiny-macho-x86_64-adhoc"), file, {}, 4000) == 3000);
    CHECK(mr::FirstDifferenceOutside(Load("tiny-macho-x86_64-adhoc"), file, {{3000, 1}}, 4000) == -1);
}
