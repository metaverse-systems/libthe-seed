// MachOSigner: the program has no header space for the signature command, and
// signing writes nothing but the named changes.
//
// Statements tested here, each by the cases named after it:
//   - a program with no header space is refused with the exact "no room"
//     message (bytes needed and available, the offsets, advice) and the file is
//     byte-identical, with its modification time and its folder unchanged;
//   - header space is measured from the end of the load commands to the lowest
//     file offset used by any section or segment, not to the end of the file;
//   - exactly 16 free bytes are enough and the first section is not touched;
//   - a program that already has a signature needs no room;
//   - a universal file with one refusing slice is refused whole and unchanged;
//   - relinking with room makes the same path signable;
//   - for every sample, every slice and every repeated signing, each byte
//     outside the named changes (header counts and sizes, the signature
//     command, the link-edit sizes, the signature data and its padding, the
//     universal table entries) equals the original.
//
// The files with a name ending in -nospace and -exactfit are genuine linker
// output; the two programs built inside the test are synthetic (an edited copy
// of a genuine sample) and serve only the refusal cases.

#include "MachOSigningSupport.hpp"

#include <libthe-seed/MachOParser.hpp>

#include <algorithm>
#include <chrono>
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

// The contract's text, with the numbers of the case.
std::string NoRoomText(std::uint64_t available, std::uint64_t end_of_commands, std::uint64_t first_content)
{
    return "no room for the code signature command: 16 bytes needed, " + std::to_string(available) +
           " available between the end of the load commands (offset " + std::to_string(end_of_commands) +
           ") and the first section (offset " + std::to_string(first_content) +
           "); relink with extra header space (for example -headerpad 0x20)";
}

std::size_t CountEntries(const std::filesystem::path &folder)
{
    std::size_t n = 0;
    for(const auto &entry : std::filesystem::directory_iterator(folder))
    {
        (void)entry;
        ++n;
    }
    return n;
}

// A file as the test left it, with a modification time far in the past so that
// any rewrite shows even on a coarse clock.
struct Aged
{
    std::string path;
    Bytes bytes;
    std::filesystem::file_time_type modified;
};

Aged AgeFile(const std::string &path)
{
    const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
    std::filesystem::last_write_time(path, old);
    return {path, ms::ReadAll(path), std::filesystem::last_write_time(path)};
}

void ExpectUntouched(const Aged &before, const seedtest::ScratchDir &scratch, std::size_t entries)
{
    CHECK(ms::ReadAll(before.path) == before.bytes);
    CHECK(std::filesystem::last_write_time(before.path) == before.modified);
    CHECK(CountEntries(scratch.Path()) == entries);
}

// Walks the load commands of a thin 64-bit little-endian image and returns the
// offset of the first command with the given number, or -1.
std::int64_t FindCommand(const Bytes &slice, std::uint32_t wanted)
{
    std::uint64_t ncmds = 0;
    mr::ReadLE(slice, 16, 4, ncmds);
    std::uint64_t at = 32;
    for(std::uint64_t i = 0; i < ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        if(!mr::ReadLE(slice, at, 4, cmd) || !mr::ReadLE(slice, at + 4, 4, size) || size < 8)
        {
            return -1;
        }
        if(cmd == wanted)
        {
            return static_cast<std::int64_t>(at);
        }
        at += size;
    }
    return -1;
}

// The offset of the __LINKEDIT segment command, or -1.
std::int64_t FindLinkedit(const Bytes &slice)
{
    std::uint64_t ncmds = 0;
    mr::ReadLE(slice, 16, 4, ncmds);
    std::uint64_t at = 32;
    for(std::uint64_t i = 0; i < ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        if(!mr::ReadLE(slice, at, 4, cmd) || !mr::ReadLE(slice, at + 4, 4, size) || size < 8)
        {
            return -1;
        }
        if(cmd == 0x19 && mr::Fits(slice, at + 8, 16) &&
           std::string(reinterpret_cast<const char *>(&slice[at + 8])) == "__LINKEDIT")
        {
            return static_cast<std::int64_t>(at);
        }
        at += size;
    }
    return -1;
}

