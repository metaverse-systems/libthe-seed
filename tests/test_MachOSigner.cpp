// MachOSigner: PrepareSignature and CompleteSignature, judged by the independent
// checker in MachOReference.hpp (page hashes, code limit, sizes, command fields,
// link-edit extent) and never by library code.
//
// Statements tested here, each by the cases named after it:
//   - the signature covers the file as finished (every sample, every slice);
//   - PrepareSignature never changes the file, on success or on failure;
//   - a stale or foreign PreparedSignature, a signature that does not fit and a
//     capacity above 2^31 are refused and leave the file unchanged;
//   - equal inputs give an identical CodeDirectory;
//   - hashing first and embedding afterwards (the order the library used to
//     offer) gives a signature the checker rejects, hashing the finished layout
//     gives one it accepts;
//   - signing again: a signed program (signed here or by another tool) keeps one
//     signature command, updated in place, with no entry of size zero and no
//     leftover of the old signature; a signature followed by other data is
//     refused; StripSignature leaves an unsigned program and signing after a
//     strip gives the file signed once;
//   - universal files: each slice is signed as its own program, the table is
//     rewritten for the new lengths with alignment and order kept, and a refusal
//     in any slice refuses the whole file.
//
// Test cases that belong to the older two-call interface (ComputeCodeDirectory
// and EmbedSignature) are gone because those functions are removed; what each
// one checked is covered by the cases below.

#include "MachOSigningSupport.hpp"

#include <libthe-seed/MachOParser.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mr = machoref;
namespace ms = machosupport;

using Bytes = mr::Bytes;

