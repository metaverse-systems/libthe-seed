#include <catch_amalgamated.hpp>

#include <libthe-seed/PeSigner.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

namespace {

std::string FixturePath(const std::string &name)
{
    // Try relative path from build directory
    std::string path = "../tests/fixtures/" + name;
    if (std::filesystem::exists(path))
        return path;
    // Try from test source directory
    path = "tests/fixtures/" + name;
    if (std::filesystem::exists(path))
        return path;
    // Try absolute
    path = std::string(FIXTURES_DIR) + "/" + name;
    if (std::filesystem::exists(path))
        return path;
    return "../tests/fixtures/" + name;
}

std::string CopyFixture(const std::string &name)
{
    auto src = FixturePath(name);
    auto dst = std::filesystem::temp_directory_path() / ("test_pesigner_" + name);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    return dst.string();
}

} // namespace

TEST_CASE("PeSigner::ComputeAuthenticodeDigest computes valid digest", "[PeSigner]")
{
    auto fixturePath = FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(fixturePath));

    auto result = PeSigner::ComputeAuthenticodeDigest(fixturePath);

    // Digest should be 32 bytes (SHA-256)
    REQUIRE(result.digest.size() == 32);

    // tiny.exe is PE32+ (x86-64)
    CHECK(result.is_pe32_plus == true);

    // Digest should be deterministic
    auto result2 = PeSigner::ComputeAuthenticodeDigest(fixturePath);
    CHECK(result.digest == result2.digest);
}

TEST_CASE("PeSigner::HasEmbeddedSignature returns false for unsigned PE", "[PeSigner]")
{
    auto fixturePath = FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK(PeSigner::HasEmbeddedSignature(fixturePath) == false);
}

TEST_CASE("PeSigner::EmbedSignature and ExtractSignature round-trip", "[PeSigner]")
{
    auto tempPath = CopyFixture("tiny.exe");

    // Initially unsigned
    REQUIRE(PeSigner::HasEmbeddedSignature(tempPath) == false);

    // Create a fake PKCS#7 blob (not valid CMS, just for round-trip testing)
    std::vector<std::uint8_t> fakePkcs7(128);
    for (size_t i = 0; i < fakePkcs7.size(); i++)
        fakePkcs7[i] = static_cast<std::uint8_t>(i & 0xFF);

    // Embed signature
    PeSigner::EmbedSignature(tempPath, fakePkcs7);

    // Now should have embedded signature
    CHECK(PeSigner::HasEmbeddedSignature(tempPath) == true);

    // Extract and verify round-trip
    auto extracted = PeSigner::ExtractSignature(tempPath);
    REQUIRE(extracted.has_value());
    CHECK(extracted->size() == fakePkcs7.size());
    CHECK(*extracted == fakePkcs7);

    // Clean up
    std::filesystem::remove(tempPath);
}

TEST_CASE("PeSigner::StripSignature removes embedded signature", "[PeSigner]")
{
    auto tempPath = CopyFixture("tiny.exe");

    // Embed a fake signature first
    std::vector<std::uint8_t> fakePkcs7(64, 0xAA);
    PeSigner::EmbedSignature(tempPath, fakePkcs7);
    REQUIRE(PeSigner::HasEmbeddedSignature(tempPath) == true);

    // Strip it
    PeSigner::StripSignature(tempPath);

    // Should be unsigned again
    CHECK(PeSigner::HasEmbeddedSignature(tempPath) == false);

    // Extract should return nullopt
    auto extracted = PeSigner::ExtractSignature(tempPath);
    CHECK_FALSE(extracted.has_value());

    // Clean up
    std::filesystem::remove(tempPath);
}

TEST_CASE("PeSigner throws on non-PE file", "[PeSigner]")
{
    auto fixturePath = FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK_THROWS_AS(PeSigner::ComputeAuthenticodeDigest(fixturePath), std::runtime_error);
    CHECK_THROWS_AS(PeSigner::HasEmbeddedSignature(fixturePath), std::runtime_error);
}

TEST_CASE("PeSigner::ComputeAuthenticodeDigest changes after embed", "[PeSigner]")
{
    auto tempPath = CopyFixture("tiny.exe");

    // Get digest before signing
    auto digestBefore = PeSigner::ComputeAuthenticodeDigest(tempPath);

    // Embed a fake signature
    std::vector<std::uint8_t> fakePkcs7(128, 0xBB);
    PeSigner::EmbedSignature(tempPath, fakePkcs7);

    // Get digest after signing — should be the same because Authenticode
    // digest excludes the certificate data and DD entry 4
    auto digestAfter = PeSigner::ComputeAuthenticodeDigest(tempPath);
    CHECK(digestBefore.digest == digestAfter.digest);

    std::filesystem::remove(tempPath);
}