void Put64(Bytes &b, std::uint64_t at, std::uint64_t value)
{
    for(int i = 0; i < 8; ++i)
    {
        b.at(static_cast<std::size_t>(at) + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void Put32LE(Bytes &b, std::uint64_t at, std::uint32_t value)
{
    for(int i = 0; i < 4; ++i)
    {
        b.at(static_cast<std::size_t>(at) + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

// SYNTHETIC. The -nospace program with a zero-filled area appended and the
// link-edit segment extended over it: the end of the file looks roomy, the
// section data still starts right after the load commands.
Bytes NoSpaceWithRoomyEnd()
{
    Bytes b = ms::LoadFixture("tiny-macho-x86_64-nospace");
    const std::int64_t seg = FindLinkedit(b);
    REQUIRE(seg >= 0);
    std::uint64_t vmsize = 0, filesize = 0;
    REQUIRE(mr::ReadLE(b, static_cast<std::uint64_t>(seg) + 32, 8, vmsize));
    REQUIRE(mr::ReadLE(b, static_cast<std::uint64_t>(seg) + 48, 8, filesize));
    b.resize(b.size() + 8192, 0);
    Put64(b, static_cast<std::uint64_t>(seg) + 32, vmsize + 8192);
    Put64(b, static_cast<std::uint64_t>(seg) + 48, filesize + 8192);
    return b;
}

// SYNTHETIC. The -exactfit program with one more 8-byte load command of an
// unknown kind in its free area: 8 bytes free instead of 16.
Bytes ExactFitWithEightBytesUsed()
{
    Bytes b = ms::LoadFixture("tiny-macho-x86_64-exactfit");
    std::uint64_t ncmds = 0, sizeofcmds = 0;
    REQUIRE(mr::ReadLE(b, 16, 4, ncmds));
    REQUIRE(mr::ReadLE(b, 20, 4, sizeofcmds));
    REQUIRE(sizeofcmds == 648);
    Put32LE(b, 32 + sizeofcmds, 0x7E);
    Put32LE(b, 32 + sizeofcmds + 4, 8);
    Put32LE(b, 16, static_cast<std::uint32_t>(ncmds + 1));
    Put32LE(b, 20, static_cast<std::uint32_t>(sizeofcmds + 8));
    return b;
}

const std::vector<std::string> &SignedByAnotherTool()
{
    static const std::vector<std::string> names = {"tiny-macho-x86_64-adhoc", "tiny-macho-arm64-adhoc",
                                                   "tiny-macho-universal-adhoc"};
    return names;
}

// One slice's bytes inside a file.
Bytes SliceOf(const Bytes &file, const mr::SliceReport &s)
{
    REQUIRE(mr::Fits(file, s.offset, s.size));
    return Bytes(file.begin() + static_cast<std::ptrdiff_t>(s.offset),
                 file.begin() + static_cast<std::ptrdiff_t>(s.offset + s.size));
}

// Every byte of `changed` outside the named changes equals `original`.
// `original` and `changed` are the files before and after one or more signings.
void ExpectOnlyNamedChanges(const Bytes &original, const Bytes &changed)
{
    const mr::FileReport before = mr::CheckFile(original);
    const mr::FileReport after = mr::CheckFile(changed);
    REQUIRE(before.Ok());
    REQUIRE(after.Ok());
    REQUIRE(after.form == before.form);
    REQUIRE(after.slices.size() == before.slices.size());

    for(std::size_t i = 0; i < before.slices.size(); ++i)
    {
        INFO("slice " << i);
        const mr::SliceReport &o = before.slices[i];
        const mr::SliceReport &s = after.slices[i];
        const Bytes was = SliceOf(original, o);
        const Bytes now = SliceOf(changed, s);

        const std::int64_t linkedit = FindLinkedit(was);
        REQUIRE(linkedit >= 0);
        const std::int64_t old_command = FindCommand(was, 0x1D);
        const std::uint64_t command_at =
            old_command >= 0 ? static_cast<std::uint64_t>(old_command) : 32 + o.sizeofcmds;
        // Up to the old signature data (or the old end) the bytes are the original's,
        // except for the places the change names.
        const std::uint64_t compare = o.has_signature ? o.dataoff : was.size();
        const std::vector<mr::Range> allowed = {
            {16, 8},                                             // ncmds, sizeofcmds
            {command_at, 16},                                    // the signature command
            {static_cast<std::uint64_t>(linkedit) + 32, 8},      // link-edit vmsize
            {static_cast<std::uint64_t>(linkedit) + 48, 8},      // link-edit filesize
        };
        const std::int64_t difference = mr::FirstDifferenceOutside(was, now, allowed, compare);
        INFO("first byte that differs outside the named changes: " << difference);
        CHECK(difference == -1);
        CHECK(now.size() >= compare);

        // The command's neighbours and the link-edit start are untouched (the
        // ranges above are the only exceptions): the link-edit file offset
        // does not move.
        CHECK(s.linkedit_fileoff == o.linkedit_fileoff);
        CHECK(s.cputype == o.cputype);
        CHECK(s.cpusubtype == o.cpusubtype);
        if(old_command < 0)
        {
            CHECK(s.ncmds == o.ncmds + 1);
            CHECK(s.sizeofcmds == o.sizeofcmds + 16);
        }
        else
        {
            CHECK(s.ncmds == o.ncmds);
            CHECK(s.sizeofcmds == o.sizeofcmds);
        }

        // Between the old end of the content and the new signature data only
        // zero bytes (alignment padding).
        if(!o.has_signature)
        {
            REQUIRE(s.dataoff >= was.size());
            for(std::uint64_t at = was.size(); at < s.dataoff; ++at)
            {
                REQUIRE(now.at(static_cast<std::size_t>(at)) == 0);
            }
        }
    }

    if(before.form != 0)
    {
        const std::size_t entry = before.form == 2 ? 32 : 20;
        for(std::size_t i = 0; i < before.slices.size(); ++i)
        {
            INFO("table entry " << i);
            const std::uint64_t at = 8 + i * entry;
            // The magic and the count.
            if(i == 0)
            {
                CHECK(Bytes(original.begin(), original.begin() + 8) == Bytes(changed.begin(), changed.begin() + 8));
            }
            // The CPU type and subtype.
            CHECK(Bytes(original.begin() + static_cast<std::ptrdiff_t>(at),
                        original.begin() + static_cast<std::ptrdiff_t>(at + 8)) ==
                  Bytes(changed.begin() + static_cast<std::ptrdiff_t>(at),
                        changed.begin() + static_cast<std::ptrdiff_t>(at + 8)));
            // The alignment (and the reserved word of the wide form).
            const std::uint64_t tail = entry == 32 ? 24 : 16;
            CHECK(Bytes(original.begin() + static_cast<std::ptrdiff_t>(at + tail),
                        original.begin() + static_cast<std::ptrdiff_t>(at + entry)) ==
                  Bytes(changed.begin() + static_cast<std::ptrdiff_t>(at + tail),
                        changed.begin() + static_cast<std::ptrdiff_t>(at + entry)));
            // The slice still starts on its alignment.
            std::uint64_t align_log2 = 0;
            REQUIRE(mr::ReadBE(changed, at + tail, 4, align_log2));
            CHECK(after.slices[i].offset % (std::uint64_t(1) << align_log2) == 0);
        }
        // Slices keep their order and do not overlap; the gaps hold zero bytes.
        std::uint64_t cursor = 8 + before.slices.size() * entry;
        for(std::size_t i = 0; i < after.slices.size(); ++i)
        {
            INFO("gap before slice " << i);
            REQUIRE(after.slices[i].offset >= cursor);
            for(std::uint64_t at = cursor; at < after.slices[i].offset; ++at)
            {
                REQUIRE(changed.at(static_cast<std::size_t>(at)) == 0);
            }
            cursor = after.slices[i].offset + after.slices[i].size;
        }
        CHECK(cursor == changed.size());
    }
}

// One signing with the given capacity and a CMS of `cms_size` bytes.
void SignOnce(const std::string &path, std::uint32_t capacity, std::size_t cms_size)
{
    const auto prepared = MachOSigner::PrepareSignature(path, ms::KnownIdentity(), capacity);
    MachOSigner::CompleteSignature(path, prepared, ms::CountingCms(prepared.slices.size(), cms_size));
}

} // namespace

// ---------------------------------------------------------------------------
// Refusal
// ---------------------------------------------------------------------------

TEST_CASE("MachONoRoom a program with no header space is refused with the exact message", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64-nospace");
    const Aged before = AgeFile(path);

    const std::string message =
        ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity); });
    CHECK(message == path + ": " + NoRoomText(0, 680, 680));
    ExpectUntouched(before, scratch, 1);
}

TEST_CASE("MachONoRoom signing in two calls is refused at the first call", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64-nospace");
    const Aged before = AgeFile(path);
    // A signature prepared from a program with room cannot be completed here.
    const std::string other = ms::CopyFixture(scratch, "tiny-macho-x86_64-exactfit", "other");
    const auto prepared = MachOSigner::PrepareSignature(other, ms::KnownIdentity(), ms::KnownCapacity);

    const std::string message = ms::ErrorOf(
        [&] { MachOSigner::CompleteSignature(path, prepared, ms::CountingCms(prepared.slices.size())); });
    CHECK(message.rfind(path + ": ", 0) == 0);
    ExpectUntouched(before, scratch, 2);
}

TEST_CASE("MachONoRoom the refusal states the bytes available when some room is left", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::WriteScratch(scratch, "eight-free", ExactFitWithEightBytesUsed());
    const Aged before = AgeFile(path);

    const std::string message =
        ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity); });
    CHECK(message == path + ": " + NoRoomText(8, 688, 696));
    ExpectUntouched(before, scratch, 1);
}

