// Malformed and edge-case Mac programs and Mac signature data, through
// MachOParser, MachOSigner (file operations) and the two SuperBlob helpers
// MachOSigner::ExtractCmsFromSuperBlob and
// MachOSigner::ExtractCodeDirectoryFromSuperBlob.
//
// Test case names start with their origin: "ok:" for a well-formed or
// legal-but-unusual input with today's result, "review:" and "f18:" for the
// review's input, and "edge:" for an edge-case family.
//
// Byte order. The library reads every header field of a program whose first
// four bytes are CF FA ED FE (a real little-endian 64-bit Mach-O) as
// big-endian, and every field of a universal file whose first four bytes are
// CA FE BA BE as little-endian, so it misreads the genuine samples. The synthetic images below are written in
// the order the code reads today, so that their commands are seen as they are
// meant; the genuine samples are seen as today's misreading, which ends the
// load-command walk at the first command. When the byte order is corrected
// the image builders and the expectations that name misread values change
// with it. A load command whose 8-byte header does not fit inside the file
// ends the walk (it is not an error); that rule stays until then.
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

#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>

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

void PutSegment(Bytes &image, std::uint64_t at, const std::string &name, std::uint64_t vmaddr,
                std::uint64_t vmsize, std::uint64_t fileoff, std::uint64_t filesize)
{
    PatchBE<std::uint32_t>(image, at, kLcSegment64);
    PatchBE<std::uint32_t>(image, at + 4, kSegmentCmdSize);
    for(std::size_t i = 0; i < name.size(); ++i)
    {
        image[at + kSegmentNameField + i] = static_cast<std::uint8_t>(name[i]);
    }
    PatchBE<std::uint64_t>(image, at + 24, vmaddr);
    PatchBE<std::uint64_t>(image, at + 32, vmsize);
    PatchBE<std::uint64_t>(image, at + 40, fileoff);
    PatchBE<std::uint64_t>(image, at + 48, filesize);
    PatchBE<std::uint32_t>(image, at + 56, 5);
    PatchBE<std::uint32_t>(image, at + 60, 5);
}

void PutSignatureCommand(Bytes &image, std::uint64_t at, std::uint32_t dataoff, std::uint32_t datasize)
{
    PatchBE<std::uint32_t>(image, at, kLcCodeSignature);
    PatchBE<std::uint32_t>(image, at + 4, kSignatureCmdSize);
    PatchBE<std::uint32_t>(image, at + kSignatureDataoffField, dataoff);
    PatchBE<std::uint32_t>(image, at + kSignatureDatasizeField, datasize);
}

Bytes MakeImage(bool with_signature = false)
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
    PatchBE<std::uint32_t>(image, 4, 0x01000007);
    PatchBE<std::uint32_t>(image, 8, 3);
    PatchBE<std::uint32_t>(image, 12, 2);
    PatchBE<std::uint32_t>(image, kNcmdsField, with_signature ? 4 : 3);
    PatchBE<std::uint32_t>(image, kSizeofcmdsField,
                           static_cast<std::uint32_t>(kCommandsSize + (with_signature ? 16 : 0)));
    PatchBE<std::uint32_t>(image, 24, 0x00200085);

    PutSegment(image, kTextCmd, "__TEXT", 0, 4096, 0, 4096);
    PutSegment(image, kLinkeditCmd, "__LINKEDIT", 4096, 4096, kLinkeditOffset, 64);

    PatchBE<std::uint32_t>(image, kDylibCmd, kLcLoadDylib);
    PatchBE<std::uint32_t>(image, kDylibCmd + 4, kDylibCmdSize);
    PatchBE<std::uint32_t>(image, kDylibCmd + kDylibNameOffsetField, kDylibNameField);
    PatchBE<std::uint32_t>(image, kDylibCmd + 12, 2);
    PatchBE<std::uint32_t>(image, kDylibCmd + 16, 0x10000);
    PatchBE<std::uint32_t>(image, kDylibCmd + 20, 0x10000);
    const std::string name = kDylibName;
    for(std::size_t i = 0; i < name.size(); ++i)
    {
        image[kDylibCmd + kDylibNameField + i] = static_cast<std::uint8_t>(name[i]);
    }

    if(with_signature)
    {
        PutSignatureCommand(image, kSignatureCmd, static_cast<std::uint32_t>(kSignatureOffset),
                            static_cast<std::uint32_t>(kSignatureSize));
        for(std::uint64_t i = kSignatureOffset; i < kUnsignedSize; ++i)
        {
            image[i] = 0x5A;
        }
    }
    return image;
}

