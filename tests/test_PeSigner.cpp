#include "TestPaths.hpp"

#include <libthe-seed/PeSigner.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

namespace {

std::string CopyFixture(const seedtest::ScratchDir &scratch, const std::string &name)
{
    auto src = seedtest::FixturePath(name);
    auto dst = scratch.Path() / ("test_pesigner_" + name);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    return dst.string();
}

// The digest osslsigncode calculated for `fixture` with nothing appended, from the recorded
// reference file (the same file test_PeDigestReference reads).
std::string RecordedDigest(const std::string &fixture)
{
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
        unsigned appended = 0;
        std::uint64_t length = 0;
        std::string digest;
        if((fields >> name >> appended >> length >> digest) && name == fixture && appended == 0)
        {
            return digest;
        }
    }
    FAIL("no recorded digest for " << fixture);
    return {};
}

std::string Hex(const std::vector<std::uint8_t> &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for(std::uint8_t byte : bytes)
    {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

} // namespace

TEST_CASE("PeSigner::ComputeAuthenticodeDigest computes valid digest", "[PeSigner]")
{
    auto fixturePath = seedtest::FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(fixturePath));

    auto result = the_seed::PeSigner::ComputeAuthenticodeDigest(fixturePath);

    // Digest should be 32 bytes (SHA-256)
    REQUIRE(result.digest.size() == 32);

    // The value is the one an independent tool calculated.
    CHECK(Hex(result.digest) == RecordedDigest("tiny.exe"));

    // tiny.exe is PE32+ (x86-64)
    CHECK(result.is_pe32_plus == true);

    // Digest should be deterministic
    auto result2 = the_seed::PeSigner::ComputeAuthenticodeDigest(fixturePath);
    CHECK(result.digest == result2.digest);
}

TEST_CASE("PeSigner::HasEmbeddedSignature returns false for unsigned PE", "[PeSigner]")
{
    auto fixturePath = seedtest::FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK(the_seed::PeSigner::HasEmbeddedSignature(fixturePath) == false);
}

TEST_CASE("PeSigner::EmbedSignature and ExtractSignature round-trip", "[PeSigner]")
{
    seedtest::ScratchDir scratch;
    auto tempPath = CopyFixture(scratch, "tiny.exe");

    // Initially unsigned
    REQUIRE(the_seed::PeSigner::HasEmbeddedSignature(tempPath) == false);

    // Create a fake PKCS#7 blob (not valid CMS, just for round-trip testing)
    std::vector<std::uint8_t> fakePkcs7(128);
    for (size_t i = 0; i < fakePkcs7.size(); i++)
        fakePkcs7[i] = static_cast<std::uint8_t>(i & 0xFF);

    // Embed signature
    the_seed::PeSigner::EmbedSignature(tempPath, fakePkcs7);

    // Now should have embedded signature
    CHECK(the_seed::PeSigner::HasEmbeddedSignature(tempPath) == true);

    // Extract and verify round-trip
    auto extracted = the_seed::PeSigner::ExtractSignature(tempPath);
    REQUIRE(extracted.has_value());
    CHECK(extracted->size() == fakePkcs7.size());
    CHECK(*extracted == fakePkcs7);

    // Clean up
}

TEST_CASE("PeSigner::StripSignature removes embedded signature", "[PeSigner]")
{
    seedtest::ScratchDir scratch;
    auto tempPath = CopyFixture(scratch, "tiny.exe");

    // Embed a fake signature first
    std::vector<std::uint8_t> fakePkcs7(64, 0xAA);
    the_seed::PeSigner::EmbedSignature(tempPath, fakePkcs7);
    REQUIRE(the_seed::PeSigner::HasEmbeddedSignature(tempPath) == true);

    // Strip it
    the_seed::PeSigner::StripSignature(tempPath);

    // Should be unsigned again
    CHECK(the_seed::PeSigner::HasEmbeddedSignature(tempPath) == false);

    // Extract should return nullopt
    auto extracted = the_seed::PeSigner::ExtractSignature(tempPath);
    CHECK_FALSE(extracted.has_value());

    // Clean up
}

TEST_CASE("PeSigner throws on non-PE file", "[PeSigner]")
{
    auto fixturePath = seedtest::FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK_THROWS_AS(the_seed::PeSigner::ComputeAuthenticodeDigest(fixturePath), std::runtime_error);
    CHECK_THROWS_AS(the_seed::PeSigner::HasEmbeddedSignature(fixturePath), std::runtime_error);
}

TEST_CASE("PeSigner::ComputeAuthenticodeDigest changes after embed", "[PeSigner]")
{
    seedtest::ScratchDir scratch;
    auto tempPath = CopyFixture(scratch, "tiny.exe");

    // Get digest before signing
    auto digestBefore = the_seed::PeSigner::ComputeAuthenticodeDigest(tempPath);

    // Embed a fake signature
    std::vector<std::uint8_t> fakePkcs7(128, 0xBB);
    the_seed::PeSigner::EmbedSignature(tempPath, fakePkcs7);

    // Get digest after signing — should be the same because Authenticode
    // digest excludes the certificate data and DD entry 4
    auto digestAfter = the_seed::PeSigner::ComputeAuthenticodeDigest(tempPath);
    CHECK(digestBefore.digest == digestAfter.digest);

}