TEST_CASE("MachONoRoom the space ends at the section data and not at the end of the file", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const Bytes roomy = NoSpaceWithRoomyEnd();
    REQUIRE(roomy.size() > ms::LoadFixture("tiny-macho-x86_64-nospace").size() + 8000);
    const std::string path = ms::WriteScratch(scratch, "roomy-end", roomy);
    const Aged before = AgeFile(path);

    const std::string message =
        ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity); });
    CHECK(message == path + ": " + NoRoomText(0, 680, 680));
    ExpectUntouched(before, scratch, 1);
}

TEST_CASE("MachONoRoom the refusal of every unsigned sample without room leaves the folder as it was", "[MachONoRoom]")
{
    for(const std::string &name : {"tiny-macho-x86_64-nospace"})
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Aged before = AgeFile(path);
        for(std::uint32_t capacity : {0u, 1u, 64u, 100000u})
        {
            INFO("capacity " << capacity);
            const std::string message =
                ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, "x", capacity); });
            CHECK(Contains(message, "no room for the code signature command: 16 bytes needed, 0 available"));
        }
        ExpectUntouched(before, scratch, 1);
    }
}

// ---------------------------------------------------------------------------
// Exactly enough room, and no room needed
// ---------------------------------------------------------------------------

TEST_CASE("MachONoRoom exactly 16 free bytes are enough and the first section is unchanged", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64-exactfit");
    const Bytes original = ms::ReadAll(path);

    const auto prepared = MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity);
    const std::vector<Bytes> cms = ms::CountingCms(prepared.slices.size());
    MachOSigner::CompleteSignature(path, prepared, cms);
    const Bytes finished = ms::ReadAll(path);
    ms::ExpectFinished(original, finished, prepared, cms);

    // The section data starts at 696; everything from there to the link-edit
    // segment is the program's code and data.
    const mr::FileReport report = mr::CheckFile(original);
    REQUIRE(report.slices.size() == 1);
    const std::uint64_t end = report.slices[0].linkedit_fileoff;
    REQUIRE(end > 696);
    CHECK(Bytes(original.begin() + 696, original.begin() + static_cast<std::ptrdiff_t>(end)) ==
          Bytes(finished.begin() + 696, finished.begin() + static_cast<std::ptrdiff_t>(end)));
    // The load commands end where the section data starts.
    CHECK(mr::CheckFile(finished).slices[0].sizeofcmds + 32 == 696);
    ExpectOnlyNamedChanges(original, finished);
}

