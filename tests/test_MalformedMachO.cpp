// Malformed and edge-case Mac programs and Mac signature data, through
// MachOParser, MachOSigner (file operations) and the two SuperBlob helpers
// MachOSigner::ExtractCmsFromSuperBlob and
// MachOSigner::ExtractCodeDirectoryFromSuperBlob.
//
// Test case names start with their origin: "ok:" for a well-formed or
// legal-but-unusual input with today's result, "review:" and "f18:" for the
// review's input, and "edge:" for an edge-case family.
//
// Byte order. A program whose first four bytes are CF FA ED FE is a real
// little-endian 64-bit Mach-O and holds every header and command field
// little-endian; a universal file (CA FE BA BE or CA FE BA BF) has a
// big-endian table and holds little-endian slices. The synthetic images below
// are written that way (the "genuine" form) and are what MachOParser is
// tested with.
//
// MachOSigner still reads the order it read before the reader was corrected
// (big-endian fields behind CF FA ED FE) until the signer is rewritten on the
// same layout module. The cases that exercise it therefore also use a
// "legacy" copy of each synthetic image in that old order. Every image is
// built as a pair, one copy per order, and an operation is given the copy it
// reads: MachOParser operations the genuine one, MachOSigner operations the
// legacy one. The legacy copies are retired together with the old signer API.
// A load command whose 8-byte header does not fit inside the file ends the
// walk of the signer (it is not an error there).
//
// Mac signature data: a SuperBlob that is well formed but has no slot of the
// wanted type gives an empty result; anything malformed is rejected with
// std::runtime_error whose message starts with "Mach-O code signature: ". The
// keywords the tests look for are:
//   SuperBlob header   fewer bytes than the 12-byte header
//   magic              the SuperBlob or the wanted blob has the wrong magic
//   length             the SuperBlob length field is past the input or smaller than its index
//   blob index         the index does not fit, or a slot offset is inside the index or past the end
//   blob length        the wanted blob's length is below 8 or runs past the SuperBlob
//
// Mac programs: a rejection is a std::runtime_error whose message starts with
// "Mach-O: ". The keywords the tests look for (the message names the
// structure with its zero-based index where it has one) are:
//   load command N size S     a command whose header is inside the file has a size below 8
//                             ("... is smaller than 8") or not a multiple of 4
//                             ("... is not a multiple of 4")
//   LC_SEGMENT_64             a __TEXT or __LINKEDIT segment command smaller than 72 bytes or
//                             not wholly inside the file ("... extends past the end of the file")
//   LC_LOAD_DYLIB             a dylib command smaller than 24 bytes, not wholly inside the
//                             file, with its name offset outside the command, or with a name
//                             not ended by a NUL inside the command
//   LC_CODE_SIGNATURE         a command smaller than 16 bytes or not wholly inside the file; a
//                             second one; data past the end of the file
//                             ("... extends past the end of the file") or inside the
//                             load-command area
//   __LINKEDIT                EmbedSignature: the segment's file offset is past the end of the file
//   No space for new load command
//                             EmbedSignature: header size + sizeofcmds + 16 does not fit
//   Truncated fat_arch entry table
//                             the slice table does not fit in the file (checked before any
//                             allocation); "Truncated fat_arch entry" is also kept verbatim
//   not a Mach-O              a universal magic with a slice count of 0 or above 256, or whose first
//                             slice does not start with a Mach-O magic (a Java class file has the
//                             same first four bytes)
//   past the end              a fat slice reaches past the end of the file
//   overlap                   two fat slices overlap
//   header table              a fat slice starts inside the slice table
//   header                    a program shorter than its header fields
//   too small                 a file under 4 bytes given to an operation that needs a program
//   SuperBlob                 EmbedSignature: a SuperBlob that its 32-bit size field cannot describe
//
// An operation that does not read the faulty structure keeps answering as
// today; the tests check that for MachOParser::ListDependencies when only the
// segment or signature commands are faulty, and for the signer operations
// other than EmbedSignature when only sizeofcmds or the __LINKEDIT file
// offset is faulty.

#include "MalformedInput.hpp"
#include "DepFixtures.hpp"

#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "internal/MachOSuperBlob.hpp"

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;
using seedtest::malformed::PatchBE;
using seedtest::malformed::RequireRejected;
using Result = std::optional<Bytes>;

constexpr const char *kFormat = "Mach-O code signature";

constexpr std::uint32_t kSuperBlobMagic = 0xFADE0CC0;
constexpr std::uint32_t kCodeDirectoryMagic = 0xFADE0C02;
constexpr std::uint32_t kCmsWrapperMagic = 0xFADE0B01;
constexpr std::uint32_t kCmsSlotType = 0x10000;

// BuildSuperBlob lays out a 12-byte header, three index entries (code
// directory, requirements, CMS) and then the three blobs.
constexpr std::uint64_t kLengthField = 4;
constexpr std::uint64_t kCountField = 8;
constexpr std::uint64_t kIndexStart = 12;
constexpr std::uint32_t kCodeDirectoryEntry = 0;
constexpr std::uint32_t kCmsEntry = 2;

std::uint64_t EntryTypeField(std::uint32_t entry)
{
    return kIndexStart + 8ull * entry;
}

std::uint64_t EntryOffsetField(std::uint32_t entry)
{
    return kIndexStart + 8ull * entry + 4;
}

std::uint32_t GetBE32(const Bytes &bytes, std::uint64_t offset)
{
    REQUIRE(offset + 4 <= bytes.size());
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

// A code directory blob: its magic, its own length, then filler.
Bytes FakeCodeDirectory()
{
    Bytes cd(96);
    for(std::size_t i = 0; i < cd.size(); ++i)
    {
        cd[i] = static_cast<std::uint8_t>(0x40 + (i % 23));
    }
    PatchBE<std::uint32_t>(cd, 0, kCodeDirectoryMagic);
    PatchBE<std::uint32_t>(cd, 4, static_cast<std::uint32_t>(cd.size()));
    return cd;
}

Bytes FakeCms()
{
    Bytes cms(70);
    for(std::size_t i = 0; i < cms.size(); ++i)
    {
        cms[i] = static_cast<std::uint8_t>(0xA0 + (i % 31));
    }
    return cms;
}

struct Built
{
    Bytes blob;
    Bytes code_directory;
    Bytes cms;
};

Built MakeBuilt()
{
    Built built;
    built.code_directory = FakeCodeDirectory();
    built.cms = FakeCms();
    built.blob = MachOSigner::BuildSuperBlob(built.code_directory, built.cms);
    REQUIRE(GetBE32(built.blob, 0) == kSuperBlobMagic);
    REQUIRE(GetBE32(built.blob, kLengthField) == built.blob.size());
    return built;
}

// One of the two helpers, with the index entry that holds the blob it wants.
struct Helper
{
    const char *name;
    Result (*extract)(const Bytes &);
    std::uint32_t entry;
    std::uint32_t slot_type;
    std::uint32_t blob_magic;
};

const Helper kHelpers[] = {
    {"ExtractCmsFromSuperBlob",
     [](const Bytes &b) -> Result { return MachOSigner::ExtractCmsFromSuperBlob(b); }, kCmsEntry,
     kCmsSlotType, kCmsWrapperMagic},
    {"ExtractCodeDirectoryFromSuperBlob",
     [](const Bytes &b) -> Result { return MachOSigner::ExtractCodeDirectoryFromSuperBlob(b); },
     kCodeDirectoryEntry, 0, kCodeDirectoryMagic},
};

template <typename F>
void ForBothHelpers(F &&body)
{
    for(const Helper &helper : kHelpers)
    {
        DYNAMIC_SECTION(helper.name)
        {
            body(helper);
        }
    }
}

void RequireHelperRejects(const Helper &helper, const Bytes &input, const std::string &keyword)
{
    RequireRejected([&] { (void)helper.extract(input); }, kFormat, keyword, input.size());
}

// What the helper returns for the unmodified output of BuildSuperBlob.
Bytes Expected(const Helper &helper, const Built &built)
{
    return helper.entry == kCmsEntry ? built.cms : built.code_directory;
}

} // namespace

