// Adding a signature to a Windows program and then removing it gives back the
// original file.
//
// A signature is stored on an 8-byte boundary, so embedding pads a program
// whose length is not a multiple of 8 with zero bytes before the signature.
// Removing the signature has to take that padding away again, using one rule:
// it removes trailing zero bytes in front of the signature, at most 7, and
// never anything below the end of the last section's data or the headers.
//
// What follows from the rule, and what these tests require:
//   - a program whose last byte is not zero is restored byte for byte, for
//     every length remainder modulo 8;
//   - a program whose length is a multiple of 8 and whose own final bytes are
//     zeros cannot be told from a padded one, so at most 7 of those zero bytes
//     are lost, and nothing below the section data or the headers;
//   - a program signed by another tool is treated the same way: its signature
//     entry goes, and so does the zero padding that precedes the signature,
//     up to the same limit;
//   - a program without a signature is not touched at all.

#include "MalformedInput.hpp"

#include <libthe-seed/PeSigner.hpp>

#include "internal/FileReplaceHooks.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;

// Offsets in a 64-bit program whose PE header sits at 0x80 (both samples).
constexpr std::uint64_t kPeHeader = 0x80;
constexpr std::uint64_t kNumberOfSectionsField = kPeHeader + 6;
constexpr std::uint64_t kChecksumField = kPeHeader + 24 + 64;
constexpr std::uint64_t kSizeOfHeadersField = kPeHeader + 24 + 60;
constexpr std::uint64_t kCertificateDirectoryField = 128 + 24 + 144;

const char *const kFixtures[] = {"tiny.exe", "test.dll"};

template <typename T>
T Get(const Bytes &bytes, std::uint64_t offset)
{
    return seedtest::malformed::detail::GetLE<T>(bytes, offset);
}

// Appended bytes as the recorded fingerprints were made: 0xA5, 0xA6, ...
Bytes Derived(const std::string &fixture, unsigned count)
{
    Bytes bytes = seedtest::malformed::LoadSample(fixture);
    for(unsigned k = 0; k < count; ++k)
    {
        bytes.push_back(static_cast<std::uint8_t>(0xA5 + k));
    }
    return bytes;
}

Bytes TestBlob(std::size_t size)
{
    Bytes blob(size);
    for(std::size_t i = 0; i < blob.size(); ++i)
    {
        blob[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
    }
    return blob;
}

std::string ToHex(const std::vector<std::uint8_t> &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for(const auto byte : bytes)
    {
        text += digits[byte >> 4];
        text += digits[byte & 0xF];
    }
    return text;
}

std::string DigestOf(const std::string &path)
{
    return ToHex(PeSigner::ComputeAuthenticodeDigest(path).digest);
}

// The recorded fingerprints, by "<fixture>:<appended count>".
std::string Recorded(const std::string &fixture, unsigned appended)
{
    static const std::map<std::string, std::string> table = [] {
        std::map<std::string, std::string> rows;
        std::ifstream in(seedtest::FixturePath("pe-reference-digests.txt"));
        REQUIRE(in.good());
        std::string line;
        while(std::getline(in, line))
        {
            if(line.empty() || line[0] == '#')
            {
                continue;
            }
            std::istringstream fields(line);
            std::string name;
            unsigned count = 0;
            std::uint64_t length = 0;
            std::string digest;
            REQUIRE(static_cast<bool>(fields >> name >> count >> length >> digest));
            rows[name + ":" + std::to_string(count)] = digest;
        }
        return rows;
    }();
    const auto found = table.find(fixture + ":" + std::to_string(appended));
    REQUIRE(found != table.end());
    return found->second;
}

// The CheckSum of a program as the loader defines it, calculated here and not
// by the library.
std::uint32_t ExpectedChecksum(const Bytes &bytes)
{
    std::uint32_t sum = 0;
    for(std::size_t offset = 0; offset + 1 < bytes.size(); offset += 2)
    {
        if(offset == kChecksumField || offset == kChecksumField + 2)
        {
            continue;
        }
        sum += static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8);
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    if(bytes.size() % 2 != 0)
    {
        sum += bytes.back();
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = (sum & 0xFFFF) + (sum >> 16);
    return sum + static_cast<std::uint32_t>(bytes.size());
}

// The larger of the end of the last section's raw data and SizeOfHeaders.
std::uint64_t Floor(const Bytes &bytes)
{
    const std::uint16_t sections = Get<std::uint16_t>(bytes, kNumberOfSectionsField);
    const std::uint16_t optional_size = Get<std::uint16_t>(bytes, kPeHeader + 20);
    std::uint64_t floor = Get<std::uint32_t>(bytes, kSizeOfHeadersField);
    const std::uint64_t table = kPeHeader + 24 + optional_size;
    for(std::uint16_t i = 0; i < sections; ++i)
    {
        const std::uint64_t entry = table + 40ull * i;
        const std::uint64_t end = static_cast<std::uint64_t>(Get<std::uint32_t>(bytes, entry + 20)) +
                                  Get<std::uint32_t>(bytes, entry + 16);
        floor = std::max(floor, end);
    }
    return floor;
}

// What every stripped file must look like, whatever was removed.
void CheckStrippedShape(const std::string &path)
{
    const Bytes stripped = seedtest::malformed::ReadAll(path);
    CHECK(Get<std::uint32_t>(stripped, kCertificateDirectoryField) == 0);
    CHECK(Get<std::uint32_t>(stripped, kCertificateDirectoryField + 4) == 0);
    CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(PeSigner::ExtractSignature(path).has_value());
    CHECK(Get<std::uint32_t>(stripped, kChecksumField) == ExpectedChecksum(stripped));
    CHECK(stripped.size() >= Floor(stripped));
    // Well formed: the fingerprint can be calculated again.
    CHECK_NOTHROW((void)PeSigner::ComputeAuthenticodeDigest(path));
}

// A program signed the way osslsigncode signs one: the program is padded with
// zero bytes to a multiple of 8, the certificate table follows, with its
// 8-byte entry header (length, revision 0x0200, type 0x0002) and the
// signature, padded to a multiple of 8 inside the entry. The directory entry
// points at the table; the CheckSum field is left as it was. It is built by
// hand so no signing tool is needed to run the tests.
Bytes SignedLikeOsslsigncode(const Bytes &program, std::size_t blob_size)
{
    Bytes bytes = program;
    while(bytes.size() % 8 != 0)
    {
        bytes.push_back(0);
    }
    const std::uint32_t address = static_cast<std::uint32_t>(bytes.size());
    const std::uint32_t length = static_cast<std::uint32_t>((8 + blob_size + 7) / 8 * 8);
    const Bytes blob = TestBlob(blob_size);
    bytes.resize(bytes.size() + length, 0);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, address, length);
    seedtest::malformed::PatchLE<std::uint16_t>(bytes, address + 4, 0x0200);
    seedtest::malformed::PatchLE<std::uint16_t>(bytes, address + 6, 0x0002);
    std::copy(blob.begin(), blob.end(), bytes.begin() + address + 8);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField, address);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4, length);
    return bytes;
}

} // namespace