TEST_CASE("MachONoRoom a program with a signature needs no room to be signed again", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64-exactfit");
    const Bytes original = ms::ReadAll(path);
    SignOnce(path, 64, 64);
    // Now there are no free bytes left in the header.
    const mr::FileReport signed_once = mr::CheckFile(ms::ReadAll(path));
    REQUIRE(signed_once.slices.size() == 1);
    CHECK(signed_once.slices[0].sizeofcmds + 32 == 696);

    for(std::uint32_t capacity : {200u, 32u, 64u})
    {
        INFO("capacity " << capacity);
        SignOnce(path, capacity, capacity);
        const Bytes again = ms::ReadAll(path);
        CHECK(mr::CheckFile(again).Ok());
        ExpectOnlyNamedChanges(original, again);
    }
}

TEST_CASE("MachONoRoom a program signed by another tool needs no room", "[MachONoRoom]")
{
    for(const std::string &name : SignedByAnotherTool())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        SignOnce(path, 64, 64);
        const Bytes finished = ms::ReadAll(path);
        CHECK(mr::CheckFile(finished).Ok());
        ExpectOnlyNamedChanges(original, finished);
    }
}

// ---------------------------------------------------------------------------
// Universal files
// ---------------------------------------------------------------------------

TEST_CASE("MachONoRoom a universal file with one refusing slice is refused whole", "[MachONoRoom]")
{
    const Bytes tight = ms::LoadFixture("tiny-macho-x86_64-nospace");
    const Bytes roomy = ms::LoadFixture("tiny-macho-arm64");
    struct Case
    {
        const char *name;
        Bytes file;
        bool wide;
        std::size_t refused;
        const char *arch;
    };
    const Case cases[] = {
        {"refusing slice first", mr::Universal({tight, roomy}, false, 14), false, 0, "x86_64"},
        {"refusing slice last", mr::Universal({roomy, tight}, false, 14), false, 1, "x86_64"},
        {"wide table", mr::Universal({roomy, tight}, true, 14), true, 1, "x86_64"},
    };
    for(const Case &c : cases)
    {
        INFO(c.name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::WriteScratch(scratch, "fat", c.file);
        const Aged before = AgeFile(path);

        const std::string message =
            ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity); });
        CHECK(Contains(message, path + ": slice " + std::to_string(c.refused) + " (" + c.arch + "): " +
                                    "no room for the code signature command: 16 bytes needed, 0 available between "
                                    "the end of the load commands (offset 680) and the first section (offset 680); "
                                    "relink with extra header space (for example -headerpad 0x20)"));
        ExpectUntouched(before, scratch, 1);
    }
}