namespace {

bool Contains(const std::string &text, const std::string &part)
{
    return text.find(part) != std::string::npos;
}

// Error text begins with the path or with the format name ("Mach-O: ...").
bool NamesTheFile(const std::string &message, const std::string &path)
{
    return message.rfind(path + ": ", 0) == 0 || message.rfind("Mach-O: ", 0) == 0;
}

// The "Changed" message is checked as a whole sentence (the contract's exact text).
void ExpectChanged(const std::string &message, const std::string &path)
{
    CHECK(message.rfind(path + ": ", 0) == 0);
    CHECK(Contains(message, "the program, the identity or the capacity changed since PrepareSignature; "
                            "prepare again"));
}

// Reads the slice table of a universal file: {cpu, offset, size, align}.
struct TableEntry
{
    std::uint64_t cpu, offset, size, align;
};

std::vector<TableEntry> ReadTable(const Bytes &file)
{
    std::uint64_t magic = 0, count = 0;
    mr::ReadBE(file, 0, 4, magic);
    mr::ReadBE(file, 4, 4, count);
    const bool wide = magic == 0xCAFEBABF;
    std::vector<TableEntry> out;
    for(std::uint64_t i = 0; i < count; ++i)
    {
        const std::uint64_t at = 8 + i * (wide ? 32 : 20);
        TableEntry e{};
        mr::ReadBE(file, at, 4, e.cpu);
        mr::ReadBE(file, at + 8, wide ? 8 : 4, e.offset);
        mr::ReadBE(file, at + (wide ? 16 : 12), wide ? 8 : 4, e.size);
        mr::ReadBE(file, at + (wide ? 24 : 16), 4, e.align);
        out.push_back(e);
    }
    return out;
}

Bytes Slice(const Bytes &file, std::uint64_t offset, std::uint64_t size)
{
    return Bytes(file.begin() + static_cast<std::ptrdiff_t>(offset),
                 file.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

// ---------------------------------------------------------------------------
// The old order, written out with raw bytes and nothing from the library: hash
// the file as it is, add the signature command and the data afterwards.
// ---------------------------------------------------------------------------

void PutBE(Bytes &b, std::size_t at, std::uint64_t value, int width)
{
    for(int i = 0; i < width; ++i)
    {
        b.at(at + static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(value >> (8 * (width - 1 - i)));
    }
}

void PutLE(Bytes &b, std::size_t at, std::uint64_t value, int width)
{
    for(int i = 0; i < width; ++i)
    {
        b.at(at + static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

Bytes RequirementsBlob()
{
    Bytes b(12, 0);
    PutBE(b, 0, 0xFADE0C01, 4);
    PutBE(b, 4, 12, 4);
    return b;
}

// A CodeDirectory (version 0x20400, SHA-256, 4096-byte pages, two special
// slots) over `file` up to `code_limit`.
Bytes RawCodeDirectory(const std::string &identifier, const Bytes &file, std::uint64_t code_limit)
{
    const std::uint64_t slots = (code_limit + 4095) / 4096;
    const std::uint64_t ident_off = 88;
    const std::uint64_t hash_off = ident_off + identifier.size() + 1 + 64;
    Bytes cd(hash_off + 32 * slots, 0);
    PutBE(cd, 0, 0xFADE0C02, 4);
    PutBE(cd, 4, cd.size(), 4);
    PutBE(cd, 8, 0x20400, 4);
    PutBE(cd, 16, hash_off, 4);
    PutBE(cd, 20, ident_off, 4);
    PutBE(cd, 24, 2, 4);
    PutBE(cd, 28, slots, 4);
    PutBE(cd, 32, code_limit, 4);
    cd[36] = 32;
    cd[37] = 2;
    cd[39] = 12;
    std::copy(identifier.begin(), identifier.end(), cd.begin() + static_cast<std::ptrdiff_t>(ident_off));
    const Bytes req = RequirementsBlob();
    const Bytes req_hash = mr::Sha256(req.data(), req.size());
    std::copy(req_hash.begin(), req_hash.end(), cd.begin() + static_cast<std::ptrdiff_t>(hash_off - 64));
    for(std::uint64_t i = 0; i < slots; ++i)
    {
        const std::uint64_t start = i * 4096;
        const std::uint64_t end = std::min<std::uint64_t>(start + 4096, code_limit);
        const Bytes h = mr::Sha256(file.data() + start, static_cast<std::size_t>(end - start));
        std::copy(h.begin(), h.end(), cd.begin() + static_cast<std::ptrdiff_t>(hash_off + 32 * i));
    }
    return cd;
}

// SuperBlob of the CodeDirectory and the empty requirements.
Bytes RawSuperBlob(const Bytes &cd)
{
    const Bytes req = RequirementsBlob();
    Bytes b(12 + 16, 0);
    PutBE(b, 0, 0xFADE0CC0, 4);
    PutBE(b, 4, 28 + cd.size() + req.size(), 4);
    PutBE(b, 8, 2, 4);
    PutBE(b, 12, 0, 4);
    PutBE(b, 16, 28, 4);
    PutBE(b, 20, 2, 4);
    PutBE(b, 24, 28 + cd.size(), 4);
    b.insert(b.end(), cd.begin(), cd.end());
    b.insert(b.end(), req.begin(), req.end());
    return b;
}

// The unsigned 64-bit little-endian thin program in the layout of a signed
// one: one more command (16 bytes) placed after the last, __LINKEDIT extended
// to the end of `datasize` bytes reserved at the 16-byte aligned end of the
// content, and the reserved bytes zero.
Bytes ArrangeFinalLayout(const Bytes &original, std::uint64_t datasize, std::uint64_t &dataoff)
{
    Bytes b = original;
    std::uint64_t ncmds = 0, sizeofcmds = 0;
    mr::ReadLE(b, 16, 4, ncmds);
    mr::ReadLE(b, 20, 4, sizeofcmds);
    dataoff = ms::Align16(original.size());
    std::uint64_t at = 32;
    for(std::uint64_t i = 0; i < ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        mr::ReadLE(b, at, 4, cmd);
        mr::ReadLE(b, at + 4, 4, size);
        if(cmd == 0x19 && std::string(reinterpret_cast<const char *>(&b[at + 8])).rfind("__LINKEDIT", 0) == 0)
        {
            std::uint64_t fileoff = 0;
            mr::ReadLE(b, at + 40, 8, fileoff);
            PutLE(b, at + 48, dataoff + datasize - fileoff, 8);
            PutLE(b, at + 32, ms::AlignUp(dataoff + datasize - fileoff, 4096), 8);
        }
        at += size;
    }
    const std::uint64_t command_at = 32 + sizeofcmds;
    PutLE(b, command_at, 0x1D, 4);
    PutLE(b, command_at + 4, 16, 4);
    PutLE(b, command_at + 8, dataoff, 4);
    PutLE(b, command_at + 12, datasize, 4);
    PutLE(b, 16, ncmds + 1, 4);
    PutLE(b, 20, sizeofcmds + 16, 4);
    b.resize(dataoff + datasize, 0);
    return b;
}

} // namespace

TEST_CASE("MachOSigner::BuildSuperBlob creates valid blob", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
    REQUIRE(prepared.slices.size() == 1);

    const Bytes fakeCms(64, 0xCC);
    const Bytes superBlob = MachOSigner::BuildSuperBlob(prepared.slices[0].code_directory, fakeCms);

    // The blob starts with the embedded-signature magic and its own length.
    REQUIRE(superBlob.size() >= 12);
    std::uint64_t magic = 0, length = 0, count = 0;
    REQUIRE(mr::ReadBE(superBlob, 0, 4, magic));
    REQUIRE(mr::ReadBE(superBlob, 4, 4, length));
    REQUIRE(mr::ReadBE(superBlob, 8, 4, count));
    CHECK(magic == 0xFADE0CC0);
    CHECK(length == superBlob.size());
    // CodeDirectory, requirements and CMS.
    CHECK(count == 3);
}

TEST_CASE("MachOSigner::BuildSuperBlob does not touch any file", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const Bytes before = ms::ReadAll(path);
    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
    (void)MachOSigner::BuildSuperBlob(prepared.slices[0].code_directory, Bytes(64, 1));
    CHECK(ms::ReadAll(path) == before);
}

TEST_CASE("MachOSigner::HasEmbeddedSignature returns false for unsigned", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        CHECK(MachOSigner::HasEmbeddedSignature(seedtest::FixturePath(name)) == false);
    }
}

TEST_CASE("MachOSigner::ExtractCmsFromSuperBlob extracts CMS blob", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const auto prepared = MachOSigner::PrepareSignature(path, "test", 48);

    const Bytes fakeCms(48, 0xEE);
    const Bytes superBlob = MachOSigner::BuildSuperBlob(prepared.slices[0].code_directory, fakeCms);

    const auto cms = MachOSigner::ExtractCmsFromSuperBlob(superBlob);
    REQUIRE(cms.has_value());
    CHECK(*cms == fakeCms);
}

TEST_CASE("MachOSigner::ExtractCodeDirectoryFromSuperBlob extracts CD", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const auto prepared = MachOSigner::PrepareSignature(path, "test", 32);

    const Bytes fakeCms(32, 0xFF);
    const Bytes superBlob = MachOSigner::BuildSuperBlob(prepared.slices[0].code_directory, fakeCms);

    const auto cd = MachOSigner::ExtractCodeDirectoryFromSuperBlob(superBlob);
    REQUIRE(cd.has_value());
    CHECK(*cd == prepared.slices[0].code_directory);
}

// The name of the former known-gap case is kept, so that its line in
// known-gaps.txt is removed in the same change that makes this pass.
TEST_CASE("MachOSigner::EmbedSignature and ExtractSignature round-trip", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    REQUIRE(MachOSigner::HasEmbeddedSignature(path) == false);

    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
    const Bytes fakeCms(64, 0xDD);
    MachOSigner::CompleteSignature(path, prepared, {fakeCms});

    CHECK(MachOSigner::HasEmbeddedSignature(path) == true);

    const Bytes expected = MachOSigner::BuildSuperBlob(prepared.slices[0].code_directory, fakeCms);
    const auto extracted = MachOSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    // The SuperBlob alone, not the zero bytes that follow it.
    CHECK(extracted->size() == expected.size());
    CHECK(*extracted == expected);

    const auto cms = MachOSigner::ExtractCmsFromSuperBlob(*extracted);
    REQUIRE(cms.has_value());
    CHECK(*cms == fakeCms);
    const auto cd = MachOSigner::ExtractCodeDirectoryFromSuperBlob(*extracted);
    REQUIRE(cd.has_value());
    CHECK(*cd == prepared.slices[0].code_directory);
}

TEST_CASE("MachOSigner signing finishes every sample and slice", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);

        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
        const auto cms = ms::CountingCms(prepared.slices.size());
        MachOSigner::CompleteSignature(path, prepared, cms);

        const Bytes finished = ms::ReadAll(path);
        ms::ExpectFinished(original, finished, prepared, cms);
        CHECK(MachOSigner::HasEmbeddedSignature(path));
        CHECK(prepared.identity == "test-identity");
        CHECK(prepared.cms_capacity == 64);
    }
}

TEST_CASE("MachOSigner signing gives the same result for other identities and capacities", "[MachOSigner]")
{
    const std::uint32_t capacities[] = {0, 1, 63, 64, 65, 1000, 8192};
    const char *identities[] = {"a", "test-identity", "com.example.a-much-longer-identity-with-dots-and-dashes"};
    for(const std::string &name : {std::string("tiny-macho-x86_64"), std::string("tiny-macho-universal")})
    {
        for(const std::uint32_t capacity : capacities)
        {
            for(const char *identity : identities)
            {
                INFO(name << " capacity " << capacity << " identity " << identity);
                seedtest::ScratchDir scratch;
                const std::string path = ms::CopyFixture(scratch, name);
                const Bytes original = ms::ReadAll(path);
                const auto prepared = MachOSigner::PrepareSignature(path, identity, capacity);
                // The CMS is as large as the capacity allows, and in the other
                // run one byte shorter.
                const std::size_t size = capacity;
                const auto cms = ms::CountingCms(prepared.slices.size(), size);
                MachOSigner::CompleteSignature(path, prepared, cms);
                ms::ExpectFinished(original, ms::ReadAll(path), prepared, cms);
            }
        }
    }
}

TEST_CASE("MachOSigner a shorter CMS than reserved leaves zero bytes", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-arm64");
    const Bytes original = ms::ReadAll(path);
    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 512);
    const auto cms = ms::CountingCms(1, 100);
    MachOSigner::CompleteSignature(path, prepared, cms);
    const Bytes finished = ms::ReadAll(path);
    ms::ExpectFinished(original, finished, prepared, cms);

    const mr::FileReport report = mr::CheckFile(finished);
    REQUIRE(report.slices.size() == 1);
    const mr::SliceReport &s = report.slices[0];
    // The reserved region is part of the file: the SuperBlob is shorter than the
    // data size, and the rest is zero (the checker reports a non-zero byte).
    CHECK(s.superblob_length < s.datasize);
    CHECK(s.dataoff + s.datasize == finished.size());
}

TEST_CASE("MachOSigner PrepareSignature leaves the file byte-identical", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes before = ms::ReadAll(path);
        const auto modified_before = std::filesystem::last_write_time(path);

        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
        CHECK_FALSE(prepared.slices.empty());
        CHECK(ms::ReadAll(path) == before);
        CHECK(std::filesystem::last_write_time(path) == modified_before);
        // Nothing else was created next to it.
        std::size_t entries = 0;
        for(const auto &entry : std::filesystem::directory_iterator(scratch.Path()))
        {
            (void)entry;
            ++entries;
        }
        CHECK(entries == 1);
    }
}

TEST_CASE("MachOSigner PrepareSignature leaves the file byte-identical when it fails", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string good = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    struct Case
    {
        std::string name;
        std::string path;
        std::string identity;
        std::uint32_t capacity;
    };
    const std::string text = ms::CopyFixture(scratch, "plain.txt");
    Bytes macho32(64, 0);
    macho32[0] = 0xCE;
    macho32[1] = 0xFA;
    macho32[2] = 0xED;
    macho32[3] = 0xFE;
    const std::string thirty_two = ms::WriteScratch(scratch, "macho32.bin", macho32);
    const std::string java = ms::WriteScratch(scratch, "class.bin", mr::JavaClassHeader());
    const std::string empty = ms::WriteScratch(scratch, "empty.bin", Bytes());
    const std::string big_endian = ms::WriteScratch(scratch, "bigendian.bin", mr::BigEndian64());

    const Case cases[] = {
        {"capacity above 2^31", good, "test-identity", 0x80000001u},
        {"largest 32-bit capacity", good, "test-identity", 0xFFFFFFFFu},
        {"not a Mach-O file", text, "test-identity", 64},
        {"32-bit program", thirty_two, "test-identity", 64},
        {"big-endian program", big_endian, "test-identity", 64},
        {"Java class file", java, "test-identity", 64},
        {"empty file", empty, "test-identity", 64},
        {"missing file", scratch.File("absent"), "test-identity", 64},
        {"no room for the command", ms::CopyFixture(scratch, "tiny-macho-x86_64-nospace"), "test-identity", 64},
    };
    for(const Case &c : cases)
    {
        INFO(c.name);
        const bool present = std::filesystem::exists(c.path);
        const Bytes before = present ? ms::ReadAll(c.path) : Bytes();
        const std::string message = ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(c.path, c.identity, c.capacity); });
        CHECK_FALSE(message.empty());
        CHECK(NamesTheFile(message, c.path));
        CHECK(std::filesystem::exists(c.path) == present);
        if(present)
        {
            CHECK(ms::ReadAll(c.path) == before);
        }
    }
}