std::vector<std::uint8_t> SignatureBytes(const Bytes &image)
{
    return Bytes(image.begin() + static_cast<std::ptrdiff_t>(kSignatureOffset),
                 image.begin() + static_cast<std::ptrdiff_t>(kSignatureOffset + kSignatureSize));
}

// A universal file in the order the code reads (little-endian fields behind
// the CA FE BA BE bytes).
struct FatEntry
{
    std::uint32_t cpu_type;
    std::uint32_t cpu_subtype;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t align;
};

constexpr std::uint64_t kFatSize = 12288;

const std::vector<FatEntry> kGoodSlices = {{0x01000007, 3, 4096, 4096, 12},
                                           {0x0100000C, 0, 8192, 4096, 14}};

Bytes MakeFat(const std::vector<FatEntry> &entries, std::uint64_t total_size = kFatSize)
{
    Bytes fat(total_size, 0);
    fat[0] = 0xCA;
    fat[1] = 0xFE;
    fat[2] = 0xBA;
    fat[3] = 0xBE;
    PatchLE<std::uint32_t>(fat, 4, static_cast<std::uint32_t>(entries.size()));
    for(std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::uint64_t at = 8 + 20 * i;
        PatchLE<std::uint32_t>(fat, at, entries[i].cpu_type);
        PatchLE<std::uint32_t>(fat, at + 4, entries[i].cpu_subtype);
        PatchLE<std::uint32_t>(fat, at + 8, entries[i].offset);
        PatchLE<std::uint32_t>(fat, at + 12, entries[i].size);
        PatchLE<std::uint32_t>(fat, at + 16, entries[i].align);
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
};

Operation ListOp()
{
    return {"MachOParser::ListDependencies",
            [](const std::string &path) { (void)MachOParser::ListDependencies(path); }, false};
}

Operation SlicesOp()
{
    return {"MachOParser::GetArchSlices",
            [](const std::string &path) { (void)MachOParser::GetArchSlices(path); }, false};
}

Operation ComputeOp()
{
    return {"MachOSigner::ComputeCodeDirectory",
            [](const std::string &path) { (void)MachOSigner::ComputeCodeDirectory(path, "test-identity"); },
            false};
}

Operation ExtractOp()
{
    return {"MachOSigner::ExtractSignature",
            [](const std::string &path) { (void)MachOSigner::ExtractSignature(path); }, false};
}

Operation HasOp()
{
    return {"MachOSigner::HasEmbeddedSignature",
            [](const std::string &path) { (void)MachOSigner::HasEmbeddedSignature(path); }, false};
}

Operation EmbedOp()
{
    return {"MachOSigner::EmbedSignature",
            [](const std::string &path) { MachOSigner::EmbedSignature(path, EmbeddedBlob()); }, true};
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

// Each operation runs on a fresh copy of the input. It must be rejected with
// "Mach-O: <keyword>" within the time and heap limits, and a rejected embed
// must leave the file unchanged.
void RequireAllRejected(const Bytes &input, const std::vector<Operation> &ops, const std::string &keyword)
{
    seedtest::ScratchDir scratch("malformed-macho");
    for(const auto &op : ops)
    {
        DYNAMIC_SECTION(op.name)
        {
            const std::string path = WriteScratch(scratch, "input.bin", input);
            seedtest::malformed::RequireRejected([&] { op.run(path); }, kProgramFormat, keyword, input.size(),
                                                 EmbeddedBlob().size());
            if(op.modifies)
            {
                RequireUnchanged(path, input);
            }
        }
    }
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

// ListDependencies of an image that has no faulty dylib command.
void RequireListAnswers(const Bytes &input, const std::vector<std::string> &expected)
{
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "input.bin", input);
    CHECK(MachOParser::ListDependencies(path) == expected);
}

// The signer's read-only operations on an image whose layout parse accepts it
// and that has no signature command.
void RequireUnsignedQueriesAnswer(const Bytes &input)
{
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "input.bin", input);
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
        {"synthetic image", MakeImage(), MachOParser::Format::MachO64},
        {"synthetic signed image", MakeImage(true), MachOParser::Format::MachO64},
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
    // One slice covering the whole file; the cpu fields are read as the code
    // reads them today (big-endian).
    seedtest::ScratchDir scratch("malformed-macho");
    for(const std::string name : {"tiny-macho-x86_64", "tiny-macho-arm64"})
    {
        DYNAMIC_SECTION(name)
        {
            const Bytes bytes = Sample(name);
            const auto slices = MachOParser::GetArchSlices(WriteScratch(scratch, "input.bin", bytes));
            REQUIRE(slices.size() == 1);
            CHECK(slices[0].cpu_type == GetBE32(bytes, 4));
            CHECK(slices[0].cpu_subtype == GetBE32(bytes, 8));
            CHECK(slices[0].offset == 0);
            CHECK(slices[0].size == bytes.size());
        }
    }
    DYNAMIC_SECTION("synthetic image")
    {
        const auto slices = MachOParser::GetArchSlices(WriteScratch(scratch, "input.bin", MakeImage()));
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

TEST_CASE("ok: tiny-macho-universal is rejected as today", "[MalformedMachO][ok]")
{
    // The genuine universal file is read in the wrong byte order: its slice
    // table is taken to hold 33,554,432 entries (a known gap).
    seedtest::ScratchDir scratch("malformed-macho");
    const Bytes bytes = Sample("tiny-macho-universal");
    const std::string path = WriteScratch(scratch, "input.bin", bytes);
    RequireThrowsText([&] { (void)MachOParser::GetArchSlices(path); }, "Truncated fat_arch entry");
    for(const auto &op : {ListOp(), ComputeOp(), ExtractOp(), HasOp(), EmbedOp()})
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
    // Today the walk ends at the first command (its misread size is about
    // 1.2 GB), so no dependency, segment or signature is seen.
    seedtest::ScratchDir scratch("malformed-macho");
    for(const std::string name : {"tiny-macho-x86_64", "tiny-macho-arm64"})
    {
        DYNAMIC_SECTION(name)
        {
            const Bytes bytes = Sample(name);
            const std::string path = WriteScratch(scratch, "input.bin", bytes);
            CHECK(MachOParser::ListDependencies(path).empty());
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
    const Bytes image = MakeImage();
    const std::string path = WriteScratch(scratch, "input.bin", image);

    CHECK(MachOParser::ListDependencies(path) == std::vector<std::string>{kDylibName});
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
        CHECK(MachOParser::ListDependencies(path) == std::vector<std::string>{kDylibName});
    }
}

TEST_CASE("ok: synthetic Mach-O image with a signature", "[MalformedMachO][ok]")
{
    seedtest::ScratchDir scratch("malformed-macho");
    const Bytes image = MakeImage(true);
    const std::string path = WriteScratch(scratch, "input.bin", image);

    CHECK(MachOParser::ListDependencies(path) == std::vector<std::string>{kDylibName});
    CHECK(MachOSigner::HasEmbeddedSignature(path));
    const auto extracted = MachOSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    CHECK(*extracted == SignatureBytes(image));

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
            Bytes image = MakeImage(true);
            PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDataoffField, dataoff);
            PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDatasizeField, 0);
            const std::string path = WriteScratch(scratch, "input.bin", image);
            // The command exists, so a signature is present, but there are no data to extract.
            CHECK(MachOSigner::HasEmbeddedSignature(path));
            CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
            CHECK(MachOParser::ListDependencies(path) == std::vector<std::string>{kDylibName});
            if(dataoff != 0)
            {
                CheckCodeDirectoryShape(MachOSigner::ComputeCodeDirectory(path, "test-identity"), dataoff,
                                        "test-identity");
            }
        }
    }
}