// ---------------------------------------------------------------------------
// Well-formed and legal-but-unusual input
// ---------------------------------------------------------------------------

TEST_CASE("ok: SuperBlob from BuildSuperBlob", "[MalformedMachO][ok]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        const Result result = helper.extract(built.blob);
        REQUIRE(result.has_value());
        CHECK(*result == Expected(helper, built));
    });
}

TEST_CASE("ok: SuperBlob with zero padding after its length", "[MalformedMachO][ok]")
{
    // Linkers pad the signature area; the bytes after `length` are not part of
    // the SuperBlob.
    const Built built = MakeBuilt();
    Bytes padded = built.blob;
    padded.resize(padded.size() + 123, 0);
    ForBothHelpers([&](const Helper &helper) {
        const Result result = helper.extract(padded);
        REQUIRE(result.has_value());
        CHECK(*result == Expected(helper, built));
    });
}

TEST_CASE("ok: well-formed SuperBlob without the wanted slot", "[MalformedMachO][ok]")
{
    const Built built = MakeBuilt();
    SECTION("no CMS slot: the index lists two entries")
    {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, kCountField, 2);
        CHECK_FALSE(MachOSigner::ExtractCmsFromSuperBlob(blob).has_value());
        const auto cd = MachOSigner::ExtractCodeDirectoryFromSuperBlob(blob);
        REQUIRE(cd.has_value());
        CHECK(*cd == built.code_directory);
    }
    SECTION("no CMS slot: the slot has another type")
    {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, EntryTypeField(kCmsEntry), 0x10001);
        CHECK_FALSE(MachOSigner::ExtractCmsFromSuperBlob(blob).has_value());
    }
    SECTION("no code directory slot: the slot has another type")
    {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, EntryTypeField(kCodeDirectoryEntry), 1);
        CHECK_FALSE(MachOSigner::ExtractCodeDirectoryFromSuperBlob(blob).has_value());
        const auto cms = MachOSigner::ExtractCmsFromSuperBlob(blob);
        REQUIRE(cms.has_value());
        CHECK(*cms == built.cms);
    }
    SECTION("an empty index")
    {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, kCountField, 0);
        CHECK_FALSE(MachOSigner::ExtractCmsFromSuperBlob(blob).has_value());
        CHECK_FALSE(MachOSigner::ExtractCodeDirectoryFromSuperBlob(blob).has_value());
    }
}

// ---------------------------------------------------------------------------
// From the review
// ---------------------------------------------------------------------------

TEST_CASE("review: SuperBlob slot 0xFFFFFFFC", "[MalformedMachO][review]")
{
    // `offset + 8` wraps in 32 bits and the offset is far outside the input.
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, EntryOffsetField(helper.entry), 0xFFFFFFFCu);
        RequireHelperRejects(helper, blob, "blob index");
    });
}

// ---------------------------------------------------------------------------
// Edge-case families
// ---------------------------------------------------------------------------

TEST_CASE("edge: SuperBlob count 0xFFFFFFFF", "[MalformedMachO][edge]")
{
    // The wanted slot is a real entry, so only a check of the index against
    // the length rejects this.
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, kCountField, 0xFFFFFFFFu);
        RequireHelperRejects(helper, blob, "blob index");
    });
}

TEST_CASE("edge: SuperBlob count larger than its length allows", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, kCountField, 1000);
        RequireHelperRejects(helper, blob, "blob index");
    });
}

TEST_CASE("edge: SuperBlob length 0xFFFFFFFF", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, kLengthField, 0xFFFFFFFFu);
        RequireHelperRejects(helper, blob, "length");
    });
}

TEST_CASE("edge: SuperBlob length past the input", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        Bytes blob = built.blob;
        PatchBE<std::uint32_t>(blob, kLengthField, static_cast<std::uint32_t>(blob.size() + 1));
        RequireHelperRejects(helper, blob, "length");
    });
}

TEST_CASE("edge: SuperBlob length smaller than the index", "[MalformedMachO][edge]")
{
    // The index needs 12 + 3 * 8 = 36 bytes.
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        for(const std::uint32_t length : {0u, 11u, 12u, 35u})
        {
            DYNAMIC_SECTION("length " << length)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, kLengthField, length);
                RequireHelperRejects(helper, blob, "length");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob slot offset plus 8 wraps 32 bits", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        for(const std::uint32_t offset : {0xFFFFFFF8u, 0xFFFFFFFAu, 0xFFFFFFFFu})
        {
            DYNAMIC_SECTION("offset " << offset)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, EntryOffsetField(helper.entry), offset);
                RequireHelperRejects(helper, blob, "blob index");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob slot offset past the length", "[MalformedMachO][edge]")
{
    // Inside the input (padding follows) but past the length field.
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        Bytes blob = built.blob;
        blob.resize(blob.size() + 64, 0);
        PatchBE<std::uint32_t>(blob, EntryOffsetField(helper.entry),
                               static_cast<std::uint32_t>(built.blob.size() - 4));
        RequireHelperRejects(helper, blob, "blob index");
    });
}

TEST_CASE("edge: SuperBlob slot offset of an unwanted slot", "[MalformedMachO][edge]")
{
    // Every slot offset is checked, not only the one of the requested blob.
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        for(const std::uint32_t other : {kCmsEntry, kCodeDirectoryEntry})
        {
            if(other == helper.entry)
            {
                continue;
            }
            for(const std::uint32_t offset : {0xFFFFFFFCu, 4u})
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, EntryOffsetField(other), offset);
                RequireHelperRejects(helper, blob, "blob index");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob slot offset pointing at the header", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        for(const std::uint32_t offset : {0u, 4u, 12u, 35u})
        {
            DYNAMIC_SECTION("offset " << offset)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, EntryOffsetField(helper.entry), offset);
                RequireHelperRejects(helper, blob, "blob index");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob inner blob length past the SuperBlob", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        const std::uint32_t blob_offset = GetBE32(built.blob, EntryOffsetField(helper.entry));
        const std::uint64_t inner_length_field = static_cast<std::uint64_t>(blob_offset) + 4;
        for(const std::uint32_t length :
            {static_cast<std::uint32_t>(built.blob.size() - blob_offset + 1), 0xFFFFFFFFu})
        {
            DYNAMIC_SECTION("inner length " << length)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, inner_length_field, length);
                RequireHelperRejects(helper, blob, "blob length");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob inner blob length below 8", "[MalformedMachO][edge]")
{
    // The CMS helper subtracts 8 from this length.
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        const std::uint32_t blob_offset = GetBE32(built.blob, EntryOffsetField(helper.entry));
        for(const std::uint32_t length : {0u, 7u})
        {
            DYNAMIC_SECTION("inner length " << length)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, static_cast<std::uint64_t>(blob_offset) + 4, length);
                RequireHelperRejects(helper, blob, "blob length");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob wrong outer magic", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        for(const std::uint32_t magic : {0u, 0xFADE0C01u, 0xFADE0CC1u, 0xC0DEFA0Du})
        {
            DYNAMIC_SECTION("magic " << magic)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, 0, magic);
                RequireHelperRejects(helper, blob, "magic");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob wrong inner magic", "[MalformedMachO][edge]")
{
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        const std::uint32_t blob_offset = GetBE32(built.blob, EntryOffsetField(helper.entry));
        for(const std::uint32_t magic : {0u, helper.blob_magic ^ 1u, 0xFADE0C01u})
        {
            DYNAMIC_SECTION("magic " << magic)
            {
                Bytes blob = built.blob;
                PatchBE<std::uint32_t>(blob, blob_offset, magic);
                RequireHelperRejects(helper, blob, "magic");
            }
        }
    });
}

TEST_CASE("edge: SuperBlob empty vector", "[MalformedMachO][edge]")
{
    ForBothHelpers([&](const Helper &helper) { RequireHelperRejects(helper, Bytes{}, "SuperBlob header"); });
}

