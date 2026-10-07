// The program fingerprint (Authenticode digest) against values recorded from
// a standard verifier.
//
// fixtures/pe-reference-digests.txt holds, for tiny.exe and test.dll with 0 to
// 7 non-zero bytes appended (the bytes are 0xA5, 0xA6, ...), the digest that
// osslsigncode calculates for the signed file. The tests read those values,
// so they need neither osslsigncode nor any new binary file.
//
// The reproduction that started this: a MinGW program with 3 bytes appended
// had the fingerprint 38618379b7314327... before embedding, while the verifier
// (and the library after embedding) computed 8962CB5CA43313B7... for the
// signed file. A signature is stored on an 8-byte boundary, so the padding
// that embedding adds belongs to the fingerprint of the unsigned file too.

#include "MalformedInput.hpp"

#include <libthe-seed/PeSigner.hpp>

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;

constexpr std::uint64_t kCertificateDirectoryField = 128 + 24 + 144;

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

std::string Key(const std::string &fixture, unsigned appended)
{
    return fixture + ":" + std::to_string(appended);
}

// The recorded digests, by "<fixture>:<appended count>".
const std::map<std::string, std::string> &Reference()
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
            std::string fixture;
            unsigned appended = 0;
            std::uint64_t length = 0;
            std::string digest;
            REQUIRE(static_cast<bool>(fields >> fixture >> appended >> length >> digest));
            rows[Key(fixture, appended)] = digest;
        }
        return rows;
    }();
    return table;
}

const std::string &Recorded(const std::string &fixture, unsigned appended)
{
    const auto found = Reference().find(Key(fixture, appended));
    INFO("no recorded digest for " << fixture << " with " << appended << " bytes appended");
    REQUIRE(found != Reference().end());
    return found->second;
}

// A fixture with `count` bytes appended, as the recorded values were made.
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

std::string DigestOf(const std::string &path)
{
    return ToHex(PeSigner::ComputeAuthenticodeDigest(path).digest);
}

std::uint32_t CertificateOffset(const Bytes &bytes)
{
    return seedtest::malformed::detail::GetLE<std::uint32_t>(bytes, kCertificateDirectoryField);
}

std::uint32_t CertificateSize(const Bytes &bytes)
{
    return seedtest::malformed::detail::GetLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4);
}

const char *const kFixtures[] = {"tiny.exe", "test.dll"};

} // namespace

// ---------------------------------------------------------------------------
// Aligned programs: nothing changes
// ---------------------------------------------------------------------------

TEST_CASE("aligned: the fingerprint equals the recorded value and survives embedding",
          "[PeDigestReference][aligned]")
{
    for(const std::string fixture : kFixtures)
    {
        DYNAMIC_SECTION(fixture)
        {
            seedtest::ScratchDir scratch("pe-digest-aligned");
            const Bytes original = seedtest::malformed::LoadSample(fixture);
            REQUIRE(original.size() % 8 == 0);
            const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, original);

            CHECK(DigestOf(path) == Recorded(fixture, 0));

            PeSigner::EmbedSignature(path, TestBlob(128));
            CHECK(DigestOf(path) == Recorded(fixture, 0));

            const auto extracted = PeSigner::ExtractSignature(path);
            REQUIRE(extracted.has_value());
            CHECK(*extracted == TestBlob(128));
        }
    }
}

// ---------------------------------------------------------------------------
// Unaligned programs: the padding that embedding adds is part of the fingerprint
// ---------------------------------------------------------------------------

