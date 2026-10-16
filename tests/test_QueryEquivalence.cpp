// A question about a file gets the same answer, or the same refusal, whichever
// way the file is read.
//
// Each query (kind detection, signature presence, installer detection) is run
// through its public call and through a reference that reads the whole file
// into memory and runs the parser over those bytes. The two must agree on
// every input: the same value, or the same exception type with the same
// what(). The inputs are:
//
//   - every file in tests/fixtures;
//   - every file of the committed fuzz corpus and the in-code fuzz seeds;
//   - every prefix of the small samples up to a few hundred bytes, and a spread
//     of longer prefixes (a file shorter than its own headers);
//   - each sample with one 32-bit header field set to a boundary value;
//   - a missing path, a folder, an empty file;
//   - a file that shrinks and grows while it is queried (no crash, and every
//     outcome is an answer or a std::runtime_error).
//
// The reference mirrors the in-memory parse of each query; it does not use the
// public call.

#include "MalformedInput.hpp"
#include "MsiTestSupport.hpp"

#include "fuzz/FuzzTargets.hpp"

#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MsiSigner.hpp>
#include <libthe-seed/PeSigner.hpp>

#include "internal/BoundedBytes.hpp"
#include "internal/CfbReader.hpp"
#include "internal/FileIO.hpp"
#include "internal/MachOLayout.hpp"
#include "internal/MsiAuthenticode.hpp"
#include "internal/PeParser.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef FUZZ_CORPUS_DIR
#error "FUZZ_CORPUS_DIR must be defined by the build"
#endif

SEED_DEFINE_HEAP_COUNTER()