TEST_CASE("edge: every prefix of a built SuperBlob is rejected", "[MalformedMachO][edge]")
{
    // A prefix is a SuperBlob whose length field is past the input; it is
    // malformed, never "no signature".
    const Built built = MakeBuilt();
    ForBothHelpers([&](const Helper &helper) {
        for(std::size_t length = 0; length < built.blob.size(); ++length)
        {
            INFO("prefix of " << length << " bytes");
            const Bytes prefix = seedtest::malformed::Truncate(built.blob, length);
            RequireRejected([&] { (void)helper.extract(prefix); }, kFormat, length < 12 ? "SuperBlob header" : "length",
                            prefix.size());
        }
    });
}

// ===========================================================================
// Mac programs: images, operations and helpers
// ===========================================================================

namespace {

using seedtest::malformed::PatchLE;
using seedtest::malformed::RequireUnchanged;
using seedtest::malformed::Truncate;
using seedtest::malformed::WriteScratch;

constexpr const char *kProgramFormat = "Mach-O";

constexpr std::uint32_t kLcSegment64 = 0x19;
constexpr std::uint32_t kLcLoadDylib = 0x0C;
constexpr std::uint32_t kLcCodeSignature = 0x1D;

// The synthetic 64-bit image: a 32-byte header, then these commands, then
// zeros up to the first page, a 4096-byte __TEXT segment starting at file
// offset 0, and a 64-byte __LINKEDIT segment at 4096. The signed variant adds
// an LC_CODE_SIGNATURE whose data are the last 32 bytes of __LINKEDIT.
constexpr std::uint64_t kNcmdsField = 16;
constexpr std::uint64_t kSizeofcmdsField = 20;
constexpr std::uint64_t kTextCmd = 32;       // LC_SEGMENT_64 __TEXT, 72 bytes
constexpr std::uint64_t kLinkeditCmd = 104;  // LC_SEGMENT_64 __LINKEDIT, 72 bytes
constexpr std::uint64_t kDylibCmd = 176;     // LC_LOAD_DYLIB, 56 bytes
constexpr std::uint64_t kSignatureCmd = 232; // LC_CODE_SIGNATURE, 16 bytes (signed variant)
constexpr std::uint64_t kSegmentCmdSize = 72;
constexpr std::uint64_t kDylibCmdSize = 56;
constexpr std::uint64_t kSignatureCmdSize = 16;
constexpr std::uint64_t kCommandsSize = 200;
constexpr std::uint64_t kLinkeditOffset = 4096;
constexpr std::uint64_t kUnsignedSize = 4160;
constexpr std::uint64_t kSignatureOffset = 4128;
constexpr std::uint64_t kSignatureSize = 32;
constexpr const char *kDylibName = "/usr/lib/libSystem.B.dylib";

// Field offsets inside a command.
constexpr std::uint64_t kCmdSizeField = 4;
constexpr std::uint64_t kSegmentNameField = 8;
constexpr std::uint64_t kSegmentFileoffField = 40;
constexpr std::uint64_t kDylibNameOffsetField = 8;
constexpr std::uint64_t kDylibNameField = 24;
constexpr std::uint64_t kSignatureDataoffField = 8;
constexpr std::uint64_t kSignatureDatasizeField = 12;

// The byte order of the fields of a synthetic image.
enum class Conv
{
    Genuine, // little-endian behind CF FA ED FE, as the platform writes
    Legacy   // big-endian behind CF FA ED FE, as the signer still reads
};

void Put32(Bytes &image, Conv conv, std::uint64_t at, std::uint32_t value)
{
    if(conv == Conv::Genuine)
    {
        PatchLE<std::uint32_t>(image, at, value);
    }
    else
    {
        PatchBE<std::uint32_t>(image, at, value);
    }
}

void Put64(Bytes &image, Conv conv, std::uint64_t at, std::uint64_t value)
{
    if(conv == Conv::Genuine)
    {
        PatchLE<std::uint64_t>(image, at, value);
    }
    else
    {
        PatchBE<std::uint64_t>(image, at, value);
    }
}

// The same image in both byte orders. Every change is made to both copies.
struct Image
{
    Bytes genuine;
    Bytes legacy;

    const Bytes &For(Conv conv) const
    {
        return conv == Conv::Genuine ? this->genuine : this->legacy;
    }

    void Patch32(std::uint64_t at, std::uint32_t value)
    {
        Put32(this->genuine, Conv::Genuine, at, value);
        Put32(this->legacy, Conv::Legacy, at, value);
    }

    void Patch64(std::uint64_t at, std::uint64_t value)
    {
        Put64(this->genuine, Conv::Genuine, at, value);
        Put64(this->legacy, Conv::Legacy, at, value);
    }

    void Fill(std::uint64_t from, std::uint64_t to, std::uint8_t value)
    {
        for(std::uint64_t i = from; i < to; ++i)
        {
            this->genuine.at(i) = value;
            this->legacy.at(i) = value;
        }
    }

    Image Cut(std::size_t length) const
    {
        return {Truncate(this->genuine, length), Truncate(this->legacy, length)};
    }