TEST_CASE("unaligned: the fingerprint equals the verifier's value for the signed file",
          "[PeDigestReference][unaligned]")
{
    for(const std::string fixture : kFixtures)
    {
        for(unsigned count = 1; count <= 7; ++count)
        {
            DYNAMIC_SECTION(fixture << " plus " << count)
            {
                seedtest::ScratchDir scratch("pe-digest-unaligned");
                const Bytes input = Derived(fixture, count);
                REQUIRE(input.size() % 8 != 0);
                const std::string path = seedtest::malformed::WriteScratch(scratch, fixture, input);

                const std::string before = DigestOf(path);
                CHECK(before == Recorded(fixture, count));

                // Blobs of both alignments, so the signature table padding is covered too.
                for(const std::size_t blob_size : {128u, 61u})
                {
                    DYNAMIC_SECTION("blob of " << blob_size << " bytes")
                    {
                        seedtest::malformed::WriteScratch(scratch, fixture, input);
                        PeSigner::EmbedSignature(path, TestBlob(blob_size));
                        const Bytes signed_bytes = seedtest::malformed::ReadAll(path);

                        CHECK(DigestOf(path) == before);
                        CHECK(DigestOf(path) == Recorded(fixture, count));

                        // The signature starts on the next 8-byte boundary, which the
                        // appended bytes now reach (the lengths become aligned
                        // exactly when the signature is added).
                        const std::uint64_t padded = (input.size() + 7) / 8 * 8;
                        CHECK(CertificateOffset(signed_bytes) == padded);
                        CHECK(CertificateOffset(signed_bytes) % 8 == 0);
                        CHECK(signed_bytes.size() == CertificateOffset(signed_bytes) + CertificateSize(signed_bytes));

                        // Everything the user had, including the appended bytes, is kept.
                        REQUIRE(signed_bytes.size() > input.size());
                        CHECK(Bytes(signed_bytes.begin() + static_cast<std::ptrdiff_t>(input.size() - count),
                                    signed_bytes.begin() + static_cast<std::ptrdiff_t>(input.size())) ==
                              Bytes(input.end() - static_cast<std::ptrdiff_t>(count), input.end()));
                        CHECK(PeSigner::ExtractSignature(path) == std::optional<Bytes>(TestBlob(blob_size)));
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Already signed programs
// ---------------------------------------------------------------------------

TEST_CASE("signed: the fingerprint equals that of the program with its signature stripped",
          "[PeDigestReference][signed]")
{
    for(const std::string fixture : kFixtures)
    {
        for(const unsigned count : {0u, 3u})
        {
            DYNAMIC_SECTION(fixture << " plus " << count)
            {
                seedtest::ScratchDir scratch("pe-digest-signed");
                const std::string path =
                    seedtest::malformed::WriteScratch(scratch, fixture, Derived(fixture, count));
                PeSigner::EmbedSignature(path, TestBlob(128));
                REQUIRE(PeSigner::HasEmbeddedSignature(path));
                const std::string signed_digest = DigestOf(path);
                CHECK(signed_digest == Recorded(fixture, count));

                PeSigner::StripSignature(path);
                CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
                CHECK(DigestOf(path) == signed_digest);
            }
        }
    }
}

TEST_CASE("signed: embedding replaces the old signature and leaves exactly one",
          "[PeDigestReference][signed]")
{
    for(const std::string fixture : kFixtures)
    {
        for(const unsigned count : {0u, 3u})
        {
            DYNAMIC_SECTION(fixture << " plus " << count)
            {
                seedtest::ScratchDir scratch("pe-digest-replace");
                const std::string path =
                    seedtest::malformed::WriteScratch(scratch, fixture, Derived(fixture, count));
                const Bytes first = TestBlob(128);
                const Bytes second = TestBlob(200);

                PeSigner::EmbedSignature(path, first);
                const Bytes after_first = seedtest::malformed::ReadAll(path);
                PeSigner::EmbedSignature(path, second);
                const Bytes after_second = seedtest::malformed::ReadAll(path);

                CHECK(PeSigner::ExtractSignature(path) == std::optional<Bytes>(second));
                // One certificate table, ending the file: the old one is gone.
                CHECK(after_second.size() == CertificateOffset(after_second) + CertificateSize(after_second));
                CHECK(CertificateOffset(after_second) == CertificateOffset(after_first));
                CHECK(after_second.size() < after_first.size() + 200);
                CHECK(DigestOf(path) == Recorded(fixture, count));

                PeSigner::StripSignature(path);
                CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
                CHECK_FALSE(PeSigner::ExtractSignature(path).has_value());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Malformed input is still rejected with the existing messages
// ---------------------------------------------------------------------------

namespace {

void RequireDigestAndEmbedRejected(const Bytes &input, const std::string &keyword)
{
    seedtest::ScratchDir scratch("pe-digest-malformed");
    const std::string path = seedtest::malformed::WriteScratch(scratch, "input.bin", input);
    seedtest::malformed::RequireRejected([&] { (void)PeSigner::ComputeAuthenticodeDigest(path); },
                                         "PE", keyword, input.size());
    seedtest::malformed::RequireRejected([&] { PeSigner::EmbedSignature(path, TestBlob(128)); },
                                         "PE", keyword, input.size(), 128);
    seedtest::malformed::RequireUnchanged(path, input);
}

} // namespace

TEST_CASE("malformed: a certificate range past the end of the file is rejected",
          "[PeDigestReference][malformed]")
{
    Bytes bytes = seedtest::malformed::LoadSample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField,
                                                static_cast<std::uint32_t>(bytes.size() - 8));
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4, 4096);
    RequireDigestAndEmbedRejected(bytes, "certificate table");
}

TEST_CASE("malformed: a zero-length file is rejected", "[PeDigestReference][malformed]")
{
    RequireDigestAndEmbedRejected(Bytes{}, "DOS header");
}

TEST_CASE("malformed: a truncated header is rejected", "[PeDigestReference][malformed]")
{
    RequireDigestAndEmbedRejected(seedtest::malformed::Truncate(seedtest::malformed::LoadSample("tiny.exe"), 40),
                                  "DOS header");
    RequireDigestAndEmbedRejected(seedtest::malformed::Truncate(seedtest::malformed::LoadSample("tiny.exe"), 140),
                                  "PE header");
}