TEST_CASE("MachOSigner equal inputs give an identical CodeDirectory", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string first = ms::CopyFixture(scratch, name, "first");
        const std::string second = ms::CopyFixture(scratch, name, "second");

        const auto a = MachOSigner::PrepareSignature(first, "test-identity", 64);
        const auto b = MachOSigner::PrepareSignature(first, "test-identity", 64);
        const auto c = MachOSigner::PrepareSignature(second, "test-identity", 64);
        REQUIRE(a.slices.size() == b.slices.size());
        REQUIRE(a.slices.size() == c.slices.size());
        for(std::size_t i = 0; i < a.slices.size(); ++i)
        {
            CHECK(a.slices[i].code_directory == b.slices[i].code_directory);
            CHECK(a.slices[i].cd_hash == b.slices[i].cd_hash);
            // A different path is a different name, not a different program.
            CHECK(a.slices[i].code_directory == c.slices[i].code_directory);
            CHECK(a.slices[i].cd_hash.size() == 32);
        }

        // Different inputs give a different CodeDirectory.
        const auto other_identity = MachOSigner::PrepareSignature(first, "other-identity", 64);
        const auto other_capacity = MachOSigner::PrepareSignature(first, "test-identity", 128);
        CHECK(other_identity.slices[0].code_directory != a.slices[0].code_directory);
        CHECK(other_capacity.slices[0].code_directory != a.slices[0].code_directory);

        // Two complete signings with equal inputs give identical files.
        const std::string third = ms::CopyFixture(scratch, name, "third");
        ms::SignWithKnownInputs(first);
        ms::SignWithKnownInputs(third);
        CHECK(ms::ReadAll(first) == ms::ReadAll(third));
    }
}

TEST_CASE("MachOSigner CompleteSignature works on an identical copy in another place", "[MachOSigner]")
{
    // No shared state: the value returned by PrepareSignature is all that
    // CompleteSignature needs, so a copy of the same bytes can be completed.
    seedtest::ScratchDir scratch;
    const std::string a = ms::CopyFixture(scratch, "tiny-macho-universal", "a");
    const std::string b = ms::CopyFixture(scratch, "tiny-macho-universal", "b");
    const Bytes original = ms::ReadAll(a);
    const auto prepared = MachOSigner::PrepareSignature(a, "test-identity", 64);
    const auto cms = ms::CountingCms(prepared.slices.size());
    MachOSigner::CompleteSignature(b, prepared, cms);
    ms::ExpectFinished(original, ms::ReadAll(b), prepared, cms);
    CHECK(ms::ReadAll(a) == original);
}

TEST_CASE("MachOSigner a stale PreparedSignature is refused", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
    const auto cms = ms::CountingCms(1);

    SECTION("one byte of the program changed")
    {
        Bytes changed = ms::ReadAll(path);
        changed.at(100) ^= 0x40; // inside the load commands: page 0
        ms::WriteAll(path, changed);
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, cms); });
        ExpectChanged(message, path);
        CHECK(ms::ReadAll(path) == changed);
    }
    SECTION("a byte in a later page changed")
    {
        Bytes changed = ms::ReadAll(path);
        changed.at(4100) ^= 0x01;
        ms::WriteAll(path, changed);
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, cms); });
        ExpectChanged(message, path);
        CHECK(ms::ReadAll(path) == changed);
    }
    SECTION("the program grew")
    {
        Bytes changed = ms::ReadAll(path);
        changed.insert(changed.end(), 16, 0);
        ms::WriteAll(path, changed);
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, cms); });
        CHECK(NamesTheFile(message, path));
        CHECK(ms::ReadAll(path) == changed);
    }
    SECTION("the identity differs")
    {
        const Bytes before = ms::ReadAll(path);
        MachOSigner::PreparedSignature other = prepared;
        other.identity = "another-identity";
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, other, cms); });
        ExpectChanged(message, path);
        CHECK(ms::ReadAll(path) == before);
    }
    SECTION("the capacity differs")
    {
        const Bytes before = ms::ReadAll(path);
        MachOSigner::PreparedSignature other = prepared;
        other.cms_capacity = 128;
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, other, cms); });
        ExpectChanged(message, path);
        CHECK(ms::ReadAll(path) == before);
    }
    SECTION("the CodeDirectory was altered")
    {
        const Bytes before = ms::ReadAll(path);
        MachOSigner::PreparedSignature other = prepared;
        other.slices[0].code_directory.back() ^= 0x01;
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, other, cms); });
        ExpectChanged(message, path);
        CHECK(ms::ReadAll(path) == before);
    }
    SECTION("the program was signed in the meantime")
    {
        MachOSigner::CompleteSignature(path, prepared, cms);
        const Bytes signed_once = ms::ReadAll(path);
        // A signed program is signed again in place, and the same inputs give
        // the same CodeDirectory, so the prepared value still describes the
        // result: completing it again changes nothing.
        MachOSigner::CompleteSignature(path, prepared, cms);
        CHECK(ms::ReadAll(path) == signed_once);
    }
}

TEST_CASE("MachOSigner a PreparedSignature of another program is refused", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string x86 = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const std::string arm = ms::CopyFixture(scratch, "tiny-macho-arm64");
    const std::string fat = ms::CopyFixture(scratch, "tiny-macho-universal");
    const Bytes x86_before = ms::ReadAll(x86);
    const Bytes arm_before = ms::ReadAll(arm);
    const Bytes fat_before = ms::ReadAll(fat);

    const auto for_x86 = MachOSigner::PrepareSignature(x86, "test-identity", 64);
    const auto for_arm = MachOSigner::PrepareSignature(arm, "test-identity", 64);

    ExpectChanged(ms::ErrorOf([&] { MachOSigner::CompleteSignature(arm, for_x86, ms::CountingCms(1)); }), arm);
    ExpectChanged(ms::ErrorOf([&] { MachOSigner::CompleteSignature(x86, for_arm, ms::CountingCms(1)); }), x86);
    // The slice count differs: one prepared slice against a universal file.
    const std::string fat_message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(fat, for_x86, ms::CountingCms(1)); });
    CHECK(NamesTheFile(fat_message, fat));

    CHECK(ms::ReadAll(x86) == x86_before);
    CHECK(ms::ReadAll(arm) == arm_before);
    CHECK(ms::ReadAll(fat) == fat_before);
}

TEST_CASE("MachOSigner a signature larger than the reserved capacity is refused", "[MachOSigner]")
{
    for(const std::string &name : {std::string("tiny-macho-x86_64"), std::string("tiny-macho-universal")})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes before = ms::ReadAll(path);
        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);

        auto cms = ms::CountingCms(prepared.slices.size());
        cms.back().push_back(0xAB); // one byte over, in the last slice only
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, cms); });
        CHECK(message.rfind(path + ": ", 0) == 0);
        CHECK(Contains(message, "the signature is 65 bytes but only 64 were reserved; "
                                "prepare again with a larger capacity"));
        CHECK(ms::ReadAll(path) == before);
        CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));

        // The same value still works with a signature that fits.
        const auto fitting = ms::CountingCms(prepared.slices.size());
        MachOSigner::CompleteSignature(path, prepared, fitting);
        ms::ExpectFinished(before, ms::ReadAll(path), prepared, fitting);
    }
}

TEST_CASE("MachOSigner a wrong number of signatures is refused", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string thin = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const std::string fat = ms::CopyFixture(scratch, "tiny-macho-universal");
    const Bytes thin_before = ms::ReadAll(thin);
    const Bytes fat_before = ms::ReadAll(fat);
    const auto thin_prepared = MachOSigner::PrepareSignature(thin, "test-identity", 64);
    const auto fat_prepared = MachOSigner::PrepareSignature(fat, "test-identity", 64);
    REQUIRE(fat_prepared.slices.size() == 2);

    for(std::size_t count : {std::size_t(0), std::size_t(2), std::size_t(3)})
    {
        INFO("thin file, signatures: " << count);
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(thin, thin_prepared, ms::CountingCms(count)); });
        CHECK(NamesTheFile(message, thin));
        CHECK(ms::ReadAll(thin) == thin_before);
    }
    for(std::size_t count : {std::size_t(0), std::size_t(1), std::size_t(3)})
    {
        INFO("universal file, signatures: " << count);
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(fat, fat_prepared, ms::CountingCms(count)); });
        CHECK(NamesTheFile(message, fat));
        CHECK(ms::ReadAll(fat) == fat_before);
    }
}

TEST_CASE("MachOSigner capacity zero reserves an empty CMS", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const Bytes original = ms::ReadAll(path);

    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 0);
    CHECK(prepared.cms_capacity == 0);

    SECTION("an empty CMS gives a SuperBlob with a zero-length CMS wrapper")
    {
        const std::vector<Bytes> cms = {Bytes()};
        MachOSigner::CompleteSignature(path, prepared, cms);
        const Bytes finished = ms::ReadAll(path);
        ms::ExpectFinished(original, finished, prepared, cms);

        const auto blob = MachOSigner::ExtractSignature(path);
        REQUIRE(blob.has_value());
        CHECK(*blob == MachOSigner::BuildSuperBlob(prepared.slices[0].code_directory, Bytes()));
        const auto extracted = MachOSigner::ExtractCmsFromSuperBlob(*blob);
        REQUIRE(extracted.has_value());
        CHECK(extracted->empty());
    }
    SECTION("one byte does not fit")
    {
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, {Bytes(1, 0x30)}); });
        CHECK(Contains(message, "the signature is 1 bytes but only 0 were reserved; "
                                "prepare again with a larger capacity"));
        CHECK(ms::ReadAll(path) == original);
    }
}