    std::size_t size() const
    {
        return this->genuine.size();
    }
};

void PutSegment(Bytes &image, Conv conv, std::uint64_t at, const std::string &name, std::uint64_t vmaddr,
                std::uint64_t vmsize, std::uint64_t fileoff, std::uint64_t filesize)
{
    Put32(image, conv, at, kLcSegment64);
    Put32(image, conv, at + 4, kSegmentCmdSize);
    for(std::size_t i = 0; i < name.size(); ++i)
    {
        image[at + kSegmentNameField + i] = static_cast<std::uint8_t>(name[i]);
    }
    Put64(image, conv, at + 24, vmaddr);
    Put64(image, conv, at + 32, vmsize);
    Put64(image, conv, at + 40, fileoff);
    Put64(image, conv, at + 48, filesize);
    Put32(image, conv, at + 56, 5);
    Put32(image, conv, at + 60, 5);
}

void PutSignatureCommand(Bytes &image, Conv conv, std::uint64_t at, std::uint32_t dataoff, std::uint32_t datasize)
{
    Put32(image, conv, at, kLcCodeSignature);
    Put32(image, conv, at + 4, kSignatureCmdSize);
    Put32(image, conv, at + kSignatureDataoffField, dataoff);
    Put32(image, conv, at + kSignatureDatasizeField, datasize);
}

Bytes MakeImageBytes(bool with_signature, Conv conv)
{
    Bytes image(kUnsignedSize, 0);
    for(std::size_t i = 256; i < image.size(); ++i)
    {
        image[i] = static_cast<std::uint8_t>(0x11 + (i % 97));
    }
    image[0] = 0xCF;
    image[1] = 0xFA;
    image[2] = 0xED;
    image[3] = 0xFE;
    Put32(image, conv, 4, 0x01000007);
    Put32(image, conv, 8, 3);
    Put32(image, conv, 12, 2);
    Put32(image, conv, kNcmdsField, with_signature ? 4 : 3);
    Put32(image, conv, kSizeofcmdsField, static_cast<std::uint32_t>(kCommandsSize + (with_signature ? 16 : 0)));
    Put32(image, conv, 24, 0x00200085);

    PutSegment(image, conv, kTextCmd, "__TEXT", 0, 4096, 0, 4096);
    PutSegment(image, conv, kLinkeditCmd, "__LINKEDIT", 4096, 4096, kLinkeditOffset, 64);

    Put32(image, conv, kDylibCmd, kLcLoadDylib);
    Put32(image, conv, kDylibCmd + 4, kDylibCmdSize);
    Put32(image, conv, kDylibCmd + kDylibNameOffsetField, kDylibNameField);
    Put32(image, conv, kDylibCmd + 12, 2);
    Put32(image, conv, kDylibCmd + 16, 0x10000);
    Put32(image, conv, kDylibCmd + 20, 0x10000);
    const std::string name = kDylibName;
    for(std::size_t i = 0; i < name.size(); ++i)
    {
        image[kDylibCmd + kDylibNameField + i] = static_cast<std::uint8_t>(name[i]);
    }

    if(with_signature)
    {
        PutSignatureCommand(image, conv, kSignatureCmd, static_cast<std::uint32_t>(kSignatureOffset),
                            static_cast<std::uint32_t>(kSignatureSize));
        for(std::uint64_t i = kSignatureOffset; i < kUnsignedSize; ++i)
        {
            image[i] = 0x5A;
        }
    }
    return image;
}

Image MakeImage(bool with_signature = false)
{
    return {MakeImageBytes(with_signature, Conv::Genuine), MakeImageBytes(with_signature, Conv::Legacy)};
}

std::vector<std::uint8_t> SignatureBytes(const Bytes &image)
{
    return Bytes(image.begin() + static_cast<std::ptrdiff_t>(kSignatureOffset),
                 image.begin() + static_cast<std::ptrdiff_t>(kSignatureOffset + kSignatureSize));
}

// A universal file as the platform writes it: a big-endian table behind the
// CA FE BA BE bytes.
struct FatEntry
{
    std::uint32_t cpu_type;
    std::uint32_t cpu_subtype;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t align;
};

// The size of each program held in a universal file below (the synthetic image).
constexpr std::uint64_t kSliceSize = kUnsignedSize;
constexpr std::uint64_t kFatSize = 16448;

const std::vector<FatEntry> kGoodSlices = {
    {0x01000007, 3, 4096, static_cast<std::uint32_t>(kSliceSize), 12},
    {0x0100000C, 0, 12288, static_cast<std::uint32_t>(kSliceSize), 14}};

// A table of `entries`, and a complete program (the genuine synthetic image)
// at the offset of every entry that fits after the table. An entry whose
// offset is damaged therefore does not get one. The first entry wins where
// two overlap.
Bytes MakeFat(const std::vector<FatEntry> &entries, std::uint64_t total_size = kFatSize)
{
    Bytes fat(total_size, 0);
    const Bytes program = MakeImageBytes(false, Conv::Genuine);
    const std::uint64_t table_end = 8 + 20 * entries.size();
    for(std::size_t i = entries.size(); i-- > 0;)
    {
        const std::uint64_t at = entries[i].offset;
        if(at >= table_end && at + program.size() <= fat.size())
        {
            std::copy(program.begin(), program.end(), fat.begin() + static_cast<std::ptrdiff_t>(at));
        }
    }
    fat[0] = 0xCA;
    fat[1] = 0xFE;
    fat[2] = 0xBA;
    fat[3] = 0xBE;
    PatchBE<std::uint32_t>(fat, 4, static_cast<std::uint32_t>(entries.size()));
    for(std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::uint64_t at = 8 + 20 * i;
        PatchBE<std::uint32_t>(fat, at, entries[i].cpu_type);
        PatchBE<std::uint32_t>(fat, at + 4, entries[i].cpu_subtype);
        PatchBE<std::uint32_t>(fat, at + 8, entries[i].offset);
        PatchBE<std::uint32_t>(fat, at + 12, entries[i].size);
        PatchBE<std::uint32_t>(fat, at + 16, entries[i].align);
    }
    return fat;
}

// Signature data for EmbedSignature, a SuperBlob of about 220 bytes.
const std::vector<std::uint8_t> &EmbeddedBlob()
{
    static const std::vector<std::uint8_t> blob = MachOSigner::BuildSuperBlob(FakeCodeDirectory(), FakeCms());
    return blob;
}

struct Operation
{
    std::string name;
    std::function<void(const std::string &)> run;
    bool modifies;
    Conv conv; // the byte order of the images the operation reads
};

Operation ListOp()
{
    return {"MachOParser::ListDependencies",
            [](const std::string &path) { (void)MachOParser::ListDependencies(path); }, false, Conv::Genuine};
}

Operation SlicesOp()
{
    return {"MachOParser::GetArchSlices",
            [](const std::string &path) { (void)MachOParser::GetArchSlices(path); }, false, Conv::Genuine};
}

Operation ComputeOp()
{
    return {"MachOSigner::ComputeCodeDirectory",
            [](const std::string &path) { (void)MachOSigner::ComputeCodeDirectory(path, "test-identity"); },
            false, Conv::Legacy};
}

Operation ExtractOp()
{
    return {"MachOSigner::ExtractSignature",
            [](const std::string &path) { (void)MachOSigner::ExtractSignature(path); }, false, Conv::Legacy};
}

Operation HasOp()
{
    return {"MachOSigner::HasEmbeddedSignature",
            [](const std::string &path) { (void)MachOSigner::HasEmbeddedSignature(path); }, false, Conv::Legacy};
}

Operation EmbedOp()
{
    return {"MachOSigner::EmbedSignature",
            [](const std::string &path) { MachOSigner::EmbedSignature(path, EmbeddedBlob()); }, true, Conv::Legacy};
}

// Every operation that reads the load commands of a single-architecture program.
std::vector<Operation> ProgramOps()
{
    return {ListOp(), ComputeOp(), ExtractOp(), HasOp(), EmbedOp()};
}

std::vector<Operation> SignerOps()
{
    return {ComputeOp(), ExtractOp(), HasOp(), EmbedOp()};
}

// Each operation runs on a fresh copy of the input in the byte order it
// reads. It must be rejected with "Mach-O: <keyword>" within the time and heap
// limits, and a rejected embed must leave the file unchanged.
void RequireAllRejected(const Image &input, const std::vector<Operation> &ops, const std::string &keyword)
{
    seedtest::ScratchDir scratch("malformed-macho");
    for(const auto &op : ops)
    {
        DYNAMIC_SECTION(op.name)
        {
            const Bytes &bytes = input.For(op.conv);
            const std::string path = WriteScratch(scratch, "input.bin", bytes);
            seedtest::malformed::RequireRejected([&] { op.run(path); }, kProgramFormat, keyword, bytes.size(),
                                                 EmbeddedBlob().size());
            if(op.modifies)
            {
                RequireUnchanged(path, bytes);
            }
        }
    }
}

// The same for an input whose bytes do not depend on the byte order.
void RequireAllRejected(const Bytes &input, const std::vector<Operation> &ops, const std::string &keyword)
{
    RequireAllRejected(Image{input, input}, ops, keyword);
}

// For today's rejections of well-formed input: the message is only checked
// for the text, because these messages carry no format prefix.
template <typename F>
void RequireThrowsText(F &&callable, const std::string &text)
{
    try
    {
        callable();
    }
    catch(const std::runtime_error &error)
    {
        INFO("message: " << error.what());
        CHECK(std::string(error.what()).find(text) != std::string::npos);
        return;
    }
    FAIL("expected std::runtime_error containing \"" << text << "\"");
}

// The signer's read-only operations on an image whose layout parse accepts it
// and that has no signature command.
void RequireUnsignedQueriesAnswer(const Image &input)
{
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "input.bin", input.legacy);
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
    CHECK_NOTHROW(MachOSigner::ComputeCodeDirectory(path, "test-identity"));
}

// Fields of the code directory that ComputeCodeDirectory builds for `identity`.
void CheckCodeDirectoryShape(const MachOSigner::CodeDirectoryResult &result, std::uint64_t code_limit,
                             const std::string &identity)
{
    const Bytes &cd = result.code_directory;
    const std::uint64_t slots = (code_limit + 4095) / 4096;
    const std::uint64_t length = 88 + identity.size() + 1 + (2 + slots) * 32;
    REQUIRE(cd.size() == length);
    CHECK(GetBE32(cd, 0) == kCodeDirectoryMagic);
    CHECK(GetBE32(cd, 4) == length);
    CHECK(GetBE32(cd, 24) == 2);
    CHECK(GetBE32(cd, 28) == slots);
    CHECK(GetBE32(cd, 32) == code_limit);
    CHECK(result.cd_hash.size() == 32);
}

std::uint64_t GetBE64(const Bytes &bytes, std::uint64_t offset)
{
    return (static_cast<std::uint64_t>(GetBE32(bytes, offset)) << 32) | GetBE32(bytes, offset + 4);
}

Bytes Sample(const std::string &name)
{
    return seedtest::malformed::LoadSample(name);
}

} // namespace

// ---------------------------------------------------------------------------
// Well-formed and legal-but-unusual programs
// ---------------------------------------------------------------------------