TEST_CASE("MachONoRoom a universal file with room in every slice signs and the others stay as they were", "[MachONoRoom]")
{
    const Bytes exact = ms::LoadFixture("tiny-macho-x86_64-exactfit");
    const Bytes roomy = ms::LoadFixture("tiny-macho-arm64");
    seedtest::ScratchDir scratch;
    const Bytes fat = mr::Universal({exact, roomy}, false, 14);
    const std::string path = ms::WriteScratch(scratch, "fat", fat);
    const auto prepared = MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity);
    const auto cms = ms::CountingCms(prepared.slices.size());
    MachOSigner::CompleteSignature(path, prepared, cms);
    ms::ExpectFinished(fat, ms::ReadAll(path), prepared, cms);
    ExpectOnlyNamedChanges(fat, ms::ReadAll(path));
}

// ---------------------------------------------------------------------------
// Relinked with room
// ---------------------------------------------------------------------------

TEST_CASE("MachONoRoom signing succeeds after the program is relinked with room", "[MachONoRoom]")
{
    seedtest::ScratchDir scratch;
    const std::string path = ms::CopyFixture(scratch, "tiny-macho-x86_64-nospace", "program");
    const Aged before = AgeFile(path);
    (void)ms::ErrorOf([&] { (void)MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity); });
    ExpectUntouched(before, scratch, 1);

    // The same program linked with -headerpad 0x10 takes the place of the file.
    std::filesystem::copy_file(seedtest::FixturePath("tiny-macho-x86_64-exactfit"), path,
                               std::filesystem::copy_options::overwrite_existing);
    const Bytes relinked = ms::ReadAll(path);
    const auto prepared = MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity);
    const auto cms = ms::CountingCms(prepared.slices.size());
    MachOSigner::CompleteSignature(path, prepared, cms);
    ms::ExpectFinished(relinked, ms::ReadAll(path), prepared, cms);
    CHECK(CountEntries(scratch.Path()) == 1);
}