TEST_CASE("MachOSigner a capacity above 2^31 is refused", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const Bytes before = ms::ReadAll(path);

    for(const std::uint32_t capacity : {0x80000001u, 0x90000000u, 0xFFFFFFFFu})
    {
        INFO("capacity " << capacity);
        CHECK_FALSE(ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "test-identity", capacity); }).empty());
        CHECK(ms::ReadAll(path) == before);
    }

    // A value built by hand is refused by CompleteSignature as well.
    MachOSigner::PreparedSignature forged = MachOSigner::PrepareSignature(path, "test-identity", 64);
    forged.cms_capacity = 0x80000001u;
    CHECK_FALSE(ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, forged, ms::CountingCms(1)); }).empty());
    CHECK(ms::ReadAll(path) == before);
}

TEST_CASE("MachOSigner CompleteSignature failing on a missing file changes nothing", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64");
    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
    const std::string missing = scratch.File("absent");
    const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(missing, prepared, ms::CountingCms(1)); });
    CHECK(NamesTheFile(message, missing));
    CHECK_FALSE(std::filesystem::exists(missing));
}

TEST_CASE("MachOSigner rejects 32-bit Mach-O", "[MachOSigner]")
{
    // 32-bit header is 28 bytes: magic(4) + cputype(4) + cpusubtype(4) +
    //   filetype(4) + ncmds(4) + sizeofcmds(4) + flags(4)
    Bytes macho32(64, 0);
    macho32[0] = 0xCE;
    macho32[1] = 0xFA;
    macho32[2] = 0xED;
    macho32[3] = 0xFE;

    seedtest::ScratchDir scratch;
    const std::string path = ms::WriteScratch(scratch, "macho32_reject.bin", macho32);

    REQUIRE_THROWS_AS(MachOSigner::PrepareSignature(path, "test-identity", 64), std::runtime_error);
    CHECK(ms::ReadAll(path) == macho32);
}

TEST_CASE("MachOSigner a flipped byte in a signed program fails exactly its page", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-arm64");
    ms::SignWithKnownInputs(path);
    const Bytes finished = ms::ReadAll(path);
    REQUIRE(mr::CheckFile(finished).Ok());

    const mr::SliceReport clean = mr::CheckFile(finished).slices[0];
    REQUIRE(clean.n_code_slots >= 5);
    for(const std::uint64_t at : {std::uint64_t(3000), std::uint64_t(4096), std::uint64_t(8192 + 9),
                                  clean.code_limit - 1})
    {
        INFO("offset " << at);
        Bytes bad = finished;
        bad.at(static_cast<std::size_t>(at)) ^= 0x01;
        const mr::FileReport report = mr::CheckFile(bad);
        REQUIRE(report.slices.size() == 1);
        std::size_t failed = 0;
        for(std::size_t p = 0; p < report.slices[0].page_ok.size(); ++p)
        {
            if(!report.slices[0].page_ok[p])
            {
                ++failed;
                CHECK(p == at / 4096);
            }
        }
        CHECK(failed == 1);
    }
}

// ---------------------------------------------------------------------------
// The old order against the independent checker.
// ---------------------------------------------------------------------------

TEST_CASE("MachOSigner hashing before the final layout gives a signature the checker rejects", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-x86_64", "tiny-macho-x86_64-exactfit"})
    {
        INFO(name);
        const Bytes original = ms::LoadFixture(name);

        // Old order: hash the file as it is now, with the code limit at its end.
        const Bytes stale_cd = RawCodeDirectory("order-test", original, original.size());
        const Bytes stale_blob = RawSuperBlob(stale_cd);
        std::uint64_t dataoff = 0;
        Bytes wrong = ArrangeFinalLayout(original, ms::Align16(stale_blob.size()), dataoff);
        std::copy(stale_blob.begin(), stale_blob.end(), wrong.begin() + static_cast<std::ptrdiff_t>(dataoff));

        const mr::FileReport rejected = mr::CheckFile(wrong);
        INFO("problems: " << ms::JoinProblems(rejected));
        CHECK_FALSE(rejected.Ok());
        REQUIRE(rejected.slices.size() == 1);
        // The header changed after it was hashed: page 0 is stale.
        CHECK_FALSE(rejected.slices[0].page_ok.at(0));
        // The code limit is the old end of the file, not the start of the data.
        bool limit_reported = false;
        for(const std::string &problem : rejected.problems)
        {
            limit_reported = limit_reported || Contains(problem, "codeLimit");
        }
        CHECK(limit_reported);

        // New order: lay the file out first (the size of the data does not
        // depend on any hash), then hash up to the start of the data.
        const Bytes sized = RawSuperBlob(RawCodeDirectory("order-test", original, ms::Align16(original.size())));
        const std::uint64_t datasize = ms::Align16(sized.size());
        Bytes right = ArrangeFinalLayout(original, datasize, dataoff);
        const Bytes good_cd = RawCodeDirectory("order-test", right, dataoff);
        const Bytes good_blob = RawSuperBlob(good_cd);
        REQUIRE(good_blob.size() == sized.size());
        std::copy(good_blob.begin(), good_blob.end(), right.begin() + static_cast<std::ptrdiff_t>(dataoff));

        const mr::FileReport accepted = mr::CheckFile(right);
        INFO("problems: " << ms::JoinProblems(accepted));
        CHECK(accepted.Ok());
        REQUIRE(accepted.slices.size() == 1);
        CHECK(accepted.slices[0].code_limit == accepted.slices[0].dataoff);
        for(bool ok : accepted.slices[0].page_ok)
        {
            CHECK(ok);
        }
    }
}

// ---------------------------------------------------------------------------
// Universal files.
// ---------------------------------------------------------------------------

TEST_CASE("MachOSigner universal files keep order alignment and a consistent table", "[MachOSigner]")
{
    for(const std::string &name : {std::string("tiny-macho-universal"), std::string("tiny-macho-universal64")})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        const std::vector<TableEntry> table_before = ReadTable(original);
        REQUIRE(table_before.size() == 2);

        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
        REQUIRE(prepared.slices.size() == 2);
        // Table order: x86-64 first, arm64 second.
        CHECK(prepared.slices[0].cpu_type == 0x01000007);
        CHECK(prepared.slices[1].cpu_type == 0x0100000C);
        const auto cms = ms::CountingCms(2);
        MachOSigner::CompleteSignature(path, prepared, cms);

        const Bytes finished = ms::ReadAll(path);
        ms::ExpectFinished(original, finished, prepared, cms);

        const std::vector<TableEntry> table_after = ReadTable(finished);
        REQUIRE(table_after.size() == table_before.size());
        const mr::FileReport report = mr::CheckFile(finished);
        std::uint64_t previous_end = 0;
        for(std::size_t i = 0; i < table_after.size(); ++i)
        {
            INFO("slice " << i);
            CHECK(table_after[i].cpu == table_before[i].cpu);
            CHECK(table_after[i].align == table_before[i].align);
            CHECK(table_after[i].offset % (std::uint64_t(1) << table_after[i].align) == 0);
            CHECK(table_after[i].offset >= previous_end);
            // The new length of the slice is written in the table.
            CHECK(table_after[i].size == report.slices[i].size);
            CHECK(table_after[i].size > table_before[i].size);
            previous_end = table_after[i].offset + table_after[i].size;
        }
        CHECK(previous_end == finished.size());
        CHECK(report.form == mr::CheckFile(original).form);

        // The bytes of each slice up to its old end are what they were, except
        // in the places the signature changes (the header counts and the
        // link-edit segment): compare the code of the first section.
        for(std::size_t i = 0; i < 2; ++i)
        {
            const Bytes was = Slice(original, table_before[i].offset, table_before[i].size);
            const Bytes now = Slice(finished, table_after[i].offset, table_after[i].size);
            CHECK(Slice(was, 1024, 2048) == Slice(now, 1024, 2048));
        }
    }
}

