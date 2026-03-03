#include <catch_amalgamated.hpp>

#include <libthe-seed/MachOParser.hpp>

#include <filesystem>
#include <string>

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

} // namespace

TEST_CASE("MachOParser::DetectFormat identifies x86_64 Mach-O", "[MachOParser]")
{
    auto path = FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::MachO64);
}

TEST_CASE("MachOParser::DetectFormat identifies arm64 Mach-O", "[MachOParser]")
{
    auto path = FixturePath("tiny-macho-arm64");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::MachO64);
}

TEST_CASE("MachOParser::DetectFormat identifies fat Mach-O", "[MachOParser]")
{
    auto path = FixturePath("tiny-macho-universal");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::Fat);
}

TEST_CASE("MachOParser::DetectFormat returns NotMachO for PE", "[MachOParser]")
{
    auto path = FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::NotMachO);
}

TEST_CASE("MachOParser::DetectFormat returns NotMachO for plain text", "[MachOParser]")
{
    auto path = FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::NotMachO);
}

TEST_CASE("MachOParser::IsMachO returns true for Mach-O files", "[MachOParser]")
{
    CHECK(MachOParser::IsMachO(FixturePath("tiny-macho-x86_64")) == true);
    CHECK(MachOParser::IsMachO(FixturePath("tiny-macho-arm64")) == true);
    CHECK(MachOParser::IsMachO(FixturePath("tiny-macho-universal")) == true);
}

TEST_CASE("MachOParser::IsMachO returns false for non-Mach-O files", "[MachOParser]")
{
    CHECK(MachOParser::IsMachO(FixturePath("tiny.exe")) == false);
    CHECK(MachOParser::IsMachO(FixturePath("plain.txt")) == false);
}

TEST_CASE("MachOParser::IsFatBinary returns true only for fat binaries", "[MachOParser]")
{
    CHECK(MachOParser::IsFatBinary(FixturePath("tiny-macho-universal")) == true);
    CHECK(MachOParser::IsFatBinary(FixturePath("tiny-macho-x86_64")) == false);
    CHECK(MachOParser::IsFatBinary(FixturePath("tiny-macho-arm64")) == false);
}

TEST_CASE("MachOParser::GetArchSlices returns slices for fat binary", "[MachOParser]")
{
    auto path = FixturePath("tiny-macho-universal");
    REQUIRE(std::filesystem::exists(path));

    auto slices = MachOParser::GetArchSlices(path);
    REQUIRE(slices.size() == 2);

    // First slice should be x86_64 (cpu_type 0x01000007)
    CHECK(slices[0].cpu_type == 0x01000007);
    CHECK(slices[0].offset > 0);
    CHECK(slices[0].size > 0);

    // Second slice should be arm64 (cpu_type 0x0100000C)
    CHECK(slices[1].cpu_type == 0x0100000C);
    CHECK(slices[1].offset > 0);
    CHECK(slices[1].size > 0);
}

TEST_CASE("MachOParser::GetArchSlices returns single slice for non-fat", "[MachOParser]")
{
    auto path = FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(path));

    auto slices = MachOParser::GetArchSlices(path);
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].offset == 0);
}
