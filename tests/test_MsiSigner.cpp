#include <catch_amalgamated.hpp>

#include <libthe-seed/MsiSigner.hpp>

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
    auto dst = std::filesystem::temp_directory_path() / ("test_msisigner_" + name);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    return dst.string();
}

} // namespace

TEST_CASE("MsiSigner::IsMsi detects CFBF files", "[MsiSigner]")
{
    auto msiPath = FixturePath("tiny.msi");
    REQUIRE(std::filesystem::exists(msiPath));
    CHECK(MsiSigner::IsMsi(msiPath) == true);
}

TEST_CASE("MsiSigner::IsMsi rejects non-CFBF files", "[MsiSigner]")
{
    auto txtPath = FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(txtPath));
    CHECK(MsiSigner::IsMsi(txtPath) == false);

    auto pePath = FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(pePath));
    CHECK(MsiSigner::IsMsi(pePath) == false);
}

TEST_CASE("MsiSigner::ComputeAuthenticodeDigest computes valid digest", "[MsiSigner]")
{
    auto msiPath = FixturePath("tiny.msi");
    REQUIRE(std::filesystem::exists(msiPath));

    auto result = MsiSigner::ComputeAuthenticodeDigest(msiPath);

    // Digest should be 32 bytes (SHA-256)
    REQUIRE(result.digest.size() == 32);

    // Digest should be deterministic
    auto result2 = MsiSigner::ComputeAuthenticodeDigest(msiPath);
    CHECK(result.digest == result2.digest);
}

TEST_CASE("MsiSigner::HasEmbeddedSignature returns false for unsigned MSI", "[MsiSigner]")
{
    auto msiPath = FixturePath("tiny.msi");
    REQUIRE(std::filesystem::exists(msiPath));

    CHECK(MsiSigner::HasEmbeddedSignature(msiPath) == false);
}

TEST_CASE("MsiSigner::EmbedSignature and ExtractSignature round-trip", "[MsiSigner]")
{
    auto tempPath = CopyFixture("tiny.msi");

    // Initially unsigned
    REQUIRE(MsiSigner::HasEmbeddedSignature(tempPath) == false);

    // Create a fake PKCS#7 blob (not valid CMS, just for round-trip testing)
    std::vector<std::uint8_t> fakePkcs7(128);
    for (size_t i = 0; i < fakePkcs7.size(); i++)
        fakePkcs7[i] = static_cast<std::uint8_t>(i & 0xFF);

    // Embed signature
    MsiSigner::EmbedSignature(tempPath, fakePkcs7);

    // Now should have embedded signature
    CHECK(MsiSigner::HasEmbeddedSignature(tempPath) == true);

    // Extract and verify round-trip
    auto extracted = MsiSigner::ExtractSignature(tempPath);
    REQUIRE(extracted.has_value());
    CHECK(extracted->size() == fakePkcs7.size());
    CHECK(*extracted == fakePkcs7);

    // Clean up
    std::filesystem::remove(tempPath);
}

TEST_CASE("MsiSigner::StripSignature removes embedded signature", "[MsiSigner]")
{
    auto tempPath = CopyFixture("tiny.msi");

    // Embed a fake signature first
    std::vector<std::uint8_t> fakePkcs7(64, 0xAA);
    MsiSigner::EmbedSignature(tempPath, fakePkcs7);
    REQUIRE(MsiSigner::HasEmbeddedSignature(tempPath) == true);

    // Strip it
    MsiSigner::StripSignature(tempPath);

    // Should be unsigned again
    CHECK(MsiSigner::HasEmbeddedSignature(tempPath) == false);

    // Extract should return nullopt
    auto extracted = MsiSigner::ExtractSignature(tempPath);
    CHECK_FALSE(extracted.has_value());

    // Clean up
    std::filesystem::remove(tempPath);
}

TEST_CASE("MsiSigner throws on non-CFBF file", "[MsiSigner]")
{
    auto fixturePath = FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK_THROWS_AS(MsiSigner::ComputeAuthenticodeDigest(fixturePath), std::runtime_error);
    CHECK_THROWS_AS(MsiSigner::HasEmbeddedSignature(fixturePath), std::runtime_error);
}

TEST_CASE("MsiSigner::ComputeAuthenticodeDigest is stable after embed", "[MsiSigner]")
{
    auto tempPath = CopyFixture("tiny.msi");

    // Get digest before signing
    auto digestBefore = MsiSigner::ComputeAuthenticodeDigest(tempPath);

    // Embed a fake signature
    std::vector<std::uint8_t> fakePkcs7(128, 0xBB);
    MsiSigner::EmbedSignature(tempPath, fakePkcs7);

    // Get digest after signing — should remain the same because
    // the Authenticode digest excludes \x05DigitalSignature
    auto digestAfter = MsiSigner::ComputeAuthenticodeDigest(tempPath);
    CHECK(digestBefore.digest == digestAfter.digest);

    std::filesystem::remove(tempPath);
}

TEST_CASE("MsiSigner::EmbedSignature can replace existing signature", "[MsiSigner]")
{
    auto tempPath = CopyFixture("tiny.msi");

    // Embed first signature
    std::vector<std::uint8_t> sig1(64, 0x11);
    MsiSigner::EmbedSignature(tempPath, sig1);
    REQUIRE(MsiSigner::HasEmbeddedSignature(tempPath) == true);

    auto ext1 = MsiSigner::ExtractSignature(tempPath);
    REQUIRE(ext1.has_value());
    CHECK(*ext1 == sig1);

    // Embed replacement signature
    std::vector<std::uint8_t> sig2(256, 0x22);
    MsiSigner::EmbedSignature(tempPath, sig2);

    auto ext2 = MsiSigner::ExtractSignature(tempPath);
    REQUIRE(ext2.has_value());
    CHECK(ext2->size() == sig2.size());
    CHECK(*ext2 == sig2);

    std::filesystem::remove(tempPath);
}