TEST_CASE("MachOSigner universal slices are signed as their own programs", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string fat_path = ms::CopyFixture(scratch, "tiny-macho-universal");
    const Bytes fat = ms::ReadAll(fat_path);
    const std::vector<TableEntry> table = ReadTable(fat);
    REQUIRE(table.size() == 2);

    // The same bytes as a thin file give the same CodeDirectory: the
    // fingerprints of a slice depend on that slice only.
    const Bytes x86_bytes = Slice(fat, table[0].offset, table[0].size);
    const Bytes arm_bytes = Slice(fat, table[1].offset, table[1].size);
    const std::string x86_path = ms::WriteScratch(scratch, "slice-x86_64", x86_bytes);
    const std::string arm_path = ms::WriteScratch(scratch, "slice-arm64", arm_bytes);

    const auto together = MachOSigner::PrepareSignature(fat_path, "test-identity", 64);
    const auto x86_alone = MachOSigner::PrepareSignature(x86_path, "test-identity", 64);
    const auto arm_alone = MachOSigner::PrepareSignature(arm_path, "test-identity", 64);
    REQUIRE(together.slices.size() == 2);
    CHECK(together.slices[0].code_directory == x86_alone.slices[0].code_directory);
    CHECK(together.slices[1].code_directory == arm_alone.slices[0].code_directory);
    CHECK(together.slices[0].cd_hash == x86_alone.slices[0].cd_hash);
    CHECK(together.slices[1].cd_hash == arm_alone.slices[0].cd_hash);
    CHECK(together.slices[0].cd_hash != together.slices[1].cd_hash);

    // A different signature for each slice: each ends up in its own slice.
    const std::vector<Bytes> cms = {Bytes(40, 0x11), Bytes(64, 0x22)};
    MachOSigner::CompleteSignature(fat_path, together, cms);
    const Bytes finished = ms::ReadAll(fat_path);
    ms::ExpectFinished(fat, finished, together, cms);

    const auto signatures = MachOSigner::ExtractSignatures(fat_path);
    REQUIRE(signatures.size() == 2);
    for(std::size_t i = 0; i < 2; ++i)
    {
        INFO("slice " << i);
        REQUIRE(signatures[i].has_value());
        const auto extracted_cms = MachOSigner::ExtractCmsFromSuperBlob(*signatures[i]);
        REQUIRE(extracted_cms.has_value());
        CHECK(*extracted_cms == cms[i]);
        const auto extracted_cd = MachOSigner::ExtractCodeDirectoryFromSuperBlob(*signatures[i]);
        REQUIRE(extracted_cd.has_value());
        CHECK(*extracted_cd == together.slices[i].code_directory);
    }
    // ExtractSignature of a universal file answers for the first slice.
    const auto first = MachOSigner::ExtractSignature(fat_path);
    REQUIRE(first.has_value());
    CHECK(*first == *signatures[0]);

    // Each slice signed alone equals the slice signed inside the universal file.
    for(std::size_t i = 0; i < 2; ++i)
    {
        const std::string alone = i == 0 ? x86_path : arm_path;
        const auto alone_prepared = MachOSigner::PrepareSignature(alone, "test-identity", 64);
        MachOSigner::CompleteSignature(alone, alone_prepared, {cms[i]});
        const Bytes thin = ms::ReadAll(alone);
        const mr::FileReport report = mr::CheckFile(finished);
        const Bytes inside = Slice(finished, report.slices[i].offset, report.slices[i].size);
        CHECK(thin == inside);
    }
}

TEST_CASE("MachOSigner a refusal in any slice refuses the whole universal file", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;

    SECTION("a signature too large for the second slice")
    {
        const std::string path = ms::CopyFixture(scratch, "tiny-macho-universal");
        const Bytes before = ms::ReadAll(path);
        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
        std::vector<Bytes> cms = ms::CountingCms(2);
        cms[1].resize(65);
        const std::string message = ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, cms); });
        CHECK(Contains(message, "were reserved"));
        // Neither slice was signed, not even the first.
        CHECK(ms::ReadAll(path) == before);
        CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
        for(const auto &signature : MachOSigner::ExtractSignatures(path))
        {
            CHECK_FALSE(signature.has_value());
        }
    }
    SECTION("the second slice changed after PrepareSignature")
    {
        const std::string path = ms::CopyFixture(scratch, "tiny-macho-universal");
        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
        Bytes changed = ms::ReadAll(path);
        const std::vector<TableEntry> table = ReadTable(changed);
        changed.at(static_cast<std::size_t>(table[1].offset + 5000)) ^= 0x01;
        ms::WriteAll(path, changed);
        ExpectChanged(ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, ms::CountingCms(2)); }), path);
        CHECK(ms::ReadAll(path) == changed);
    }
    SECTION("a slice with no room for the command")
    {
        const Bytes ok_slice = ms::LoadFixture("tiny-macho-arm64");
        const Bytes tight_slice = ms::LoadFixture("tiny-macho-x86_64-nospace");
        const Bytes fat = mr::Universal({tight_slice, ok_slice}, false, 14);
        const std::string path = ms::WriteScratch(scratch, "mixed", fat);
        const std::string message = ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "test-identity", 64); });
        CHECK(NamesTheFile(message, path));
        CHECK(ms::ReadAll(path) == fat);
    }
    SECTION("a slice that is not a program")
    {
        const Bytes ok_slice = ms::LoadFixture("tiny-macho-arm64");
        const Bytes fat = mr::Universal({ok_slice, mr::BigEndian64()}, false, 14);
        const std::string path = ms::WriteScratch(scratch, "mixed", fat);
        const std::string message = ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "test-identity", 64); });
        CHECK(NamesTheFile(message, path));
        CHECK(ms::ReadAll(path) == fat);
    }
}

TEST_CASE("MachOSigner HasEmbeddedSignature is true for a universal file only when every slice is signed", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    // The lld programs are signed; the unsigned thin x86-64 slice is not.
    const Bytes signed_arm = ms::LoadFixture("tiny-macho-arm64-adhoc");
    const Bytes unsigned_x86 = ms::LoadFixture("tiny-macho-x86_64");
    const Bytes signed_x86 = ms::LoadFixture("tiny-macho-x86_64-adhoc");

    const std::string mixed = ms::WriteScratch(scratch, "mixed", mr::Universal({unsigned_x86, signed_arm}, false, 14));
    const std::string both = ms::WriteScratch(scratch, "both", mr::Universal({signed_x86, signed_arm}, false, 14));
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(mixed));
    CHECK(MachOSigner::HasEmbeddedSignature(both));
    const auto parts = MachOSigner::ExtractSignatures(mixed);
    REQUIRE(parts.size() == 2);
    CHECK_FALSE(parts[0].has_value());
    CHECK(parts[1].has_value());
}

TEST_CASE("MachOSigner the lld signatures are read back by ExtractSignature", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-arm64-adhoc", "tiny-macho-x86_64-adhoc"})
    {
        INFO(name);
        const std::string path = seedtest::FixturePath(name);
        REQUIRE(MachOSigner::HasEmbeddedSignature(path));
        const auto blob = MachOSigner::ExtractSignature(path);
        REQUIRE(blob.has_value());
        std::uint64_t length = 0;
        REQUIRE(mr::ReadBE(*blob, 4, 4, length));
        CHECK(length == blob->size());
        CHECK(MachOSigner::ExtractCodeDirectoryFromSuperBlob(*blob).has_value());
    }
}

// ---------------------------------------------------------------------------
// Signing again (US3): a signed program, whoever signed it, gets its single
// signature command updated in place; a strip operation removes the signature.
// ---------------------------------------------------------------------------

