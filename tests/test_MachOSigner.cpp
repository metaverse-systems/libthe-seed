#include <catch_amalgamated.hpp>

#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MachOParser.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

namespace {

std::string FixturePath(const std::string &name)
{
    std::string path = "../tests/fixtures/" + name;
    if (std::filesystem::exists(path))
        return path;
    path = "tests/fixtures/" + name;
    if (std::filesystem::exists(path))
        return path;
    path = std::string(FIXTURES_DIR) + "/" + name;
    if (std::filesystem::exists(path))
        return path;
    return "../tests/fixtures/" + name;
}

std::string CopyFixture(const std::string &name)
{
    auto src = FixturePath(name);
    auto dst = std::filesystem::temp_directory_path() / ("test_machosigner_" + name);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    return dst.string();
}

} // namespace

TEST_CASE("MachOSigner::ComputeCodeDirectory computes valid CodeDirectory", "[MachOSigner]")
{
    auto fixturePath = FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(fixturePath));

    auto result = MachOSigner::ComputeCodeDirectory(fixturePath, "test-identity");

    // CodeDirectory should not be empty
    REQUIRE(result.code_directory.size() > 0);

    // CD hash should be 32 bytes (SHA-256)
    CHECK(result.cd_hash.size() == 32);

    // Should be deterministic
    auto result2 = MachOSigner::ComputeCodeDirectory(fixturePath, "test-identity");
    CHECK(result.code_directory == result2.code_directory);
    CHECK(result.cd_hash == result2.cd_hash);
}

TEST_CASE("MachOSigner::BuildSuperBlob creates valid blob", "[MachOSigner]")
{
    auto fixturePath = FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(fixturePath));

    auto cdResult = MachOSigner::ComputeCodeDirectory(fixturePath, "test-identity");

    // Create a fake CMS signature
    std::vector<std::uint8_t> fakeCms(64, 0xCC);

    auto superBlob = MachOSigner::BuildSuperBlob(cdResult.code_directory, fakeCms);

    // SuperBlob should start with magic 0xFADE0CC0
    REQUIRE(superBlob.size() >= 12);
    std::uint32_t magic = (static_cast<std::uint32_t>(superBlob[0]) << 24) |
                           (static_cast<std::uint32_t>(superBlob[1]) << 16) |
                           (static_cast<std::uint32_t>(superBlob[2]) << 8) |
                           static_cast<std::uint32_t>(superBlob[3]);
    CHECK(magic == 0xFADE0CC0);

    // Count should be 3 (CodeDirectory, Requirements, CMS)
    std::uint32_t count = (static_cast<std::uint32_t>(superBlob[8]) << 24) |
                           (static_cast<std::uint32_t>(superBlob[9]) << 16) |
                           (static_cast<std::uint32_t>(superBlob[10]) << 8) |
                           static_cast<std::uint32_t>(superBlob[11]);
    CHECK(count == 3);
}

TEST_CASE("MachOSigner::HasEmbeddedSignature returns false for unsigned", "[MachOSigner]")
{
    auto fixturePath = FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK(MachOSigner::HasEmbeddedSignature(fixturePath) == false);
}

TEST_CASE("MachOSigner::EmbedSignature and ExtractSignature round-trip", "[MachOSigner]")
{
    auto tempPath = CopyFixture("tiny-macho-x86_64");

    // Initially unsigned
    REQUIRE(MachOSigner::HasEmbeddedSignature(tempPath) == false);

    // Compute CodeDirectory and build SuperBlob
    auto cdResult = MachOSigner::ComputeCodeDirectory(tempPath, "test-identity");
    std::vector<std::uint8_t> fakeCms(64, 0xDD);
    auto superBlob = MachOSigner::BuildSuperBlob(cdResult.code_directory, fakeCms);

    // Embed signature
    MachOSigner::EmbedSignature(tempPath, superBlob);

    // Now should have embedded signature
    CHECK(MachOSigner::HasEmbeddedSignature(tempPath) == true);

    // Extract and verify round-trip
    auto extracted = MachOSigner::ExtractSignature(tempPath);
    REQUIRE(extracted.has_value());
    CHECK(extracted->size() == superBlob.size());
    CHECK(*extracted == superBlob);

    // Clean up
    std::filesystem::remove(tempPath);
}

TEST_CASE("MachOSigner::ExtractCmsFromSuperBlob extracts CMS blob", "[MachOSigner]")
{
    auto fixturePath = FixturePath("tiny-macho-x86_64");
    auto cdResult = MachOSigner::ComputeCodeDirectory(fixturePath, "test");

    std::vector<std::uint8_t> fakeCms(48, 0xEE);
    auto superBlob = MachOSigner::BuildSuperBlob(cdResult.code_directory, fakeCms);

    auto cms = MachOSigner::ExtractCmsFromSuperBlob(superBlob);
    REQUIRE(cms.has_value());
    CHECK(*cms == fakeCms);
}

TEST_CASE("MachOSigner::ExtractCodeDirectoryFromSuperBlob extracts CD", "[MachOSigner]")
{
    auto fixturePath = FixturePath("tiny-macho-x86_64");
    auto cdResult = MachOSigner::ComputeCodeDirectory(fixturePath, "test");

    std::vector<std::uint8_t> fakeCms(32, 0xFF);
    auto superBlob = MachOSigner::BuildSuperBlob(cdResult.code_directory, fakeCms);

    auto cd = MachOSigner::ExtractCodeDirectoryFromSuperBlob(superBlob);
    REQUIRE(cd.has_value());
    CHECK(*cd == cdResult.code_directory);
}

TEST_CASE("MachOSigner::ComputeCodeDirectory rejects 32-bit Mach-O", "[MachOSigner]")
{
    // Build a minimal 32-bit Mach-O file (MH_MAGIC = 0xFEEDFACE)
    // 32-bit header is 28 bytes: magic(4) + cputype(4) + cpusubtype(4) +
    //   filetype(4) + ncmds(4) + sizeofcmds(4) + flags(4)
    std::vector<std::uint8_t> macho32(64, 0);
    // MH_MAGIC little-endian representation
    macho32[0] = 0xCE; macho32[1] = 0xFA; macho32[2] = 0xED; macho32[3] = 0xFE;
    // ncmds = 0, sizeofcmds = 0 (at offsets 16 and 20)

    auto tmpPath = std::filesystem::temp_directory_path() / "test_macho32_reject.bin";
    {
        std::ofstream out(tmpPath, std::ios::binary);
        out.write(reinterpret_cast<const char *>(macho32.data()),
                  static_cast<std::streamsize>(macho32.size()));
    }

    REQUIRE_THROWS_AS(
        MachOSigner::ComputeCodeDirectory(tmpPath.string(), "test-identity"),
        std::runtime_error
    );

    std::filesystem::remove(tmpPath);
}