namespace
{

namespace in = the_seed::internal;
namespace sm = seedtest::malformed;
using sm::Bytes;
using in::ByteOrder;
using in::ByteSpan;
using in::ThrowMalformed;

// ---------------------------------------------------------------------------
// The reference: the in-memory parse of each query.

constexpr const char *kPeFormat = "PE";
constexpr const char *kMachOFormat = "Mach-O";
constexpr const char *kMsiFormat = "MSI";

struct PeLayout
{
    std::uint64_t optional_header_offset = 0;
    std::uint64_t dd_security_offset = 0;
};

PeLayout ReferencePeLayout(const ByteSpan &file)
{
    using namespace in::pe;
    const ByteSpan dos = file.Sub(0, sizeof(ImageDosHeader), "DOS header");
    if(dos.Read<std::uint16_t>(0, ByteOrder::Little, "DOS header signature") != kImageDosSignature)
    {
        ThrowMalformed(kPeFormat, "DOS header signature is not MZ");
    }
    const auto e_lfanew = dos.Read<std::int32_t>(60, ByteOrder::Little, "e_lfanew");
    if(e_lfanew < 0)
    {
        ThrowMalformed(kPeFormat, "e_lfanew " + std::to_string(e_lfanew) + " is negative");
    }
    const std::uint64_t pe_header_offset = static_cast<std::uint64_t>(e_lfanew);
    const ByteSpan pe_header = file.Sub(pe_header_offset, 4 + 20 + 2, "PE header");
    if(pe_header.Read<std::uint32_t>(0, ByteOrder::Little, "PE signature") != kImageNtSignature)
    {
        ThrowMalformed(kPeFormat, "PE header signature is invalid");
    }
    PeLayout layout;
    layout.optional_header_offset = pe_header_offset + 4 + 20;
    const auto optional_magic = pe_header.Read<std::uint16_t>(24, ByteOrder::Little, "optional header magic");
    if(optional_magic != kImageNtOptionalHdr32Magic && optional_magic != kImageNtOptionalHdr64Magic)
    {
        ThrowMalformed(kPeFormat, "PE header: optional header format " + std::to_string(optional_magic) +
                                      " is not supported");
    }
    const bool is_pe32_plus = optional_magic == kImageNtOptionalHdr64Magic;
    const std::uint64_t checksum_offset = layout.optional_header_offset + 64;
    layout.dd_security_offset = layout.optional_header_offset + (is_pe32_plus ? 144 : 128);
    const std::uint64_t num_rva_offset = layout.optional_header_offset + (is_pe32_plus ? 108 : 92);
    const auto num_rva = file.Read<std::uint32_t>(num_rva_offset, ByteOrder::Little, "PE header NumberOfRvaAndSizes");
    if(num_rva < 5)
    {
        ThrowMalformed(kPeFormat, "PE header has " + std::to_string(num_rva) +
                                      " data directory entries, fewer than 5");
    }
    file.Sub(checksum_offset, 4, "PE header CheckSum field");
    file.Sub(layout.dd_security_offset, 8, "PE header certificate directory entry");
    return layout;
}

bool ReferencePeSigned(const ByteSpan &file, const PeLayout &layout)
{
    const std::uint32_t address =
        file.Read<std::uint32_t>(layout.dd_security_offset, ByteOrder::Little, "certificate directory address");
    const std::uint32_t size =
        file.Read<std::uint32_t>(layout.dd_security_offset + 4, ByteOrder::Little, "certificate directory size");
    if(address == 0 || size == 0)
    {
        return false;
    }
    const ByteSpan span = file.Sub(address, size, "certificate table");
    const std::uint64_t headers_end = layout.dd_security_offset + 8;
    if(address < headers_end)
    {
        ThrowMalformed(kPeFormat, "certificate table (offset " + std::to_string(address) + ", size " +
                                      std::to_string(size) + ") overlaps the PE headers (" +
                                      std::to_string(headers_end) + " bytes)");
    }
    if(size < 8)
    {
        ThrowMalformed(kPeFormat, "certificate table size " + std::to_string(size) +
                                      " is smaller than a WIN_CERTIFICATE header (8 bytes)");
    }
    const std::uint32_t length = span.Read<std::uint32_t>(0, ByteOrder::Little, "WIN_CERTIFICATE length");
    if(length < 8)
    {
        ThrowMalformed(kPeFormat, "WIN_CERTIFICATE length " + std::to_string(length) +
                                      " is smaller than its 8-byte header");
    }
    if(length > size)
    {
        ThrowMalformed(kPeFormat, "WIN_CERTIFICATE length " + std::to_string(length) +
                                      " is larger than the certificate table (" + std::to_string(size) +
                                      " bytes)");
    }
    const auto revision = span.Read<std::uint16_t>(4, ByteOrder::Little, "WIN_CERTIFICATE revision");
    const auto type = span.Read<std::uint16_t>(6, ByteOrder::Little, "WIN_CERTIFICATE type");
    if(revision != 0x0200 || type != 0x0002)
    {
        ThrowMalformed(kPeFormat, "WIN_CERTIFICATE revision " + std::to_string(revision) + " or type " +
                                      std::to_string(type) + " is not supported");
    }
    return true;
}

bool ReferencePeHasSignature(const std::string &path)
{
    return in::GuardEntryPoint(kPeFormat, [&] {
        const auto bytes = in::ReadFileBytes(path);
        const ByteSpan file(bytes, kPeFormat);
        const PeLayout layout = ReferencePeLayout(file);
        return ReferencePeSigned(file, layout);
    });
}

the_seed::MachOParser::Format ReferenceDetect(const std::string &path)
{
    using Format = the_seed::MachOParser::Format;
    return in::GuardEntryPoint(kMachOFormat, [&]() {
        const auto bytes = in::ReadFileBytes(path);
        switch(in::ClassifyMachOMagic(ByteSpan(bytes, kMachOFormat)))
        {
        case in::MachOKind::Thin32Little:
        case in::MachOKind::Thin32Big:
            return Format::MachO32;
        case in::MachOKind::Thin64Little:
        case in::MachOKind::Thin64Big:
            return Format::MachO64;
        case in::MachOKind::Fat32:
        case in::MachOKind::Fat64:
            return Format::Fat;
        default:
            return Format::NotMachO;
        }
    });
}

[[noreturn]] void Refuse(const std::string &path, const std::string &text)
{
    throw std::runtime_error(path + ": " + text);
}

[[noreturn]] void RefuseSlice(const std::string &path, bool universal, std::size_t index, std::uint32_t cputype,
                              const std::string &reason)
{
    if(universal)
    {
        Refuse(path, "slice " + std::to_string(index) + " (" + in::ArchName(cputype) + "): " + reason);
    }
    Refuse(path, reason);
}

bool ReferenceMachOSigned(const std::string &path)
{
    return in::GuardEntryPoint(kMachOFormat, [&]() {
        std::vector<std::uint8_t> bytes;
        try
        {
            bytes = in::ReadFileBytes(path);
        }
        catch(const std::runtime_error &error)
        {
            ThrowMalformed(kMachOFormat, error.what());
        }
        const ByteSpan file(bytes, kMachOFormat);
        if(bytes.size() < 4)
        {
            ThrowMalformed(kMachOFormat, "File too small to be a Mach-O binary");
        }
        const in::MachOContainer container = in::ParseMachOContainer(file);
        for(std::size_t i = 0; i < container.entries.size(); ++i)
        {
            const in::ContainerEntry &entry = container.entries[i];
            if(entry.slice.support != in::SliceSupport::Supported)
            {
                RefuseSlice(path, container.form != in::ContainerForm::Thin, i, entry.cputype,
                            in::DeclineText(entry.slice.support));
            }
            if(!entry.slice.codesig.has_value() || entry.slice.codesig->dataoff == 0 ||
               entry.slice.codesig->datasize == 0)
            {
                return false;
            }
            const ByteSpan region = file.Sub(entry.offset + entry.slice.codesig->dataoff,
                                             entry.slice.codesig->datasize, "LC_CODE_SIGNATURE data");
            if(region.Size() >= 12)
            {
                (void)region.Read<std::uint32_t>(0, ByteOrder::Big, "SuperBlob magic");
            }
        }
        return true;
    });
}

bool ReferenceMsiHasSignature(const std::string &path)
{
    return in::GuardEntryPoint(kMsiFormat, [&] {
        try
        {
            const std::vector<std::uint8_t> bytes = in::ReadFileBytes(path);
            in::PackageModel model = in::CfbReader::Parse(bytes);
            const in::CfbNode *entry = in::PackageModel::FindByEnumeration(model.Root(), in::MsiSignatureName());
            if(entry == nullptr || entry->size == 0)
            {
                return false;
            }
            (void)model.Resolve(*entry);
            return true;
        }
        catch(const std::runtime_error &error)
        {
            const std::string prefix = std::string(kMsiFormat) + ": ";
            const std::string message = error.what();
            if(message.compare(0, prefix.size(), prefix) != 0 || message.find(path) != std::string::npos)
            {
                throw;
            }
            throw std::runtime_error(prefix + path + ": " + message.substr(prefix.size()));
        }
    });
}

bool ReferenceIsMsi(const std::string &path)
{
    static const std::uint8_t magic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    try
    {
        const auto bytes = in::ReadFileBytes(path);
        return bytes.size() >= 8 && std::memcmp(bytes.data(), magic, 8) == 0;
    }
    catch(const std::exception &)
    {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Comparing the two paths.

struct Answer
{
    bool threw = false;
    std::string type;
    std::string what;
    int value = 0;

    bool operator==(const Answer &other) const
    {
        return this->threw == other.threw && this->type == other.type && this->what == other.what &&
               this->value == other.value;
    }
};

template <typename F>
Answer Capture(F &&query)
{
    Answer answer;
    try
    {
        answer.value = static_cast<int>(query());
    }
    catch(const std::exception &e)
    {
        answer.threw = true;
        answer.type = typeid(e).name();
        answer.what = e.what();
    }
    return answer;
}

std::string Describe(const Answer &answer)
{
    if(answer.threw)
    {
        return "throws " + answer.type + ": " + answer.what;
    }
    return "returns " + std::to_string(answer.value);
}

struct Query
{
    const char *name;
    std::function<int(const std::string &)> through_public;
    std::function<int(const std::string &)> through_memory;
};

const std::vector<Query> &Queries()
{
    using the_seed::MachOParser;
    static const std::vector<Query> queries = {
        {"MachOParser::DetectFormat",
         [](const std::string &p) { return static_cast<int>(MachOParser::DetectFormat(p)); },
         [](const std::string &p) { return static_cast<int>(ReferenceDetect(p)); }},
        {"MachOParser::IsMachO", [](const std::string &p) { return MachOParser::IsMachO(p) ? 1 : 0; },
         [](const std::string &p) {
             return ReferenceDetect(p) != MachOParser::Format::NotMachO ? 1 : 0;
         }},
        {"MachOParser::IsFatBinary", [](const std::string &p) { return MachOParser::IsFatBinary(p) ? 1 : 0; },
         [](const std::string &p) { return ReferenceDetect(p) == MachOParser::Format::Fat ? 1 : 0; }},
        {"PeSigner::HasEmbeddedSignature",
         [](const std::string &p) { return the_seed::PeSigner::HasEmbeddedSignature(p) ? 1 : 0; },
         [](const std::string &p) { return ReferencePeHasSignature(p) ? 1 : 0; }},
        {"MachOSigner::HasEmbeddedSignature",
         [](const std::string &p) { return the_seed::MachOSigner::HasEmbeddedSignature(p) ? 1 : 0; },
         [](const std::string &p) { return ReferenceMachOSigned(p) ? 1 : 0; }},
        {"MsiSigner::HasEmbeddedSignature",
         [](const std::string &p) { return the_seed::MsiSigner::HasEmbeddedSignature(p) ? 1 : 0; },
         [](const std::string &p) { return ReferenceMsiHasSignature(p) ? 1 : 0; }},
        {"MsiSigner::IsMsi", [](const std::string &p) { return the_seed::MsiSigner::IsMsi(p) ? 1 : 0; },
         [](const std::string &p) { return ReferenceIsMsi(p) ? 1 : 0; }},
    };
    return queries;
}

std::size_t g_compared = 0;
std::size_t g_threw = 0;

// Runs every query on the path both ways; fails on the first disagreement of
// each query.
void CompareAll(const std::string &path, const std::string &label)
{
    for(const Query &query : Queries())
    {
        const Answer actual = Capture([&] { return query.through_public(path); });
        const Answer expected = Capture([&] { return query.through_memory(path); });
        ++g_compared;
        if(expected.threw)
        {
            ++g_threw;
        }
        if(!(actual == expected))
        {
            INFO(query.name << " on " << label << ": public call " << Describe(actual) << "; reference "
                            << Describe(expected));
            FAIL_CHECK("the two ways of reading disagree");
        }
    }
}

// ---------------------------------------------------------------------------
// The inputs.

bool IsSample(const std::filesystem::path &path)
{
    const std::string name = path.filename().string();
    return std::filesystem::is_regular_file(path) &&
           (name.find("tiny") == 0 || name.find("nested") == 0 || name.find("two-neighbours") == 0 ||
            name.find("legacy") == 0 || name == "test.dll" || name == "msi-open.exe");
}

std::vector<std::filesystem::path> Fixtures()
{
    std::vector<std::filesystem::path> files;
    for(const auto &entry : std::filesystem::directory_iterator(seedtest::FixturePath("")))
    {
        if(entry.is_regular_file())
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::size_t> PrefixLengths(std::size_t size)
{
    std::vector<std::size_t> lengths;
    for(std::size_t length = 0; length <= std::min<std::size_t>(size, 300); ++length)
    {
        lengths.push_back(length);
    }
    for(std::size_t boundary = 512; boundary < size; boundary *= 2)
    {
        lengths.push_back(boundary - 1);
        lengths.push_back(boundary);
        lengths.push_back(boundary + 1);
    }
    for(std::size_t part = 1; part < 8; ++part)
    {
        lengths.push_back(size * part / 8);
    }
    if(size > 0)
    {
        lengths.push_back(size - 1);
    }
    std::sort(lengths.begin(), lengths.end());
    lengths.erase(std::unique(lengths.begin(), lengths.end()), lengths.end());
    lengths.erase(std::remove_if(lengths.begin(), lengths.end(), [&](std::size_t l) { return l > size; }),
                  lengths.end());
    return lengths;
}

} // namespace

TEST_CASE("every fixture gives the same answer both ways", "[QueryEquivalence]")
{
    const auto files = Fixtures();
    REQUIRE(files.size() > 20);
    for(const auto &file : files)
    {
        CompareAll(file.string(), file.filename().string());
    }
    // A signed and an unsigned sample of each kind must have been among them.
    CHECK(the_seed::PeSigner::HasEmbeddedSignature(seedtest::FixturePath("tiny.exe")) == false);
    CHECK(the_seed::MsiSigner::HasEmbeddedSignature(seedtest::FixturePath("tiny-osslsig-small.msi")));
    CHECK(the_seed::MachOSigner::HasEmbeddedSignature(seedtest::FixturePath("tiny-macho-x86_64-adhoc")));
    CHECK_FALSE(the_seed::MachOSigner::HasEmbeddedSignature(seedtest::FixturePath("tiny-macho-x86_64")));
}

TEST_CASE("the library itself, a signed PE and the in-code seeds give the same answer both ways",
          "[QueryEquivalence]")
{
    seedtest::ScratchDir scratch("query-equivalence");
    CompareAll(seedtest::LibraryPath(), "the library under test");

    const std::string signed_pe = scratch.File("signed.exe");
    std::filesystem::copy_file(seedtest::FixturePath("tiny.exe"), signed_pe);
    the_seed::PeSigner::EmbedSignature(signed_pe, Bytes(300, 0x30));
    CompareAll(signed_pe, "tiny.exe signed");
    CHECK(the_seed::PeSigner::HasEmbeddedSignature(signed_pe));

    CompareAll(sm::WriteScratch(scratch, "elf64le.bin", seedfuzz::ElfSeed(true, true)), "ElfSeed 64 little");
    CompareAll(sm::WriteScratch(scratch, "elf32be.bin", seedfuzz::ElfSeed(false, false)), "ElfSeed 32 big");
    CompareAll(sm::WriteScratch(scratch, "superblob.bin", seedfuzz::SuperBlobSeed()), "SuperBlobSeed");
}

TEST_CASE("every fuzz corpus entry gives the same answer both ways", "[QueryEquivalence]")
{
    std::size_t count = 0;
    for(const auto &target : std::filesystem::directory_iterator(FUZZ_CORPUS_DIR))
    {
        if(!target.is_directory())
        {
            continue;
        }
        for(const auto &entry : std::filesystem::directory_iterator(target.path()))
        {
            if(entry.is_regular_file() && entry.path().filename() != ".gitkeep")
            {
                CompareAll(entry.path().string(), entry.path().string());
                ++count;
            }
        }
    }
    // The committed corpus holds only the placeholders of its folders while no
    // input has needed minimising; the case then compares nothing, by design.
    std::cout << "query equivalence: " << count << " corpus entries" << std::endl;
}

TEST_CASE("empty, missing, folder and short inputs give the same answer both ways", "[QueryEquivalence]")
{
    seedtest::ScratchDir scratch("query-equivalence");
    CompareAll(scratch.File("does-not-exist"), "missing path");
    CompareAll(scratch.Path().string(), "a folder");
    CompareAll(sm::WriteScratch(scratch, "empty", Bytes{}), "empty file");
    for(std::size_t length = 1; length <= 16; ++length)
    {
        CompareAll(sm::WriteScratch(scratch, "short", Bytes(length, 0x4D)), "short file of " + std::to_string(length));
        Bytes cfb_start = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1, 0, 0, 0, 0, 0, 0, 0, 0};
        cfb_start.resize(length);
        CompareAll(sm::WriteScratch(scratch, "short-cfb", cfb_start), "short installer start of " + std::to_string(length));
    }
    // A signature at the very end of a file that is cut inside it.
    Bytes pe = sm::LoadSample("tiny.exe");
    const std::string signed_pe = scratch.File("signed.exe");
    sm::WriteScratch(scratch, "signed.exe", pe);
    the_seed::PeSigner::EmbedSignature(signed_pe, Bytes(300, 0x30));
    const Bytes signed_bytes = sm::ReadAll(signed_pe);
    for(std::size_t cut = 1; cut <= 320; cut += 7)
    {
        CompareAll(sm::WriteScratch(scratch, "cut.exe", sm::Truncate(signed_bytes, signed_bytes.size() - cut)),
                   "signed PE cut by " + std::to_string(cut));
    }
}

TEST_CASE("every prefix of the samples gives the same answer both ways", "[QueryEquivalence]")
{
    seedtest::ScratchDir scratch("query-equivalence");
    for(const auto &file : Fixtures())
    {
        if(!IsSample(file))
        {
            continue;
        }
        const Bytes bytes = sm::ReadAll(file.string());
        for(const std::size_t length : PrefixLengths(bytes.size()))
        {
            CompareAll(sm::WriteScratch(scratch, "prefix", sm::Truncate(bytes, length)),
                       file.filename().string() + " cut to " + std::to_string(length));
        }
    }
}

TEST_CASE("a header field at a boundary value gives the same answer both ways", "[QueryEquivalence]")
{
    seedtest::ScratchDir scratch("query-equivalence");
    const std::uint32_t values[] = {0u, 1u, 0x200u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu};
    std::size_t files = 0;
    for(const auto &file : Fixtures())
    {
        if(!IsSample(file))
        {
            continue;
        }
        const Bytes original = sm::ReadAll(file.string());
        if(original.size() < 64 || original.size() > 64 * 1024)
        {
            continue;
        }
        ++files;
        const std::string name = file.filename().string();
        std::vector<std::uint64_t> offsets;
        // The first 512 bytes in 4-byte steps, plus the headers that follow.
        for(std::uint64_t offset = 0; offset + 4 <= std::min<std::uint64_t>(original.size(), 512); offset += 4)
        {
            offsets.push_back(offset);
        }
        if(name.find(".exe") != std::string::npos || name.find(".dll") != std::string::npos)
        {
            const std::uint64_t pe = sm::PeHeaderOffset(original);
            for(std::uint64_t offset = pe; offset + 4 <= std::min<std::uint64_t>(original.size(), pe + 280); offset += 4)
            {
                offsets.push_back(offset);
            }
        }
        else if(name.find("macho") != std::string::npos)
        {
            for(std::uint32_t command = 0; command < 2; ++command)
            {
                const std::uint64_t base = original[0] == 0xCF || original[0] == 0xCE
                                               ? sm::MachOCommandOffset(original, command)
                                               : 0;
                for(std::uint64_t offset = base; offset + 4 <= std::min<std::uint64_t>(original.size(), base + 80);
                    offset += 4)
                {
                    offsets.push_back(offset);
                }
            }
        }
        else if(name.find(".msi") != std::string::npos)
        {
            for(std::uint32_t index = 0; index < 6; ++index)
            {
                const std::uint64_t entry = sm::CfbDirectoryEntryOffset(original, index);
                for(std::uint64_t offset = entry; offset + 4 <= std::min<std::uint64_t>(original.size(), entry + 128);
                    offset += 4)
                {
                    offsets.push_back(offset);
                }
            }
        }
        std::sort(offsets.begin(), offsets.end());
        offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
        for(const std::uint64_t offset : offsets)
        {
            for(const std::uint32_t value : values)
            {
                Bytes patched = original;
                sm::PatchLE<std::uint32_t>(patched, offset, value);
                CompareAll(sm::WriteScratch(scratch, "patched", patched),
                           name + " with the word at " + std::to_string(offset) + " set to " + std::to_string(value));
                if(offset + 4 <= patched.size() && (name.find("universal") != std::string::npos))
                {
                    sm::PatchBE<std::uint32_t>(patched, offset, value);
                    CompareAll(sm::WriteScratch(scratch, "patched", patched),
                               name + " with the big-endian word at " + std::to_string(offset) + " set to " +
                                   std::to_string(value));
                }
            }
        }
    }
    CHECK(files > 10);
    std::cout << "query equivalence: " << g_compared << " comparisons, " << g_threw << " refusals" << std::endl;
}

TEST_CASE("a file that shrinks and grows while it is queried gives answers or refusals only",
          "[QueryEquivalence]")
{
    seedtest::ScratchDir scratch("query-equivalence");
    const std::vector<std::string> samples = {"tiny.exe", "tiny-macho-universal-adhoc", "tiny-osslsig-small.msi",
                                              "tiny.msi"};
    for(const std::string &sample : samples)
    {
        const Bytes original = sm::LoadSample(sample);
        const std::string path = sm::WriteScratch(scratch, "moving", original);
        std::atomic<bool> stop{false};
        std::thread changer([&] {
            std::size_t step = 0;
            std::error_code ec;
            while(!stop.load())
            {
                const std::size_t length = original.size() * (step++ % 5) / 4;
                std::filesystem::resize_file(path, length, ec);
                std::filesystem::resize_file(path, original.size(), ec);
            }
        });
        std::size_t answers = 0;
        std::size_t refusals = 0;
        for(int round = 0; round < 300; ++round)
        {
            for(const Query &query : Queries())
            {
                try
                {
                    (void)query.through_public(path);
                    ++answers;
                }
                catch(const std::runtime_error &)
                {
                    ++refusals;
                }
            }
        }
        stop.store(true);
        changer.join();
        std::cout << "query equivalence: " << sample << " while changing: " << answers << " answers, " << refusals
                  << " refusals" << std::endl;
        CHECK(answers + refusals == 300 * Queries().size());
    }
}