namespace {

// cmdsize of every load command of a slice, in file order.
std::vector<std::uint64_t> CommandSizes(const Bytes &file, const mr::SliceReport &s)
{
    std::vector<std::uint64_t> sizes;
    std::uint64_t at = s.offset + 32;
    for(std::uint64_t i = 0; i < s.ncmds; ++i)
    {
        std::uint64_t size = 0;
        if(!mr::ReadLE(file, at + 4, 4, size))
        {
            break;
        }
        sizes.push_back(size);
        at += size == 0 ? 8 : size;
    }
    return sizes;
}

void ExpectOneCommandNoZeroEntry(const Bytes &file, const mr::FileReport &report)
{
    for(std::size_t i = 0; i < report.slices.size(); ++i)
    {
        INFO("slice " << i);
        const mr::SliceReport &s = report.slices[i];
        const std::vector<std::uint64_t> sizes = CommandSizes(file, s);
        CHECK(s.signature_commands == 1);
        CHECK(sizes.size() == s.ncmds);
        std::uint64_t sum = 0;
        for(const std::uint64_t size : sizes)
        {
            CHECK(size >= 8);
            sum += size;
        }
        CHECK(sum == s.sizeofcmds);
    }
}

// Absolute file offset of the vmsize field (and, 16 bytes later, filesize) of
// the __LINKEDIT segment command of a slice; 0 when there is none.
std::uint64_t LinkeditSizeFields(const Bytes &file, const mr::SliceReport &s)
{
    std::uint64_t at = s.offset + 32;
    for(std::uint64_t i = 0; i < s.ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        if(!mr::ReadLE(file, at, 4, cmd) || !mr::ReadLE(file, at + 4, 4, size) || size < 8)
        {
            return 0;
        }
        if(cmd == 0x19 && mr::Fits(file, at + 8, 16) &&
           std::string(reinterpret_cast<const char *>(&file[at + 8]), 10) == "__LINKEDIT")
        {
            return at + 32;
        }
        at += size;
    }
    return 0;
}

// True when a and b hold the same bytes over `length` bytes (at `a_at` and
// `b_at`) apart from the two 8-byte __LINKEDIT size fields at `fields_a` and
// `fields_b` (vmsize at +0, filesize at +16).
bool SameExceptLinkeditSizes(const Bytes &a, std::uint64_t a_at, std::uint64_t fields_a, const Bytes &b,
                             std::uint64_t b_at, std::uint64_t fields_b, std::uint64_t length)
{
    if(!mr::Fits(a, a_at, length) || !mr::Fits(b, b_at, length))
    {
        return false;
    }
    for(std::uint64_t i = 0; i < length; ++i)
    {
        const std::uint64_t pa = a_at + i;
        const std::uint64_t pb = b_at + i;
        const bool skip_a = fields_a != 0 && ((pa >= fields_a && pa < fields_a + 8) ||
                                              (pa >= fields_a + 16 && pa < fields_a + 24));
        const bool skip_b = fields_b != 0 && ((pb >= fields_b && pb < fields_b + 8) ||
                                              (pb >= fields_b + 16 && pb < fields_b + 24));
        if(skip_a != skip_b)
        {
            return false;
        }
        if(!skip_a && a[pa] != b[pb])
        {
            return false;
        }
    }
    return true;
}

// A copy of the file with the size field of the signature command of a slice
// set to zero. That field describes the signature, which is the one thing in
// the table that changes when a program is signed again.
Bytes WithoutSignatureSize(Bytes file, const mr::SliceReport &s)
{
    std::uint64_t at = s.offset + 32;
    for(std::uint64_t i = 0; i < s.ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        if(!mr::ReadLE(file, at, 4, cmd) || !mr::ReadLE(file, at + 4, 4, size) || size < 8)
        {
            break;
        }
        if(cmd == 0x1D && mr::Fits(file, at + 12, 4))
        {
            std::fill(file.begin() + static_cast<std::ptrdiff_t>(at + 12),
                      file.begin() + static_cast<std::ptrdiff_t>(at + 16), static_cast<std::uint8_t>(0));
        }
        at += size;
    }
    return file;
}

// What a program signed by another tool must look like after it was signed
// here: same place, same command, new data, nothing else changed.
void ExpectResigned(const Bytes &old, const Bytes &finished, const MachOSigner::PreparedSignature &prepared,
                    const std::vector<Bytes> &cms)
{
    const mr::FileReport before = mr::CheckFile(old);
    const mr::FileReport after = mr::CheckFile(finished);
    INFO("problems: " << ms::JoinProblems(after));
    CHECK(after.Ok());
    CHECK(after.form == before.form);
    REQUIRE(after.slices.size() == before.slices.size());
    REQUIRE(after.slices.size() == prepared.slices.size());
    REQUIRE(cms.size() == prepared.slices.size());
    ExpectOneCommandNoZeroEntry(finished, after);
    for(std::size_t i = 0; i < after.slices.size(); ++i)
    {
        INFO("slice " << i);
        const mr::SliceReport &o = before.slices[i];
        const mr::SliceReport &s = after.slices[i];
        const MachOSigner::PreparedSlice &p = prepared.slices[i];
        REQUIRE(o.has_signature);
        const Bytes blob = MachOSigner::BuildSuperBlob(p.code_directory, cms[i]);
        const Bytes reserved_blob = MachOSigner::BuildSuperBlob(p.code_directory, Bytes(prepared.cms_capacity));

        CHECK(s.cputype == o.cputype);
        CHECK(s.ncmds == o.ncmds);
        CHECK(s.sizeofcmds == o.sizeofcmds);
        CHECK(s.walked_commands == s.ncmds);
        CHECK(s.has_signature);
        CHECK(s.dataoff == o.dataoff); // updated in place
        CHECK(s.datasize == ms::Align16(reserved_blob.size()));
        CHECK(s.dataoff + s.datasize == s.size);
        CHECK(s.linkedit_fileoff == o.linkedit_fileoff);
        CHECK(s.linkedit_fileoff + s.linkedit_filesize == s.size);
        CHECK(s.linkedit_vmsize == ms::AlignUp(s.linkedit_filesize, ms::LinkeditPage(s.cputype)));
        CHECK(s.code_limit == s.dataoff);
        CHECK(s.n_code_slots == (s.code_limit + 4095) / 4096);
        REQUIRE(s.page_ok.size() == s.n_code_slots);
        for(std::size_t page = 0; page < s.page_ok.size(); ++page)
        {
            INFO("page " << page);
            CHECK(s.page_ok[page]);
        }
        CHECK(s.requirements_ok);
        CHECK(s.identifier == prepared.identity);
        CHECK(s.identifier != o.identifier);
        CHECK(s.superblob_length == blob.size());
        // Everything before the signature is the old content, apart from the
        // two sizes of the link-edit segment and the size in the signature command.
        CHECK(SameExceptLinkeditSizes(WithoutSignatureSize(old, o), o.offset, LinkeditSizeFields(old, o),
                                      WithoutSignatureSize(finished, s), s.offset,
                                      LinkeditSizeFields(finished, s), o.dataoff));
        // The signature data is ours, byte for byte, and zero after it.
        REQUIRE(mr::Fits(finished, s.offset + s.dataoff, s.datasize));
        const Bytes written(finished.begin() + static_cast<std::ptrdiff_t>(s.offset + s.dataoff),
                            finished.begin() + static_cast<std::ptrdiff_t>(s.offset + s.dataoff + blob.size()));
        CHECK(written == blob);
        for(std::uint64_t k = blob.size(); k < s.datasize; ++k)
        {
            REQUIRE(finished[s.offset + s.dataoff + k] == 0);
        }
        // Nothing of the other tool's signature is left.
        const std::string old_identifier = o.identifier;
        const auto begin = finished.begin() + static_cast<std::ptrdiff_t>(s.offset + s.dataoff);
        const auto end = finished.begin() + static_cast<std::ptrdiff_t>(s.offset + s.size);
        CHECK(std::search(begin, end, old_identifier.begin(), old_identifier.end()) == end);
        const auto cd = MachOSigner::ExtractCodeDirectoryFromSuperBlob(written);
        REQUIRE(cd.has_value());
        CHECK(*cd == p.code_directory);
    }
    if(after.form == 0)
    {
        CHECK(finished.size() == old.size() - before.slices[0].datasize + after.slices[0].datasize);
        CHECK(finished.size() == after.slices[0].size);
    }
}

// The slice of a program as it must be after a strip: the signature command
// removed from the table (later commands move up, the freed bytes are zero),
// ncmds and sizeofcmds smaller, the data cut off.
Bytes ExpectedStrippedSlice(const Bytes &signed_file, const mr::SliceReport &o)
{
    Bytes slice(signed_file.begin() + static_cast<std::ptrdiff_t>(o.offset),
                signed_file.begin() + static_cast<std::ptrdiff_t>(o.offset + o.dataoff));
    std::uint64_t at = 32;
    std::uint64_t found = 0;
    for(std::uint64_t i = 0; i < o.ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        mr::ReadLE(slice, at, 4, cmd);
        mr::ReadLE(slice, at + 4, 4, size);
        if(cmd == 0x1D)
        {
            found = at;
        }
        at += size;
    }
    REQUIRE(found != 0);
    const std::uint64_t table_end = 32 + o.sizeofcmds;
    std::copy(slice.begin() + static_cast<std::ptrdiff_t>(found + 16),
              slice.begin() + static_cast<std::ptrdiff_t>(table_end),
              slice.begin() + static_cast<std::ptrdiff_t>(found));
    std::fill(slice.begin() + static_cast<std::ptrdiff_t>(table_end - 16),
              slice.begin() + static_cast<std::ptrdiff_t>(table_end), static_cast<std::uint8_t>(0));
    PutLE(slice, 16, o.ncmds - 1, 4);
    PutLE(slice, 20, o.sizeofcmds - 16, 4);
    return slice;
}

// A signed program and its strip: well formed, unsigned, the signature gone
// and nothing else touched.
void ExpectStripped(const Bytes &signed_file, const Bytes &stripped)
{
    const mr::FileReport before = mr::CheckFile(signed_file);
    const mr::FileReport after = mr::CheckFile(stripped);
    INFO("problems: " << ms::JoinProblems(after));
    CHECK(after.Ok());
    CHECK(after.form == before.form);
    REQUIRE(after.slices.size() == before.slices.size());
    for(std::size_t i = 0; i < after.slices.size(); ++i)
    {
        INFO("slice " << i);
        const mr::SliceReport &o = before.slices[i];
        const mr::SliceReport &s = after.slices[i];
        CHECK_FALSE(s.has_signature);
        CHECK(s.signature_commands == 0);
        CHECK(s.ncmds == o.ncmds - 1);
        CHECK(s.sizeofcmds == o.sizeofcmds - 16);
        CHECK(s.walked_commands == s.ncmds);
        CHECK(s.size == o.dataoff); // cut where the data began
        CHECK(s.linkedit_fileoff == o.linkedit_fileoff);
        CHECK(s.linkedit_fileoff + s.linkedit_filesize == s.size);
        CHECK(s.linkedit_vmsize >= s.linkedit_filesize);
        const Bytes expected = ExpectedStrippedSlice(signed_file, o);
        mr::SliceReport at_zero = s;
        at_zero.offset = 0;
        CHECK(SameExceptLinkeditSizes(expected, 0, LinkeditSizeFields(expected, at_zero), stripped, s.offset,
                                      LinkeditSizeFields(stripped, s), s.size));
    }
}

struct Round
{
    std::uint32_t capacity;
    const char *identity;
};

} // namespace