TEST_CASE("ok: Mach-O format queries", "[MalformedMachO][ok]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    struct Expectation
    {
        std::string name;
        Bytes bytes;
        MachOParser::Format format;
    };
    const std::vector<Expectation> expectations = {
        {"tiny-macho-x86_64", Sample("tiny-macho-x86_64"), MachOParser::Format::MachO64},
        {"tiny-macho-arm64", Sample("tiny-macho-arm64"), MachOParser::Format::MachO64},
        {"tiny-macho-universal", Sample("tiny-macho-universal"), MachOParser::Format::Fat},
        {"tiny-macho-universal64", Sample("tiny-macho-universal64"), MachOParser::Format::Fat},
        {"synthetic image", MakeImage().genuine, MachOParser::Format::MachO64},
        {"synthetic signed image", MakeImage(true).genuine, MachOParser::Format::MachO64},
        {"synthetic universal file", MakeFat(kGoodSlices), MachOParser::Format::Fat},
        {"tiny.exe", Sample("tiny.exe"), MachOParser::Format::NotMachO},
    };
    for(const auto &expectation : expectations)
    {
        DYNAMIC_SECTION(expectation.name)
        {
            const std::string path = WriteScratch(scratch, "input.bin", expectation.bytes);
            CHECK(MachOParser::DetectFormat(path) == expectation.format);
            CHECK(MachOParser::IsMachO(path) == (expectation.format != MachOParser::Format::NotMachO));
            CHECK(MachOParser::IsFatBinary(path) == (expectation.format == MachOParser::Format::Fat));
        }
    }
}

TEST_CASE("ok: MachOParser::GetArchSlices on single-architecture programs", "[MalformedMachO][ok]")
{
    // One slice covering the whole file; the cpu fields are the ones llvm-otool -h
    // prints for the samples.
    seedtest::ScratchDir scratch("malformed-macho");
    struct Expected
    {
        const char *name;
        std::uint32_t cpu_type;
        std::uint32_t cpu_subtype;
    };
    for(const Expected &expected : {Expected{"tiny-macho-x86_64", 0x01000007, 0x80000003},
                                    Expected{"tiny-macho-arm64", 0x0100000C, 0}})
    {
        DYNAMIC_SECTION(expected.name)
        {
            const Bytes bytes = Sample(expected.name);
            const auto slices = MachOParser::GetArchSlices(WriteScratch(scratch, "input.bin", bytes));
            REQUIRE(slices.size() == 1);
            CHECK(slices[0].cpu_type == expected.cpu_type);
            CHECK(slices[0].cpu_subtype == expected.cpu_subtype);
            CHECK(slices[0].offset == 0);
            CHECK(slices[0].size == bytes.size());
        }
    }
    DYNAMIC_SECTION("synthetic image")
    {
        const auto slices = MachOParser::GetArchSlices(WriteScratch(scratch, "input.bin", MakeImage().genuine));
        REQUIRE(slices.size() == 1);
        CHECK(slices[0].cpu_type == 0x01000007);
        CHECK(slices[0].cpu_subtype == 3);
        CHECK(slices[0].offset == 0);
        CHECK(slices[0].size == kUnsignedSize);
    }
}

TEST_CASE("ok: MachOParser::GetArchSlices on a synthetic universal file", "[MalformedMachO][ok]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    const auto slices = MachOParser::GetArchSlices(WriteScratch(scratch, "input.bin", MakeFat(kGoodSlices)));
    REQUIRE(slices.size() == 2);
    for(std::size_t i = 0; i < slices.size(); ++i)
    {
        INFO("slice " << i);
        CHECK(slices[i].cpu_type == kGoodSlices[i].cpu_type);
        CHECK(slices[i].cpu_subtype == kGoodSlices[i].cpu_subtype);
        CHECK(slices[i].offset == kGoodSlices[i].offset);
        CHECK(slices[i].size == kGoodSlices[i].size);
    }
}

TEST_CASE("ok: tiny-macho-universal is read by the parser and declined by the signer", "[MalformedMachO][ok]")
{
    // The parser reads the genuine universal file: two slices, one library.
    // The signer operations still decline a universal file as a whole, as they
    // did before the signer is rewritten.
    seedtest::ScratchDir scratch("malformed-macho");
    const Bytes bytes = Sample("tiny-macho-universal");
    const std::string path = WriteScratch(scratch, "input.bin", bytes);
    CHECK(MachOParser::GetArchSlices(path).size() == 2);
    CHECK(MachOParser::ListDependencies(path) == std::vector<std::string>{kDylibName});
    for(const auto &op : {ComputeOp(), ExtractOp(), HasOp(), EmbedOp()})
    {
        DYNAMIC_SECTION(op.name)
        {
            RequireThrowsText([&] { op.run(path); }, "Not a single-arch Mach-O binary");
        }
    }
    RequireUnchanged(path, bytes);
}

TEST_CASE("ok: genuine single-architecture programs", "[MalformedMachO][ok]")
{
    // The parser lists the library the program needs. The signer operations
    // keep today's answers until the signer is rewritten: they misread the
    // order, see no signature and fail to find room for a command.
    seedtest::ScratchDir scratch("malformed-macho");
    for(const std::string name : {"tiny-macho-x86_64", "tiny-macho-arm64"})
    {
        DYNAMIC_SECTION(name)
        {
            const Bytes bytes = Sample(name);
            const std::string path = WriteScratch(scratch, "input.bin", bytes);
            CHECK(MachOParser::ListDependencies(path) == std::vector<std::string>{kDylibName});
            CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
            CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
            CheckCodeDirectoryShape(MachOSigner::ComputeCodeDirectory(path, "test-identity"), bytes.size(),
                                    "test-identity");
            RequireThrowsText([&] { MachOSigner::EmbedSignature(path, EmbeddedBlob()); },
                              "No space for new load command");
            RequireUnchanged(path, bytes);
        }
    }
}

TEST_CASE("ok: synthetic Mach-O image without a signature", "[MalformedMachO][ok]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    const Image image = MakeImage();
    const std::string reads = WriteScratch(scratch, "genuine.bin", image.genuine);
    const std::string path = WriteScratch(scratch, "input.bin", image.legacy);

    CHECK(MachOParser::ListDependencies(reads) == std::vector<std::string>{kDylibName});
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());

    const auto result = MachOSigner::ComputeCodeDirectory(path, "test-identity");
    CheckCodeDirectoryShape(result, kUnsignedSize, "test-identity");
    CHECK(GetBE64(result.code_directory, 64) == 0);    // execSegBase: __TEXT file offset
    CHECK(GetBE64(result.code_directory, 72) == 4096); // execSegLimit: __TEXT file size
    CHECK(MachOSigner::ComputeCodeDirectory(path, "test-identity").cd_hash == result.cd_hash);

    SECTION("EmbedSignature, then the signature reads back")
    {
        MachOSigner::EmbedSignature(path, EmbeddedBlob());
        const Bytes embedded = seedtest::malformed::ReadAll(path);
        CHECK(embedded.size() == kUnsignedSize + EmbeddedBlob().size());
        CHECK(GetBE32(embedded, kNcmdsField) == 4);
        CHECK(GetBE32(embedded, kSizeofcmdsField) == kCommandsSize + 16);
        CHECK(GetBE32(embedded, kSignatureCmd) == kLcCodeSignature);
        CHECK(GetBE32(embedded, kSignatureCmd + kSignatureDataoffField) == kUnsignedSize);
        CHECK(GetBE32(embedded, kSignatureCmd + kSignatureDatasizeField) == EmbeddedBlob().size());
        // __LINKEDIT grows to cover the signature.
        CHECK(GetBE64(embedded, kLinkeditCmd + 48) == embedded.size() - kLinkeditOffset);
        CHECK(MachOSigner::HasEmbeddedSignature(path));
        const auto extracted = MachOSigner::ExtractSignature(path);
        REQUIRE(extracted.has_value());
        CHECK(*extracted == EmbeddedBlob());
    }
}