// ---------------------------------------------------------------------------
// Embed then strip restores the program
// ---------------------------------------------------------------------------

TEST_CASE("round trip: embed then strip restores a program ending in a non-zero byte",
          "[PeRoundTrip][restore]")
{
    for(const std::string fixture : kFixtures)
    {
        for(unsigned count = 1; count <= 7; ++count)
        {
            // Blobs of both alignments, so the table padding is covered too.
            for(const std::size_t blob_size : {128u, 61u, 1000u})
            {
                DYNAMIC_SECTION(fixture << " plus " << count << ", blob of " << blob_size << " bytes")
                {
                    seedtest::ScratchDir scratch("pe-roundtrip");
                    const Bytes original = Derived(fixture, count);
                    REQUIRE(original.back() != 0);
                    const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, original);
                    const std::string before = DigestOf(path);

                    PeSigner::EmbedSignature(path, TestBlob(blob_size));
                    REQUIRE(PeSigner::HasEmbeddedSignature(path));
                    PeSigner::StripSignature(path);

                    const Bytes stripped = seedtest::malformed::ReadAll(path);
                    INFO("original " << original.size() << " bytes, stripped " << stripped.size());
                    // The checksum is the only field that may differ: the original may
                    // carry any value there, the stripped file carries the calculated one.
                    Bytes expected = original;
                    seedtest::malformed::PatchLE<std::uint32_t>(expected, kChecksumField,
                                                                ExpectedChecksum(original));
                    CHECK(stripped.size() == original.size());
                    CHECK(stripped == expected);
                    CheckStrippedShape(path);
                    CHECK(DigestOf(path) == before);
                    CHECK(DigestOf(path) == Recorded(fixture, count));
                }
            }
        }
    }
}

TEST_CASE("round trip: the samples as they are come back byte for byte",
          "[PeRoundTrip][restore]")
{
    for(const std::string fixture : kFixtures)
    {
        DYNAMIC_SECTION(fixture)
        {
            seedtest::ScratchDir scratch("pe-roundtrip-aligned");
            const Bytes original = seedtest::malformed::LoadSample(fixture);
            REQUIRE(original.size() % 8 == 0);
            const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, original);

            PeSigner::EmbedSignature(path, TestBlob(128));
            PeSigner::StripSignature(path);

            // These samples end inside the last section with zero bytes: nothing
            // below the section data may be taken, whatever the rule.
            CHECK(seedtest::malformed::ReadAll(path) == original);
            CheckStrippedShape(path);
            CHECK(DigestOf(path) == Recorded(fixture, 0));
        }
    }
}

