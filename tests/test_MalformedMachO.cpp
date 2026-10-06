// Malformed and edge-case Mac signature data (SuperBlob), through
// MachOSigner::ExtractCmsFromSuperBlob and
// MachOSigner::ExtractCodeDirectoryFromSuperBlob.
//
// Test case names start with their origin: "ok:" for a well-formed or
// legal-but-unusual input with today's result, "review:" for the review's
// input, and "edge:" for an edge-case family.
//
// A SuperBlob that is well formed but has no slot of the wanted type gives an
// empty result; anything malformed is rejected with std::runtime_error whose
// message starts with "Mach-O code signature: ". The keywords the tests look
// for are:
//   SuperBlob header   fewer bytes than the 12-byte header
//   magic              the SuperBlob or the wanted blob has the wrong magic
//   length             the SuperBlob length field is past the input or smaller than its index
//   blob index         the index does not fit, or a slot offset is inside the index or past the end
//   blob length        the wanted blob's length is below 8 or runs past the SuperBlob length

#include "MalformedInput.hpp"

#include <libthe-seed/MachOSigner.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