TEST_CASE("ok: a load command header past the end of the file ends the walk", "[MalformedMachO][ok]")
{
    // The file ends inside the header of the second command. That is not an
    // error: the walk stops there, as it does for the genuine samples.
    seedtest::ScratchDir scratch("malformed-macho");
    const Bytes image = Truncate(MakeImage(), kLinkeditCmd + 4);
    const std::string path = WriteScratch(scratch, "input.bin", image);
    CHECK(MachOParser::ListDependencies(path).empty());
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(MachOSigner::ExtractSignature(path).has_value());
    const auto result = MachOSigner::ComputeCodeDirectory(path, "test-identity");
    CheckCodeDirectoryShape(result, image.size(), "test-identity");
    CHECK(GetBE64(result.code_directory, 72) == 4096);
}

// ---------------------------------------------------------------------------
// From the review
// ---------------------------------------------------------------------------

TEST_CASE("review: Mach-O signature past the end", "[MalformedMachO][review]")
{
    // The signature command names data (offset 5000, size 300) in a file of
    // 4160 bytes. The digest must not be computed over bytes that do not exist.
    Bytes image = MakeImage(true);
    PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDataoffField, 5000);
    PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDatasizeField, 300);
    RequireAllRejected(image, SignerOps(), "extends past the end of the file");
    RequireListAnswers(image, {kDylibName});
}