// ---------------------------------------------------------------------------
// Nothing but the named changes
// ---------------------------------------------------------------------------

TEST_CASE("MachONoRoom signing changes only the named bytes of every unsigned sample", "[MachONoRoom]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        const auto prepared = MachOSigner::PrepareSignature(path, ms::KnownIdentity(), ms::KnownCapacity);
        const auto cms = ms::CountingCms(prepared.slices.size());
        MachOSigner::CompleteSignature(path, prepared, cms);
        const Bytes finished = ms::ReadAll(path);
        ms::ExpectFinished(original, finished, prepared, cms);
        ExpectOnlyNamedChanges(original, finished);
        CHECK(CountEntries(scratch.Path()) == 1);
    }
}

TEST_CASE("MachONoRoom repeated signings change only the named bytes of every sample", "[MachONoRoom]")
{
    std::vector<std::string> names = ms::SignableSamples();
    for(const std::string &name : SignedByAnotherTool())
    {
        names.push_back(name);
    }
    // Capacities and CMS sizes that grow, shrink and repeat.
    const std::uint32_t capacities[] = {64, 300, 20, 64, 5000, 64};
    for(const std::string &name : names)
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        const bool unsigned_sample = !mr::CheckFile(original).slices[0].has_signature;
        int round = 0;
        for(std::uint32_t capacity : capacities)
        {
            ++round;
            INFO("signing " << round << " with capacity " << capacity);
            const auto prepared = MachOSigner::PrepareSignature(path, ms::KnownIdentity(), capacity);
            const auto cms = ms::CountingCms(prepared.slices.size(), capacity - capacity / 4);
            MachOSigner::CompleteSignature(path, prepared, cms);
            const Bytes finished = ms::ReadAll(path);
            if(unsigned_sample)
            {
                ms::ExpectFinished(original, finished, prepared, cms);
            }
            ExpectOnlyNamedChanges(original, finished);
            REQUIRE(CountEntries(scratch.Path()) == 1);
        }
    }
}

TEST_CASE("MachONoRoom stripping and signing again changes only the named bytes", "[MachONoRoom]")
{
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const Bytes original = ms::ReadAll(path);
        for(int round = 0; round < 3; ++round)
        {
            INFO("round " << round);
            SignOnce(path, 64, 64);
            ExpectOnlyNamedChanges(original, ms::ReadAll(path));
            MachOSigner::StripSignature(path);
            // After a strip the signature command is gone; the rest is the
            // original except for the link-edit sizes and the length.
            const Bytes stripped = ms::ReadAll(path);
            const mr::FileReport report = mr::CheckFile(stripped);
            CHECK(report.Ok());
            for(const mr::SliceReport &s : report.slices)
            {
                CHECK_FALSE(s.has_signature);
            }
            REQUIRE(CountEntries(scratch.Path()) == 1);
        }
    }
}