TEST_CASE("round trip: embed, strip, embed gives two identical signed files",
          "[PeRoundTrip][restore]")
{
    for(const std::string fixture : kFixtures)
    {
        for(const unsigned count : {0u, 1u, 3u, 7u})
        {
            DYNAMIC_SECTION(fixture << " plus " << count)
            {
                seedtest::ScratchDir scratch("pe-roundtrip-twice");
                const std::string path =
                    seedtest::malformed::WriteScratch(scratch, fixture, Derived(fixture, count));

                PeSigner::EmbedSignature(path, TestBlob(128));
                const Bytes first = seedtest::malformed::ReadAll(path);
                PeSigner::StripSignature(path);
                PeSigner::EmbedSignature(path, TestBlob(128));
                const Bytes second = seedtest::malformed::ReadAll(path);

                CHECK(first == second);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// A program that is already signed
// ---------------------------------------------------------------------------

TEST_CASE("signed: a second embed keeps one signature and strip still restores the program",
          "[PeRoundTrip][signed]")
{
    for(const std::string fixture : kFixtures)
    {
        for(const unsigned count : {0u, 3u, 5u})
        {
            DYNAMIC_SECTION(fixture << " plus " << count)
            {
                seedtest::ScratchDir scratch("pe-roundtrip-resign");
                const Bytes original = Derived(fixture, count);
                const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, original);

                PeSigner::EmbedSignature(path, TestBlob(128));
                PeSigner::EmbedSignature(path, TestBlob(200));
                CHECK(PeSigner::ExtractSignature(path) == std::optional<Bytes>(TestBlob(200)));

                PeSigner::StripSignature(path);
                const Bytes stripped = seedtest::malformed::ReadAll(path);
                CHECK(stripped.size() == original.size());
                CheckStrippedShape(path);
                CHECK(DigestOf(path) == Recorded(fixture, count));
            }
        }
    }
}

TEST_CASE("signed by another tool: the signature entry goes and the file stays well formed",
          "[PeRoundTrip][signed]")
{
    for(const std::string fixture : kFixtures)
    {
        for(const unsigned count : {0u, 3u, 7u})
        {
            for(const std::size_t blob_size : {100u, 1000u})
            {
                DYNAMIC_SECTION(fixture << " plus " << count << ", blob of " << blob_size << " bytes")
                {
                    seedtest::ScratchDir scratch("pe-roundtrip-foreign");
                    const Bytes original = Derived(fixture, count);
                    const Bytes foreign = SignedLikeOsslsigncode(original, blob_size);
                    const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, foreign);

                    // The hand-built file is a program the verifier would accept: its
                    // fingerprint is the recorded one.
                    REQUIRE(PeSigner::HasEmbeddedSignature(path));
                    CHECK(PeSigner::ExtractSignature(path).has_value());
                    CHECK(DigestOf(path) == Recorded(fixture, count));

                    PeSigner::StripSignature(path);

                    const Bytes stripped = seedtest::malformed::ReadAll(path);
                    // The documented rule applied to the padding in front of the signature:
                    // it is removed (the program ends in a non-zero byte, or in the zeros
                    // of its last section which the rule never goes below).
                    CHECK(stripped.size() == original.size());
                    CheckStrippedShape(path);
                    CHECK(DigestOf(path) == Recorded(fixture, count));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The edge of the rule
// ---------------------------------------------------------------------------

TEST_CASE("limit: an aligned program ending in zeros loses at most 7 of them",
          "[PeRoundTrip][limit]")
{
    for(const std::string fixture : kFixtures)
    {
        for(const std::size_t zeros : {8u, 16u, 24u})
        {
            DYNAMIC_SECTION(fixture << " plus " << zeros << " zero bytes")
            {
                seedtest::ScratchDir scratch("pe-roundtrip-limit");
                Bytes original = seedtest::malformed::LoadSample(fixture);
                original.resize(original.size() + zeros, 0);
                REQUIRE(original.size() % 8 == 0);
                const std::uint64_t floor = Floor(original);
                REQUIRE(floor < original.size());
                const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, original);

                PeSigner::EmbedSignature(path, TestBlob(128));
                PeSigner::StripSignature(path);

                const Bytes stripped = seedtest::malformed::ReadAll(path);
                INFO("original " << original.size() << " bytes, stripped " << stripped.size());
                CHECK(stripped.size() <= original.size());
                CHECK(original.size() - stripped.size() <= 7);
                CHECK(stripped.size() >= floor);
                // What is left is the original up to its new end (apart from the checksum).
                Bytes head(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(std::min(stripped.size(), original.size())));
                Bytes got = stripped;
                seedtest::malformed::PatchLE<std::uint32_t>(head, kChecksumField, 0);
                seedtest::malformed::PatchLE<std::uint32_t>(got, kChecksumField, 0);
                CHECK(got == head);
                CheckStrippedShape(path);
            }
        }
    }
}

TEST_CASE("limit: nothing below the end of the last section's data is removed",
          "[PeRoundTrip][limit]")
{
    for(const std::string fixture : kFixtures)
    {
        DYNAMIC_SECTION(fixture)
        {
            seedtest::ScratchDir scratch("pe-roundtrip-floor");
            // The samples end with zero bytes that belong to the last section. With
            // zero bytes appended as well, strip may take the appended ones only.
            Bytes original = seedtest::malformed::LoadSample(fixture);
            const std::uint64_t floor = Floor(original);
            REQUIRE(floor == original.size());
            for(std::size_t i = original.size() - 16; i < original.size(); ++i)
            {
                REQUIRE(original[i] == 0);
            }
            original.resize(original.size() + 8, 0);
            const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, original);

            PeSigner::EmbedSignature(path, TestBlob(128));
            PeSigner::StripSignature(path);

            const Bytes stripped = seedtest::malformed::ReadAll(path);
            CHECK(stripped.size() >= floor);
            CHECK(stripped.size() >= original.size() - 7);
            CheckStrippedShape(path);
        }
    }
}

TEST_CASE("limit: a program that is only its headers is not cut below them",
          "[PeRoundTrip][limit]")
{
    // The sample with no sections and no data after the headers; SizeOfHeaders is 1024.
    Bytes original = seedtest::malformed::Truncate(seedtest::malformed::LoadSample("tiny.exe"), 1024);
    seedtest::malformed::PatchLE<std::uint16_t>(original, kNumberOfSectionsField, 0);
    REQUIRE(Floor(original) == 1024);
    seedtest::ScratchDir scratch("pe-roundtrip-headers");
    const std::string path = seedtest::malformed::WriteScratch(scratch, "headers.exe", original);

    PeSigner::EmbedSignature(path, TestBlob(128));
    PeSigner::StripSignature(path);

    const Bytes stripped = seedtest::malformed::ReadAll(path);
    CHECK(stripped.size() >= 1024);
    CHECK(stripped.size() == original.size());
    CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
}

// ---------------------------------------------------------------------------
// A program without a signature
// ---------------------------------------------------------------------------

TEST_CASE("unsigned: strip leaves the file alone and writes nothing",
          "[PeRoundTrip][unsigned]")
{
    for(const std::string fixture : kFixtures)
    {
        // Including lengths that end in zeros and in non-zero bytes.
        for(const unsigned count : {0u, 3u, 7u, 100u})
        {
            DYNAMIC_SECTION(fixture << " plus " << count)
            {
                seedtest::ScratchDir scratch("pe-roundtrip-unsigned");
                Bytes input = Derived(fixture, count < 8 ? count : 0);
                if(count == 100)
                {
                    input.resize(input.size() + 8, 0);
                }
                const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, input);
                REQUIRE_FALSE(PeSigner::HasEmbeddedSignature(path));

                seed::internal::FileReplaceHooksScope scope;
                CHECK_NOTHROW(PeSigner::StripSignature(path));

                CHECK(scope.Calls().empty());
                seedtest::malformed::RequireUnchanged(path, input);
                CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Malformed input is still rejected with the existing messages
// ---------------------------------------------------------------------------

namespace {

void RequireStripRejected(const Bytes &input, const std::string &keyword)
{
    seedtest::ScratchDir scratch("pe-roundtrip-malformed");
    const std::string path = seedtest::malformed::WriteScratch(scratch, "input.bin", input);
    seedtest::malformed::RequireRejected([&] { PeSigner::StripSignature(path); }, "PE", keyword,
                                         input.size());
    seedtest::malformed::RequireUnchanged(path, input);
}

} // namespace

TEST_CASE("malformed: strip rejects a certificate range past the end of the file",
          "[PeRoundTrip][malformed]")
{
    Bytes bytes = seedtest::malformed::LoadSample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField,
                                                static_cast<std::uint32_t>(bytes.size() - 8));
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4, 4096);
    RequireStripRejected(bytes, "certificate table");
}

TEST_CASE("malformed: strip rejects an empty file and truncated headers",
          "[PeRoundTrip][malformed]")
{
    RequireStripRejected(Bytes{}, "DOS header");
    RequireStripRejected(seedtest::malformed::Truncate(seedtest::malformed::LoadSample("tiny.exe"), 40),
                         "DOS header");
    RequireStripRejected(seedtest::malformed::Truncate(seedtest::malformed::LoadSample("tiny.exe"), 140),
                         "PE header");
}