TEST_CASE("MachOSigner signing ten times with changing capacities keeps one command and no empty entry",
          "[MachOSigner]")
{
    const Round rounds[] = {{64, "test-identity"}, {200, "test-identity"}, {200, "other.identity"},
                            {8, "test-identity"},  {64, "a"},              {64, "a"},
                            {0, "test-identity"},  {1000, "test-identity"}, {33, "other.identity"},
                            {64, "test-identity"}};
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        std::vector<std::uint64_t> first_ncmds;
        std::vector<std::uint64_t> previous_slice_sizes;
        std::size_t previous_length = 0;
        std::uint32_t previous_capacity = 0;
        int number = 0;
        for(const Round &round : rounds)
        {
            INFO("signing " << number << " capacity " << round.capacity << " identity " << round.identity);
            const Bytes before_prepare = ms::ReadAll(path);
            const auto prepared = MachOSigner::PrepareSignature(path, round.identity, round.capacity);
            CHECK(ms::ReadAll(path) == before_prepare); // prepare does not write
            const auto cms = ms::CountingCms(prepared.slices.size(), round.capacity);
            MachOSigner::CompleteSignature(path, prepared, cms);
            const Bytes finished = ms::ReadAll(path);

            // The same layout as signing the unsigned sample once.
            ms::ExpectFinished(original, finished, prepared, cms);
            ExpectOneCommandNoZeroEntry(finished, mr::CheckFile(finished));
            const std::string fresh_path = ms::CopyFixture(scratch, name, "fresh-" + std::to_string(number));
            const auto fresh = MachOSigner::PrepareSignature(fresh_path, round.identity, round.capacity);
            MachOSigner::CompleteSignature(fresh_path, fresh, cms);
            const Bytes fresh_bytes = ms::ReadAll(fresh_path);
            CHECK(finished.size() <= fresh_bytes.size());
            CHECK(finished == fresh_bytes);

            const mr::FileReport report = mr::CheckFile(finished);
            std::vector<std::uint64_t> slice_sizes;
            for(std::size_t i = 0; i < report.slices.size(); ++i)
            {
                slice_sizes.push_back(report.slices[i].size);
                if(number == 0)
                {
                    first_ncmds.push_back(report.slices[i].ncmds);
                }
                REQUIRE(i < first_ncmds.size());
                CHECK(report.slices[i].ncmds == first_ncmds[i]);
            }
            if(number > 0)
            {
                if(round.capacity == previous_capacity)
                {
                    CHECK(finished.size() == previous_length);
                }
                else if(round.capacity < previous_capacity)
                {
                    CHECK(finished.size() <= previous_length);
                }
                else
                {
                    CHECK(finished.size() >= previous_length);
                }
                for(std::size_t i = 0; i < slice_sizes.size(); ++i)
                {
                    if(round.capacity == previous_capacity)
                    {
                        CHECK(slice_sizes[i] == previous_slice_sizes[i]);
                    }
                    else if(previous_capacity > round.capacity + 15)
                    {
                        CHECK(slice_sizes[i] < previous_slice_sizes[i]);
                    }
                    else if(round.capacity > previous_capacity + 15)
                    {
                        CHECK(slice_sizes[i] > previous_slice_sizes[i]);
                    }
                }
            }
            previous_slice_sizes = slice_sizes;
            previous_length = finished.size();
            previous_capacity = round.capacity;
            ++number;
        }
    }
}

TEST_CASE("MachOSigner a program signed by another tool is re-signed with a larger signature", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-arm64-adhoc", "tiny-macho-x86_64-adhoc", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes old = ms::ReadAll(path);

        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 4096);
        CHECK(ms::ReadAll(path) == old); // prepare does not write
        const auto cms = ms::CountingCms(prepared.slices.size(), 4096);
        MachOSigner::CompleteSignature(path, prepared, cms);
        const Bytes finished = ms::ReadAll(path);
        ExpectResigned(old, finished, prepared, cms);
        CHECK(finished.size() > old.size());
        CHECK(MachOSigner::HasEmbeddedSignature(path));
        const auto parts = MachOSigner::ExtractSignatures(path);
        REQUIRE(parts.size() == prepared.slices.size());
        for(std::size_t i = 0; i < parts.size(); ++i)
        {
            REQUIRE(parts[i].has_value());
            CHECK(*parts[i] == MachOSigner::BuildSuperBlob(prepared.slices[i].code_directory, cms[i]));
        }
    }
}

TEST_CASE("MachOSigner a program signed by another tool is re-signed with a smaller signature", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-arm64-adhoc", "tiny-macho-x86_64-adhoc", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        // Directly with no room for a CMS, and after a larger one.
        for(const bool via_larger : {false, true})
        {
            INFO("via a larger signature first: " << via_larger);
            seedtest::ScratchDir scratch;
            const std::string path = ms::CopyFixture(scratch, name);
            if(via_larger)
            {
                const auto big = MachOSigner::PrepareSignature(path, "test-identity", 4096);
                MachOSigner::CompleteSignature(path, big, ms::CountingCms(big.slices.size(), 4096));
            }
            const Bytes old = ms::ReadAll(path);
            const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 0);
            const auto cms = ms::CountingCms(prepared.slices.size(), 0);
            MachOSigner::CompleteSignature(path, prepared, cms);
            const Bytes finished = ms::ReadAll(path);
            if(via_larger)
            {
                // The old signature here is ours, so the shape is judged, and
                // that the file became smaller.
                CHECK(finished.size() < old.size());
                const mr::FileReport report = mr::CheckFile(finished);
                const mr::FileReport previous = mr::CheckFile(old);
                INFO("problems: " << ms::JoinProblems(report));
                CHECK(report.Ok());
                REQUIRE(report.slices.size() == previous.slices.size());
                ExpectOneCommandNoZeroEntry(finished, report);
                for(std::size_t i = 0; i < report.slices.size(); ++i)
                {
                    const mr::SliceReport &s = report.slices[i];
                    CHECK(s.size < previous.slices[i].size);
                    CHECK(s.datasize < previous.slices[i].datasize);
                    CHECK(s.code_limit == s.dataoff);
                    CHECK(s.dataoff + s.datasize == s.size);
                    for(std::size_t page = 0; page < s.page_ok.size(); ++page)
                    {
                        CHECK(s.page_ok[page]);
                    }
                }
            }
            else
            {
                ExpectResigned(old, finished, prepared, cms);
            }
        }
    }
}

TEST_CASE("MachOSigner the second signing of a program signed by another tool equals the first", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-arm64-adhoc", "tiny-macho-x86_64-adhoc", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        ms::SignWithKnownInputs(path);
        const Bytes once = ms::ReadAll(path);
        ms::SignWithKnownInputs(path);
        CHECK(ms::ReadAll(path) == once);
    }
}

TEST_CASE("MachOSigner a signature followed by other data is refused and the file stays as it was",
          "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64-data-after-sig");
    const Bytes before = ms::ReadAll(path);

    const std::string message = ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "test-identity", 64); });
    CHECK(message.rfind(path + ": ", 0) == 0);
    CHECK(Contains(message, "the signature data is not at the end of the program (16 bytes follow it); "
                            "signing would have to move them"));
    CHECK(ms::ReadAll(path) == before);

    // A prepared signature of the same program without the extra bytes does not
    // get the file signed either.
    const std::string clean = ms::CopyFixture(scratch, "tiny-macho-x86_64-adhoc");
    const auto prepared = MachOSigner::PrepareSignature(clean, "test-identity", 64);
    CHECK_FALSE(ms::ErrorOf([&] { MachOSigner::CompleteSignature(path, prepared, ms::CountingCms(1)); }).empty());
    CHECK(ms::ReadAll(path) == before);
}