TEST_CASE("ok: synthetic Mach-O image with a signature", "[MalformedMachO][ok]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    const Image image = MakeImage(true);
    const std::string reads = WriteScratch(scratch, "genuine.bin", image.genuine);
    const std::string path = WriteScratch(scratch, "input.bin", image.legacy);

    CHECK(MachOParser::ListDependencies(reads) == std::vector<std::string>{kDylibName});
    CHECK(MachOSigner::HasEmbeddedSignature(path));
    const auto extracted = MachOSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    CHECK(*extracted == SignatureBytes(image.legacy));

    // The code directory covers the file up to the signature data.
    CheckCodeDirectoryShape(MachOSigner::ComputeCodeDirectory(path, "test-identity"), kSignatureOffset,
                            "test-identity");
}

TEST_CASE("ok: LC_CODE_SIGNATURE with datasize 0", "[MalformedMachO][ok]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    for(const std::uint32_t dataoff : {0u, static_cast<std::uint32_t>(kUnsignedSize)})
    {
        DYNAMIC_SECTION("dataoff " << dataoff)
        {
            Image image = MakeImage(true);
            image.Patch32(kSignatureCmd + kSignatureDataoffField, dataoff);
            image.Patch32(kSignatureCmd + kSignatureDatasizeField, 0);
            const std::string reads = WriteScratch(scratch, "genuine.bin", image.genuine);
            const std::string path = WriteScratch(scratch, "input.bin", image.legacy);
            // The command exists, so a signature is present, but there are no data to extract.
            CHECK(MachOSigner::HasEmbeddedSignature(path));
            CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
            CHECK(MachOParser::ListDependencies(reads) == std::vector<std::string>{kDylibName});
            if(dataoff != 0)
            {
                CheckCodeDirectoryShape(MachOSigner::ComputeCodeDirectory(path, "test-identity"), dataoff,
                                        "test-identity");
            }
        }
    }
}

TEST_CASE("ok: a load command header past the end of the file", "[MalformedMachO][ok]")
{
    // The file ends inside the header of the second command. The signer ends
    // its walk there, as it does today (not an error). The parser rejects the
    // file because the load-command area the header announces is not all
    // there.
    seedtest::ScratchDir scratch("malformed-macho");
    const Image image = MakeImage().Cut(kLinkeditCmd + 4);
    const std::string path = WriteScratch(scratch, "input.bin", image.legacy);
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
    const auto result = MachOSigner::ComputeCodeDirectory(path, "test-identity");
    CheckCodeDirectoryShape(result, image.size(), "test-identity");
    CHECK(GetBE64(result.code_directory, 72) == 4096);
    RequireAllRejected(image, {ListOp()}, "load commands");
}

// ---------------------------------------------------------------------------
// From the review
// ---------------------------------------------------------------------------

TEST_CASE("review: Mach-O signature past the end", "[MalformedMachO][review]")
{
    // The signature command names data (offset 5000, size 300) in a file of
    // 4160 bytes. The digest must not be computed over bytes that do not exist.
    Image image = MakeImage(true);
    image.Patch32(kSignatureCmd + kSignatureDataoffField, 5000);
    image.Patch32(kSignatureCmd + kSignatureDatasizeField, 300);
    RequireAllRejected(image, ProgramOps(), "extends past the end of the file");
}

TEST_CASE("review: Mach-O segment name outside the file", "[MalformedMachO][review]")
{
    // The file ends 4 bytes into the __TEXT segment command: its 8-byte header
    // is inside the file, its name is not.
    const Image image = MakeImage().Cut(kTextCmd + 12);
    RequireAllRejected(image, SignerOps(), "extends past the end of the file");
    RequireAllRejected(image, {ListOp()}, "load commands");
}

TEST_CASE("review: Mach-O cmdsize 0", "[MalformedMachO][review]")
{
    // The second command has size 0 and the header claims 4,294,967,295
    // commands: the walk must not stand still for 2^32 steps.
    Image image = MakeImage();
    image.Patch32(kLinkeditCmd + kCmdSizeField, 0);
    image.Patch32(kNcmdsField, 0xFFFFFFFFu);
    RequireAllRejected(image, ProgramOps(), "load command 1 size 0");
}

TEST_CASE("f18: fat slice count", "[MalformedMachO][review]")
{
    // The slice count of the universal sample is 0xFFFFFFFF in the table's
    // byte order (big-endian). It must be rejected before the 100 GB
    // reservation the count would cause; a count no real file holds is
    // rejected as not being a Mach-O file.
    Bytes bytes = Sample("tiny-macho-universal");
    PatchBE<std::uint32_t>(bytes, 4, 0xFFFFFFFFu);
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "input.bin", bytes);
    seedtest::malformed::RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, kProgramFormat,
                                         "not a Mach-O", bytes.size());
}

// ---------------------------------------------------------------------------
// Edge-case families: the load-command walk
// ---------------------------------------------------------------------------

TEST_CASE("edge: Mach-O cmdsize 4 or not a multiple of 4", "[MalformedMachO][edge]")
{
    for(const std::uint32_t size : {1u, 4u, 7u, 10u, 54u})
    {
        DYNAMIC_SECTION("second command size " << size)
        {
            Image image = MakeImage();
            image.Patch32(kLinkeditCmd + kCmdSizeField, size);
            RequireAllRejected(image, ProgramOps(), "load command 1 size " + std::to_string(size));
        }
    }
}

TEST_CASE("edge: Mach-O first command cmdsize 0", "[MalformedMachO][edge]")
{
    Image image = MakeImage();
    image.Patch32(kTextCmd + kCmdSizeField, 0);
    image.Patch32(kNcmdsField, 0xFFFFFFFFu);
    RequireAllRejected(image, ProgramOps(), "load command 0 size 0");
}

TEST_CASE("edge: Mach-O embed with the new command past the end of the file", "[MalformedMachO][edge]")
{
    // header size + sizeofcmds + 16 is compared with the size of the file as
    // it is, not with the size after the signature has been appended.
    const Image image = MakeImage().Cut(kLinkeditCmd + 4);
    RequireAllRejected(image, {EmbedOp()}, "No space for new load command");
}

TEST_CASE("edge: Mach-O sizeofcmds 0xFFFFFFFF", "[MalformedMachO][edge]")
{
    // Of the signer operations only EmbedSignature uses sizeofcmds; header size
    // + sizeofcmds + 16 does not fit the file (and wraps in 32-bit
    // arithmetic). The parser rejects the area that does not fit.
    for(const std::uint32_t size : {0xFFFFFFFFu, 0xFFFFFFF0u, 0xFFFFFFEFu, 0x80000000u})
    {
        DYNAMIC_SECTION("sizeofcmds " << size)
        {
            Image image = MakeImage();
            image.Patch32(kSizeofcmdsField, size);
            RequireAllRejected(image, {EmbedOp()}, "No space for new load command");
            RequireAllRejected(image, {ListOp()}, "load commands");
            RequireUnsignedQueriesAnswer(image);
        }
    }
}

// ---------------------------------------------------------------------------
// Edge-case families: segment, dylib and signature commands
// ---------------------------------------------------------------------------

TEST_CASE("edge: Mach-O LC_SEGMENT_64 smaller than 72 bytes", "[MalformedMachO][edge]")
{
    for(const std::uint64_t at : {kTextCmd, kLinkeditCmd})
    {
        DYNAMIC_SECTION("segment command at " << at)
        {
            Image image = MakeImage();
            image.Patch32(at + kCmdSizeField, 24);
            RequireAllRejected(image, ProgramOps(), "LC_SEGMENT_64");
        }
    }
}

TEST_CASE("edge: Mach-O dylib name offset past the command", "[MalformedMachO][edge]")
{
    for(const std::uint32_t offset : {static_cast<std::uint32_t>(kDylibCmdSize),
                                      static_cast<std::uint32_t>(kDylibCmdSize + 8), 0xFFFFFFFFu})
    {
        DYNAMIC_SECTION("name offset " << offset)
        {
            Image image = MakeImage();
            image.Patch32(kDylibCmd + kDylibNameOffsetField, offset);
            RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
        }
    }
}

TEST_CASE("edge: Mach-O dylib name with no NUL inside its command", "[MalformedMachO][edge]")
{
    // The name fills the command; zeros follow it in the file, so reading to
    // the next NUL would run into the following bytes.
    Image image = MakeImage();
    image.Fill(kDylibCmd + kDylibNameField, kDylibCmd + kDylibCmdSize, 'A');
    RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
}