TEST_CASE("review: Mach-O segment name outside the file", "[MalformedMachO][review]")
{
    // The file ends 4 bytes into the __TEXT segment command: its 8-byte header
    // is inside the file, its name is not.
    const Bytes image = Truncate(MakeImage(), kTextCmd + 12);
    RequireAllRejected(image, SignerOps(), "extends past the end of the file");
    RequireListAnswers(image, {});
}

TEST_CASE("review: Mach-O cmdsize 0", "[MalformedMachO][review]")
{
    // The second command has size 0 and the header claims 4,294,967,295
    // commands: the walk must not stand still for 2^32 steps.
    Bytes image = MakeImage();
    PatchBE<std::uint32_t>(image, kLinkeditCmd + kCmdSizeField, 0);
    PatchBE<std::uint32_t>(image, kNcmdsField, 0xFFFFFFFFu);
    RequireAllRejected(image, ProgramOps(), "load command 1 size 0");
}

TEST_CASE("f18: fat slice count", "[MalformedMachO][review]")
{
    // The slice count of the universal sample is 0xFFFFFFFF in the byte order
    // the code reads (little-endian). It must be rejected before the 100 GB
    // reservation the count would cause.
    Bytes bytes = Sample("tiny-macho-universal");
    PatchLE<std::uint32_t>(bytes, 4, 0xFFFFFFFFu);
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "input.bin", bytes);
    seedtest::malformed::RequireRejected([&] { (void)MachOParser::GetArchSlices(path); }, kProgramFormat,
                                         "Truncated fat_arch entry table", bytes.size());
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
            Bytes image = MakeImage();
            PatchBE<std::uint32_t>(image, kLinkeditCmd + kCmdSizeField, size);
            RequireAllRejected(image, ProgramOps(), "load command 1 size " + std::to_string(size));
        }
    }
}

TEST_CASE("edge: Mach-O first command cmdsize 0", "[MalformedMachO][edge]")
{
    Bytes image = MakeImage();
    PatchBE<std::uint32_t>(image, kTextCmd + kCmdSizeField, 0);
    PatchBE<std::uint32_t>(image, kNcmdsField, 0xFFFFFFFFu);
    RequireAllRejected(image, ProgramOps(), "load command 0 size 0");
}

TEST_CASE("edge: Mach-O embed with the new command past the end of the file", "[MalformedMachO][edge]")
{
    // header size + sizeofcmds + 16 is compared with the size of the file as
    // it is, not with the size after the signature has been appended.
    const Bytes image = Truncate(MakeImage(), kLinkeditCmd + 4);
    RequireAllRejected(image, {EmbedOp()}, "No space for new load command");
}

TEST_CASE("edge: Mach-O sizeofcmds 0xFFFFFFFF", "[MalformedMachO][edge]")
{
    // Only EmbedSignature uses sizeofcmds; header size + sizeofcmds + 16 does
    // not fit the file (and wraps in 32-bit arithmetic).
    for(const std::uint32_t size : {0xFFFFFFFFu, 0xFFFFFFF0u, 0xFFFFFFEFu, 0x80000000u})
    {
        DYNAMIC_SECTION("sizeofcmds " << size)
        {
            Bytes image = MakeImage();
            PatchBE<std::uint32_t>(image, kSizeofcmdsField, size);
            RequireAllRejected(image, {EmbedOp()}, "No space for new load command");
            RequireListAnswers(image, {kDylibName});
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
            Bytes image = MakeImage();
            PatchBE<std::uint32_t>(image, at + kCmdSizeField, 24);
            RequireAllRejected(image, SignerOps(), "LC_SEGMENT_64");
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
            Bytes image = MakeImage();
            PatchBE<std::uint32_t>(image, kDylibCmd + kDylibNameOffsetField, offset);
            RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
        }
    }
}

TEST_CASE("edge: Mach-O dylib name with no NUL inside its command", "[MalformedMachO][edge]")
{
    // The name fills the command; zeros follow it in the file, so reading to
    // the next NUL would run into the following bytes.
    Bytes image = MakeImage();
    for(std::uint64_t i = kDylibCmd + kDylibNameField; i < kDylibCmd + kDylibCmdSize; ++i)
    {
        image[i] = 'A';
    }
    RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
}