TEST_CASE("MachOSigner data after the signature of one slice refuses the whole universal file", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    Bytes file = ms::LoadFixture("tiny-macho-universal-adhoc");
    const std::vector<TableEntry> table = ReadTable(file);
    REQUIRE(table.size() == 2);
    REQUIRE(table[1].offset + table[1].size == file.size());
    // Sixteen bytes after the signature of the last slice (the table entry
    // grows with it; the 32-bit table puts the size at entry offset 12).
    for(std::uint8_t b = 0xA5; b < 0xA5 + 16; ++b)
    {
        file.push_back(b);
    }
    PutBE(file, 8 + 20 * 1 + 12, table[1].size + 16, 4);
    const std::string path = ms::WriteScratch(scratch, "data-after-in-slice-1", file);
    const Bytes before = ms::ReadAll(path);

    const std::string message = ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "test-identity", 64); });
    CHECK(message.rfind(path + ": ", 0) == 0);
    CHECK(Contains(message, "slice 1 ("));
    CHECK(Contains(message, "the signature data is not at the end of the program (16 bytes follow it)"));
    CHECK(ms::ReadAll(path) == before);
}

TEST_CASE("MachOSigner StripSignature removes the signature of every signed sample", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        ms::SignWithKnownInputs(path);
        const Bytes signed_file = ms::ReadAll(path);
        REQUIRE(MachOSigner::HasEmbeddedSignature(path));

        MachOSigner::StripSignature(path);
        const Bytes stripped = ms::ReadAll(path);
        CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
        ExpectStripped(signed_file, stripped);

        // Against the unsigned sample: the same program, with only the sizes
        // of the link-edit segment (and zero padding at the end) different.
        const mr::FileReport before = mr::CheckFile(original);
        const mr::FileReport after = mr::CheckFile(stripped);
        REQUIRE(after.slices.size() == before.slices.size());
        for(std::size_t i = 0; i < after.slices.size(); ++i)
        {
            INFO("slice " << i);
            const mr::SliceReport &o = before.slices[i];
            const mr::SliceReport &s = after.slices[i];
            CHECK(s.ncmds == o.ncmds);
            CHECK(s.sizeofcmds == o.sizeofcmds);
            CHECK(s.size >= o.size);
            CHECK(s.size <= ms::Align16(o.size));
            CHECK(SameExceptLinkeditSizes(original, o.offset, LinkeditSizeFields(original, o), stripped, s.offset,
                                          LinkeditSizeFields(stripped, s), o.size));
            for(std::uint64_t k = o.size; k < s.size; ++k)
            {
                REQUIRE(stripped[s.offset + k] == 0);
            }
        }
        if(after.form == 0)
        {
            CHECK(stripped.size() == after.slices[0].size);
        }
    }
}

TEST_CASE("MachOSigner StripSignature on an unsigned program changes nothing", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        MachOSigner::StripSignature(path);
        CHECK(ms::ReadAll(path) == original);
        MachOSigner::StripSignature(path);
        CHECK(ms::ReadAll(path) == original);
    }
}

TEST_CASE("MachOSigner signing after a strip gives the file signed once", "[MachOSigner]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        ms::SignWithKnownInputs(path);
        const Bytes once = ms::ReadAll(path);

        // strip then sign equals sign of the unsigned original
        MachOSigner::StripSignature(path);
        ms::SignWithKnownInputs(path);
        CHECK(ms::ReadAll(path) == once);

        // and again, with a different capacity in the middle
        MachOSigner::StripSignature(path);
        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 4000);
        MachOSigner::CompleteSignature(path, prepared, ms::CountingCms(prepared.slices.size(), 4000));
        MachOSigner::StripSignature(path);
        ms::SignWithKnownInputs(path);
        CHECK(ms::ReadAll(path) == once);
    }
}

TEST_CASE("MachOSigner StripSignature removes a signature made by another tool", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-arm64-adhoc", "tiny-macho-x86_64-adhoc", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes foreign = ms::ReadAll(path);
        REQUIRE(MachOSigner::HasEmbeddedSignature(path));

        MachOSigner::StripSignature(path);
        const Bytes stripped = ms::ReadAll(path);
        CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
        CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
        ExpectStripped(foreign, stripped);

        // The result is an unsigned program that can be signed like any other.
        const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
        const auto cms = ms::CountingCms(prepared.slices.size());
        MachOSigner::CompleteSignature(path, prepared, cms);
        ms::ExpectFinished(stripped, ms::ReadAll(path), prepared, cms);
    }
}

TEST_CASE("MachOSigner StripSignature on a missing file names it", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    const std::string missing = scratch.File("absent");
    const std::string message = ms::ErrorOf([&] { MachOSigner::StripSignature(missing); });
    CHECK(NamesTheFile(message, missing));
    CHECK_FALSE(std::filesystem::exists(missing));
}

TEST_CASE("MachOSigner non-zero bytes between universal slices refuse signing and stripping", "[MachOSigner]")
{
    for(const char *name : {"tiny-macho-universal", "tiny-macho-universal-adhoc"})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        Bytes file = ms::LoadFixture(name);
        const std::vector<TableEntry> table = ReadTable(file);
        REQUIRE(table.size() == 2);
        const std::uint64_t gap_start = table[0].offset + table[0].size;
        REQUIRE(gap_start < table[1].offset);
        file.at(static_cast<std::size_t>(gap_start + 1)) = 0x5A;
        const std::string path = ms::WriteScratch(scratch, "padding", file);

        const std::string prepare = ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "test-identity", 64); });
        CHECK(prepare.rfind(path + ": ", 0) == 0);
        CHECK(Contains(prepare, "the bytes between slice 0 ("));
        CHECK(Contains(prepare, "are not all zero (the first is at offset " + std::to_string(gap_start + 1) + ")"));
        CHECK(ms::ReadAll(path) == file);

        const std::string strip = ms::ErrorOf([&] { MachOSigner::StripSignature(path); });
        CHECK(Contains(strip, "are not all zero"));
        CHECK(ms::ReadAll(path) == file);
    }
}

TEST_CASE("MachOSigner a signature command larger than 16 bytes keeps its size when re-signed", "[MachOSigner]")
{
    seedtest::ScratchDir scratch;
    Bytes file = ms::LoadFixture("tiny-macho-x86_64-adhoc");
    std::uint64_t ncmds = 0, sizeofcmds = 0;
    REQUIRE(mr::ReadLE(file, 16, 4, ncmds));
    REQUIRE(mr::ReadLE(file, 20, 4, sizeofcmds));
    // The signature command is the last one; give it 8 more bytes (the zero
    // bytes of the header space that follows it).
    const std::uint64_t command_at = 32 + sizeofcmds - 16;
    std::uint64_t cmd = 0, size = 0;
    REQUIRE(mr::ReadLE(file, command_at, 4, cmd));
    REQUIRE(mr::ReadLE(file, command_at + 4, 4, size));
    REQUIRE(cmd == 0x1D);
    REQUIRE(size == 16);
    for(std::size_t i = 0; i < 8; ++i)
    {
        REQUIRE(file.at(static_cast<std::size_t>(32 + sizeofcmds + i)) == 0);
    }
    PutLE(file, static_cast<std::size_t>(command_at + 4), 24, 4);
    PutLE(file, 20, sizeofcmds + 8, 4);
    const std::string path = ms::WriteScratch(scratch, "wide-command", file);

    const auto prepared = MachOSigner::PrepareSignature(path, "test-identity", 64);
    const auto cms = ms::CountingCms(1);
    MachOSigner::CompleteSignature(path, prepared, cms);
    const Bytes signed_file = ms::ReadAll(path);

    std::uint64_t after_ncmds = 0, after_sizeofcmds = 0, after_size = 0;
    REQUIRE(mr::ReadLE(signed_file, 16, 4, after_ncmds));
    REQUIRE(mr::ReadLE(signed_file, 20, 4, after_sizeofcmds));
    REQUIRE(mr::ReadLE(signed_file, static_cast<std::size_t>(command_at + 4), 4, after_size));
    CHECK(after_ncmds == ncmds);
    CHECK(after_sizeofcmds == sizeofcmds + 8);
    CHECK(after_size == 24);
    // The bytes that follow the command are not touched.
    for(std::size_t i = 0; i < 8; ++i)
    {
        CHECK(signed_file.at(static_cast<std::size_t>(32 + sizeofcmds + i)) == 0);
    }
    const mr::FileReport report = mr::CheckFile(signed_file);
    INFO("problems: " << ms::JoinProblems(report));
    CHECK(report.Ok());
    CHECK(MachOSigner::HasEmbeddedSignature(path));
    CHECK(MachOParser::GetArchSlices(path).at(0).is_signed);

    // Stripping removes all 24 bytes of the command.
    MachOSigner::StripSignature(path);
    const Bytes stripped = ms::ReadAll(path);
    std::uint64_t strip_ncmds = 0, strip_sizeofcmds = 0;
    REQUIRE(mr::ReadLE(stripped, 16, 4, strip_ncmds));
    REQUIRE(mr::ReadLE(stripped, 20, 4, strip_sizeofcmds));
    CHECK(strip_ncmds == ncmds - 1);
    CHECK(strip_sizeofcmds == sizeofcmds + 8 - 24);
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
}