TEST_CASE("edge: Mach-O dylib command smaller than 24 bytes", "[MalformedMachO][edge]")
{
    Image image = MakeImage();
    image.Patch32(kDylibCmd + kCmdSizeField, 16);
    RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
}

TEST_CASE("edge: Mach-O two LC_CODE_SIGNATURE commands", "[MalformedMachO][edge]")
{
    Image image = MakeImage(true);
    PutSignatureCommand(image.genuine, Conv::Genuine, kSignatureCmd + kSignatureCmdSize,
                        static_cast<std::uint32_t>(kSignatureOffset), static_cast<std::uint32_t>(kSignatureSize));
    PutSignatureCommand(image.legacy, Conv::Legacy, kSignatureCmd + kSignatureCmdSize,
                        static_cast<std::uint32_t>(kSignatureOffset), static_cast<std::uint32_t>(kSignatureSize));
    image.Patch32(kNcmdsField, 5);
    image.Patch32(kSizeofcmdsField, static_cast<std::uint32_t>(kCommandsSize + 32));
    RequireAllRejected(image, ProgramOps(), "LC_CODE_SIGNATURE");
}

TEST_CASE("edge: Mach-O LC_CODE_SIGNATURE smaller than 16 bytes", "[MalformedMachO][edge]")
{
    Image image = MakeImage(true);
    image.Patch32(kSignatureCmd + kCmdSizeField, 8);
    RequireAllRejected(image, ProgramOps(), "LC_CODE_SIGNATURE");
}

TEST_CASE("edge: Mach-O signature dataoff 0xFFFFFFFF", "[MalformedMachO][edge]")
{
    for(const std::uint32_t datasize : {1u, 16u, 0xFFFFFFFFu})
    {
        DYNAMIC_SECTION("datasize " << datasize)
        {
            Image image = MakeImage(true);
            image.Patch32(kSignatureCmd + kSignatureDataoffField, 0xFFFFFFFFu);
            image.Patch32(kSignatureCmd + kSignatureDatasizeField, datasize);
            RequireAllRejected(image, ProgramOps(), "LC_CODE_SIGNATURE");
        }
    }
}

TEST_CASE("edge: Mach-O signature data past the end by one byte", "[MalformedMachO][edge]")
{
    Image image = MakeImage(true);
    image.Patch32(kSignatureCmd + kSignatureDatasizeField,
                           static_cast<std::uint32_t>(kSignatureSize + 1));
    RequireAllRejected(image, ProgramOps(), "LC_CODE_SIGNATURE");
}

TEST_CASE("edge: Mach-O signature data inside the load-command area", "[MalformedMachO][edge]")
{
    for(const std::uint32_t dataoff : {32u, 100u, 231u})
    {
        DYNAMIC_SECTION("dataoff " << dataoff)
        {
            Image image = MakeImage(true);
            image.Patch32(kSignatureCmd + kSignatureDataoffField, dataoff);
            image.Patch32(kSignatureCmd + kSignatureDatasizeField, 16);
            RequireAllRejected(image, ProgramOps(), "LC_CODE_SIGNATURE");
        }
    }
}

TEST_CASE("edge: Mach-O __LINKEDIT file offset past the file", "[MalformedMachO][edge]")
{
    // Of the signer operations only EmbedSignature uses the offset (to size
    // the segment after the signature is appended); the parser checks that the
    // segment lies inside the file.
    for(const std::uint64_t offset : {kUnsignedSize + 1, std::uint64_t{0x100000}, std::uint64_t{0xFFFFFFFFFFFFFFFF}})
    {
        DYNAMIC_SECTION("fileoff " << offset)
        {
            Image image = MakeImage();
            image.Patch64(kLinkeditCmd + kSegmentFileoffField, offset);
            RequireAllRejected(image, {ListOp(), EmbedOp()}, "__LINKEDIT");
            RequireUnsignedQueriesAnswer(image);
        }
    }
}

// ---------------------------------------------------------------------------
// Edge-case families: universal files
// ---------------------------------------------------------------------------

TEST_CASE("edge: Mach-O fat slice count larger than the file allows", "[MalformedMachO][edge]")
{
    // The table of 2 entries (48 bytes) is in the file and 8 bytes follow it.
    // A count that the table cannot hold is a truncated table (3 entries need
    // 68 bytes); a count above the bound no real file reaches is rejected as
    // not being a Mach-O file before the table is looked at.
    Bytes fat = MakeFat(kGoodSlices, 48 + 8);
    SECTION("nfat_arch 3")
    {
        PatchBE<std::uint32_t>(fat, 4, 3);
        RequireAllRejected(fat, {SlicesOp(), ListOp()}, "Truncated fat_arch entry table");
    }
    for(const std::uint32_t count : {257u, 1000u, 0xFFFFFFFFu})
    {
        DYNAMIC_SECTION("nfat_arch " << count)
        {
            PatchBE<std::uint32_t>(fat, 4, count);
            RequireAllRejected(fat, {SlicesOp(), ListOp()}, "not a Mach-O");
        }
    }
    SECTION("nfat_arch 0")
    {
        PatchBE<std::uint32_t>(fat, 4, 0);
        RequireAllRejected(fat, {SlicesOp(), ListOp()}, "not a Mach-O");
    }
}

TEST_CASE("edge: Mach-O fat slice covering the whole file", "[MalformedMachO][edge]")
{
    // The second slice starts at offset 0, inside the slice table.
    auto slices = kGoodSlices;
    slices[1] = {0x0100000C, 0, 0, static_cast<std::uint32_t>(kFatSize), 12};
    RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "header table");
}

TEST_CASE("edge: Mach-O fat slice starting inside the header table", "[MalformedMachO][edge]")
{
    for(const std::uint32_t offset : {1u, 12u, 27u})
    {
        DYNAMIC_SECTION("offset " << offset)
        {
            // Two entries: the table is 8 + 2 * 20 = 48 bytes.
            auto slices = kGoodSlices;
            slices[1].offset = offset;
            slices[1].size = 100;
            RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "header table");
        }
    }
}

TEST_CASE("edge: Mach-O fat overlapping slices", "[MalformedMachO][edge]")
{
    SECTION("the second starts inside the first")
    {
        auto slices = kGoodSlices;
        slices[1].offset = 6000;
        RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "overlap");
    }
    SECTION("two identical slices")
    {
        auto slices = kGoodSlices;
        slices[1] = slices[0];
        RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "overlap");
    }
    SECTION("listed in the reverse order of their offsets")
    {
        auto slices = kGoodSlices;
        slices[0].offset = 12288;
        slices[1].offset = 4096;
        slices[1].size = 9000;
        RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "overlap");
    }
}

TEST_CASE("edge: Mach-O fat slice past the end", "[MalformedMachO][edge]")
{
    SECTION("by one byte")
    {
        auto slices = kGoodSlices;
        slices[1].size = static_cast<std::uint32_t>(kSliceSize + 1);
        slices[1].offset = static_cast<std::uint32_t>(kFatSize - kSliceSize);
        RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "past the end");
    }
    SECTION("offset and size near the largest 32-bit value")
    {
        auto slices = kGoodSlices;
        slices[1].offset = 0xFFFFFFFFu;
        slices[1].size = 0xFFFFFFFFu;
        RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "past the end");
    }
    SECTION("offset past the file")
    {
        auto slices = kGoodSlices;
        slices[1].offset = static_cast<std::uint32_t>(kFatSize + 1);
        slices[1].size = 1;
        RequireAllRejected(MakeFat(slices), {SlicesOp(), ListOp()}, "past the end");
    }
}

TEST_CASE("edge: Mach-O Java class header", "[MalformedMachO][edge]")
{
    // CA FE BA BE followed by a Java version number and zeros: the four bytes
    // after the magic are not a slice count, and no slice starts with a Mach-O
    // magic.
    const Bytes java{0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x34};
    Bytes padded = java;
    padded.resize(2048, 0);
    RequireAllRejected(padded, {SlicesOp(), ListOp()}, "not a Mach-O");
}