TEST_CASE("edge: Mach-O dylib command smaller than 24 bytes", "[MalformedMachO][edge]")
{
    Bytes image = MakeImage();
    PatchBE<std::uint32_t>(image, kDylibCmd + kCmdSizeField, 16);
    RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
}

TEST_CASE("edge: Mach-O two LC_CODE_SIGNATURE commands", "[MalformedMachO][edge]")
{
    Bytes image = MakeImage(true);
    PutSignatureCommand(image, kSignatureCmd + kSignatureCmdSize, static_cast<std::uint32_t>(kSignatureOffset),
                        static_cast<std::uint32_t>(kSignatureSize));
    PatchBE<std::uint32_t>(image, kNcmdsField, 5);
    PatchBE<std::uint32_t>(image, kSizeofcmdsField, static_cast<std::uint32_t>(kCommandsSize + 32));
    RequireAllRejected(image, SignerOps(), "LC_CODE_SIGNATURE");
    RequireListAnswers(image, {kDylibName});
}

TEST_CASE("edge: Mach-O LC_CODE_SIGNATURE smaller than 16 bytes", "[MalformedMachO][edge]")
{
    Bytes image = MakeImage(true);
    PatchBE<std::uint32_t>(image, kSignatureCmd + kCmdSizeField, 8);
    RequireAllRejected(image, SignerOps(), "LC_CODE_SIGNATURE");
}

TEST_CASE("edge: Mach-O signature dataoff 0xFFFFFFFF", "[MalformedMachO][edge]")
{
    for(const std::uint32_t datasize : {1u, 16u, 0xFFFFFFFFu})
    {
        DYNAMIC_SECTION("datasize " << datasize)
        {
            Bytes image = MakeImage(true);
            PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDataoffField, 0xFFFFFFFFu);
            PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDatasizeField, datasize);
            RequireAllRejected(image, SignerOps(), "LC_CODE_SIGNATURE");
            RequireListAnswers(image, {kDylibName});
        }
    }
}

TEST_CASE("edge: Mach-O signature data past the end by one byte", "[MalformedMachO][edge]")
{
    Bytes image = MakeImage(true);
    PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDatasizeField,
                           static_cast<std::uint32_t>(kSignatureSize + 1));
    RequireAllRejected(image, SignerOps(), "LC_CODE_SIGNATURE");
}

TEST_CASE("edge: Mach-O signature data inside the load-command area", "[MalformedMachO][edge]")
{
    for(const std::uint32_t dataoff : {32u, 100u, 231u})
    {
        DYNAMIC_SECTION("dataoff " << dataoff)
        {
            Bytes image = MakeImage(true);
            PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDataoffField, dataoff);
            PatchBE<std::uint32_t>(image, kSignatureCmd + kSignatureDatasizeField, 16);
            RequireAllRejected(image, SignerOps(), "LC_CODE_SIGNATURE");
            RequireListAnswers(image, {kDylibName});
        }
    }
}

TEST_CASE("edge: Mach-O __LINKEDIT file offset past the file", "[MalformedMachO][edge]")
{
    // Only EmbedSignature uses the offset (to size the segment after the
    // signature is appended).
    for(const std::uint64_t offset : {kUnsignedSize + 1, std::uint64_t{0x100000}, std::uint64_t{0xFFFFFFFFFFFFFFFF}})
    {
        DYNAMIC_SECTION("fileoff " << offset)
        {
            Bytes image = MakeImage();
            PatchBE<std::uint64_t>(image, kLinkeditCmd + kSegmentFileoffField, offset);
            RequireAllRejected(image, {EmbedOp()}, "__LINKEDIT");
            RequireListAnswers(image, {kDylibName});
            RequireUnsignedQueriesAnswer(image);
        }
    }
}

// ---------------------------------------------------------------------------
// Edge-case families: universal files
// ---------------------------------------------------------------------------

TEST_CASE("edge: Mach-O fat slice count larger than the file allows", "[MalformedMachO][edge]")
{
    for(const std::uint32_t count : {3u, 1000u, 0xFFFFFFFFu})
    {
        DYNAMIC_SECTION("nfat_arch " << count)
        {
            // The table of 2 entries (48 bytes) is in the file and 8 bytes follow it;
            // the count claims entries that do not fit (3 entries need 68 bytes).
            Bytes fat = MakeFat(kGoodSlices, 48 + 8);
            PatchLE<std::uint32_t>(fat, 4, count);
            RequireAllRejected(fat, {SlicesOp()}, "Truncated fat_arch entry table");
        }
    }
}