TEST_CASE("edge: Mach-O fat first slice that is not a Mach-O program", "[MalformedMachO][edge]")
{
    // A Java class file starts with the universal magic. The table here has a
    // plausible count, but the first slice does not start with a Mach-O magic.
    Bytes fat = MakeFat(kGoodSlices);
    fat[4096] = 0;
    RequireAllRejected(fat, {SlicesOp(), ListOp()}, "not a Mach-O");
}

// ---------------------------------------------------------------------------
// Edge-case families: short files and truncations
// ---------------------------------------------------------------------------

TEST_CASE("edge: Mach-O zero-length and 3-byte files", "[MalformedMachO][edge]")
{
    // The format queries answer; every other operation is rejected.
    const std::vector<std::pair<std::string, Bytes>> inputs = {
        {"zero-length file", Bytes{}}, {"three bytes", Bytes{0xCF, 0xFA, 0xED}}};
    seedtest::ScratchDir scratch("malformed-macho");
    for(const auto &[name, input] : inputs)
    {
        DYNAMIC_SECTION(name)
        {
            const std::string path = WriteScratch(scratch, "queries.bin", input);
            CHECK(MachOParser::DetectFormat(path) == MachOParser::Format::NotMachO);
            CHECK_FALSE(MachOParser::IsMachO(path));
            CHECK_FALSE(MachOParser::IsFatBinary(path));
            RequireAllRejected(input, {SlicesOp(), ListOp(), ComputeOp(), ExtractOp(), HasOp(), EmbedOp()},
                               "too small");
        }
    }
}

TEST_CASE("edge: Mach-O truncated inside the header", "[MalformedMachO][edge]")
{
    // 18 bytes: the magic is there (so the format queries answer), the
    // command count at offset 16 is not.
    const Image image = MakeImage().Cut(18);
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "queries.bin", image.genuine);
    CHECK(MachOParser::DetectFormat(path) == MachOParser::Format::MachO64);
    CHECK_FALSE(MachOParser::IsFatBinary(path));
    RequireAllRejected(image, ProgramOps(), "header");
}

TEST_CASE("edge: Mach-O truncated inside a command", "[MalformedMachO][edge]")
{
    // The file ends 40 bytes into the __LINKEDIT segment command: its header
    // is inside the file, the command is not.
    const Image image = MakeImage().Cut(kLinkeditCmd + 40);
    RequireAllRejected(image, SignerOps(), "LC_SEGMENT_64");
    RequireAllRejected(image, {ListOp()}, "load commands");
}

TEST_CASE("edge: Mach-O truncated inside a dylib name", "[MalformedMachO][edge]")
{
    const Image image = MakeImage().Cut(kDylibCmd + kDylibNameField + 6);
    RequireAllRejected(image, {ListOp()}, "load commands");
}

TEST_CASE("edge: Mach-O truncated inside the signature data", "[MalformedMachO][edge]")
{
    const Image image = MakeImage(true).Cut(kSignatureOffset + 10);
    RequireAllRejected(image, ProgramOps(), "LC_CODE_SIGNATURE");
}

TEST_CASE("edge: Mach-O oversized SuperBlob size check", "[MalformedMachO][edge]")
{
    // The check takes a length, so no 4 GiB buffer is allocated.
    const std::uint64_t largest = std::numeric_limits<std::uint32_t>::max();
    CHECK_NOTHROW(seed::internal::CheckMachOSuperBlobSize(0));
    CHECK_NOTHROW(seed::internal::CheckMachOSuperBlobSize(largest));
    seedtest::malformed::RequireRejected([&] { seed::internal::CheckMachOSuperBlobSize(largest + 1); },
                                         kProgramFormat, "SuperBlob");
    seedtest::malformed::RequireRejected(
        [&] { seed::internal::CheckMachOSuperBlobSize(std::numeric_limits<std::uint64_t>::max()); },
        kProgramFormat, "SuperBlob");
}

// ===========================================================================
// Library reference commands of every kind: an unterminated name
// ===========================================================================

namespace {

namespace dep = seedtest::dep;

struct ReferenceKind
{
    std::uint32_t kind;
    const char *command;
};

// Each kind with the name the message gives its command.
const std::vector<ReferenceKind> &ReferenceKinds()
{
    static const std::vector<ReferenceKind> kinds = {
        {dep::kLoadDylib, "LC_LOAD_DYLIB"},
        {dep::kLoadWeakDylib, "LC_LOAD_WEAK_DYLIB"},
        {dep::kReexportDylib, "LC_REEXPORT_DYLIB"},
        {dep::kLazyLoadDylib, "LC_LAZY_LOAD_DYLIB"},
        {dep::kLoadUpwardDylib, "LC_LOAD_UPWARD_DYLIB"},
    };
    return kinds;
}

// A seven-character name fills a 32-byte command exactly with its NUL, so
// replacing that NUL leaves a name that runs to the end of its command.
constexpr std::uint64_t kReferenceCommandSize = 32;
constexpr std::uint64_t kReferenceFirstCommand = 32;
constexpr const char *kSevenCharacters = "/lib/ab";

Bytes UnterminatedAt(const std::vector<dep::MachOReference> &references, std::size_t index)
{
    Bytes image = dep::MachOReferencing(references, dep::MachOFields::LittleAfterMagic);
    const std::uint64_t last_byte =
        kReferenceFirstCommand + (index + 1) * kReferenceCommandSize - 1;
    REQUIRE(image.at(last_byte) == 0);
    image.at(last_byte) = 'A';
    return image;
}

}

TEST_CASE("f18: a library name not ended inside its command is rejected for every kind",
          "[MalformedMachO][f18]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    for(const ReferenceKind &kind : ReferenceKinds())
    {
        DYNAMIC_SECTION(kind.command << " as the only command")
        {
            const Bytes image = UnterminatedAt({{kind.kind, kSevenCharacters}}, 0);
            const std::string path = WriteScratch(scratch, "input.bin", image);
            RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, kProgramFormat,
                            kind.command, image.size());
            RequireUnchanged(path, image);
        }
        DYNAMIC_SECTION(kind.command << " after a well-formed command")
        {
            const Bytes image = UnterminatedAt(
                {{dep::kLoadDylib, "/lib/ok"}, {kind.kind, kSevenCharacters}}, 1);
            const std::string path = WriteScratch(scratch, "input.bin", image);
            // The message names the zero-based index of the faulty command.
            RequireRejected([&] { (void)MachOParser::ListDependencies(path); }, kProgramFormat,
                            std::string(kind.command) + " command 1", image.size());
        }
    }
}

TEST_CASE("f18: no empty library name is returned", "[MalformedMachO][f18]")
{
    seedtest::ScratchDir scratch("malformed-macho");

    SECTION("well-formed references of the five kinds are all non-empty")
    {
        std::vector<dep::MachOReference> references;
        std::size_t number = 0;
        for(const ReferenceKind &kind : ReferenceKinds())
        {
            references.push_back({kind.kind, "/lib/n" + std::to_string(number++)});
        }
        const std::string path =
            WriteScratch(scratch, "input.bin",
                         dep::MachOReferencing(references, dep::MachOFields::LittleAfterMagic));
        const auto names = MachOParser::ListDependencies(path);
        CHECK(names.size() == 5);
        for(const std::string &name : names)
        {
            CHECK_FALSE(name.empty());
        }
    }

    SECTION("a name that is not ended is rejected, never returned as an empty string")
    {
        for(const ReferenceKind &kind : ReferenceKinds())
        {
            DYNAMIC_SECTION(kind.command)
            {
                const Bytes image = UnterminatedAt({{kind.kind, kSevenCharacters}}, 0);
                const std::string path = WriteScratch(scratch, "input.bin", image);
                std::vector<std::string> names;
                bool rejected = false;
                try
                {
                    names = MachOParser::ListDependencies(path);
                }
                catch(const std::runtime_error &)
                {
                    rejected = true;
                }
                CHECK(rejected);
                CHECK(std::count(names.begin(), names.end(), std::string()) == 0);
            }
        }
    }
}