TEST_CASE("edge: Mach-O fat slice covering the whole file", "[MalformedMachO][edge]")
{
    // The slice starts at offset 0, inside the slice table.
    const Bytes fat = MakeFat({{0x01000007, 3, 0, static_cast<std::uint32_t>(kFatSize), 12}});
    RequireAllRejected(fat, {SlicesOp()}, "header table");
}

TEST_CASE("edge: Mach-O fat slice starting inside the header table", "[MalformedMachO][edge]")
{
    for(const std::uint32_t offset : {1u, 12u, 27u})
    {
        DYNAMIC_SECTION("offset " << offset)
        {
            // Two entries: the table is 8 + 2 * 20 = 48 bytes.
            auto slices = kGoodSlices;
            slices[0].offset = offset;
            slices[0].size = 100;
            RequireAllRejected(MakeFat(slices), {SlicesOp()}, "header table");
        }
    }
}

TEST_CASE("edge: Mach-O fat overlapping slices", "[MalformedMachO][edge]")
{
    SECTION("the second starts inside the first")
    {
        auto slices = kGoodSlices;
        slices[1].offset = 6000;
        RequireAllRejected(MakeFat(slices), {SlicesOp()}, "overlap");
    }
    SECTION("two identical slices")
    {
        auto slices = kGoodSlices;
        slices[1] = slices[0];
        RequireAllRejected(MakeFat(slices), {SlicesOp()}, "overlap");
    }
    SECTION("listed in the reverse order of their offsets")
    {
        auto slices = kGoodSlices;
        slices[0].offset = 8192;
        slices[0].size = 4096;
        slices[1].offset = 4096;
        slices[1].size = 5000;
        RequireAllRejected(MakeFat(slices), {SlicesOp()}, "overlap");
    }
}

TEST_CASE("edge: Mach-O fat slice past the end", "[MalformedMachO][edge]")
{
    SECTION("by one byte")
    {
        auto slices = kGoodSlices;
        slices[1].size = 4097;
        RequireAllRejected(MakeFat(slices), {SlicesOp()}, "past the end");
    }
    SECTION("offset and size near the largest 32-bit value")
    {
        auto slices = kGoodSlices;
        slices[1].offset = 0xFFFFFFFFu;
        slices[1].size = 0xFFFFFFFFu;
        RequireAllRejected(MakeFat(slices), {SlicesOp()}, "past the end");
    }
    SECTION("offset past the file")
    {
        auto slices = kGoodSlices;
        slices[1].offset = static_cast<std::uint32_t>(kFatSize + 1);
        slices[1].size = 1;
        RequireAllRejected(MakeFat(slices), {SlicesOp()}, "past the end");
    }
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
    const Bytes image = Truncate(MakeImage(), 18);
    seedtest::ScratchDir scratch("malformed-macho");
    const std::string path = WriteScratch(scratch, "queries.bin", image);
    CHECK(MachOParser::DetectFormat(path) == MachOParser::Format::MachO64);
    CHECK_FALSE(MachOParser::IsFatBinary(path));
    RequireAllRejected(image, ProgramOps(), "header");
}

TEST_CASE("edge: Mach-O truncated inside a command", "[MalformedMachO][edge]")
{
    // The file ends 40 bytes into the __LINKEDIT segment command: its header
    // is inside the file, the command is not.
    const Bytes image = Truncate(MakeImage(), kLinkeditCmd + 40);
    RequireAllRejected(image, SignerOps(), "LC_SEGMENT_64");
    RequireListAnswers(image, {});
}

TEST_CASE("edge: Mach-O truncated inside a dylib name", "[MalformedMachO][edge]")
{
    const Bytes image = Truncate(MakeImage(), kDylibCmd + kDylibNameField + 6);
    RequireAllRejected(image, {ListOp()}, "LC_LOAD_DYLIB");
}

TEST_CASE("edge: Mach-O truncated inside the signature data", "[MalformedMachO][edge]")
{
    const Bytes image = Truncate(MakeImage(true), kSignatureOffset + 10);
    RequireAllRejected(image, SignerOps(), "LC_CODE_SIGNATURE");
    RequireListAnswers(image, {kDylibName});
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
