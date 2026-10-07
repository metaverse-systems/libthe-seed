#include "TestPaths.hpp"
#include "DepFixtures.hpp"

#include <libthe-seed/MachOParser.hpp>

#include <filesystem>
#include <string>
#include <vector>


TEST_CASE("MachOParser::DetectFormat identifies x86_64 Mach-O", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::MachO64);
}

TEST_CASE("MachOParser::DetectFormat identifies arm64 Mach-O", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-arm64");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::MachO64);
}

TEST_CASE("MachOParser::DetectFormat identifies fat Mach-O", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-universal");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::Fat);
}

TEST_CASE("MachOParser::DetectFormat returns NotMachO for PE", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::NotMachO);
}

TEST_CASE("MachOParser::DetectFormat returns NotMachO for plain text", "[MachOParser]")
{
    auto path = seedtest::FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(path));

    auto fmt = MachOParser::DetectFormat(path);
    CHECK(fmt == MachOParser::Format::NotMachO);
}

TEST_CASE("MachOParser::IsMachO returns true for Mach-O files", "[MachOParser]")
{
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny-macho-x86_64")) == true);
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny-macho-arm64")) == true);
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny-macho-universal")) == true);
}

TEST_CASE("MachOParser::IsMachO returns false for non-Mach-O files", "[MachOParser]")
{
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("tiny.exe")) == false);
    CHECK(MachOParser::IsMachO(seedtest::FixturePath("plain.txt")) == false);
}

TEST_CASE("MachOParser::IsFatBinary returns true only for fat binaries", "[MachOParser]")
{
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-universal")) == true);
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-x86_64")) == false);
    CHECK(MachOParser::IsFatBinary(seedtest::FixturePath("tiny-macho-arm64")) == false);
}

TEST_CASE("MachOParser::GetArchSlices returns slices for fat binary", "[MachOParser]")
{
    auto path = seedtest::FixturePath("tiny-macho-universal");
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
    auto path = seedtest::FixturePath("tiny-macho-x86_64");
    REQUIRE(std::filesystem::exists(path));

    auto slices = MachOParser::GetArchSlices(path);
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].offset == 0);
}

// Library references of every kind, on synthesised files in the byte order the
// reader accepts at this point (big-endian fields after the CF FA ED FE magic
// of a single program, little-endian fields in a universal file). The form of
// these samples is revisited with roadmap task 5, which corrects the byte
// order.
namespace {

namespace dep = seedtest::dep;
using Names = std::vector<std::string>;

std::string WriteImage(const seedtest::ScratchDir &scratch, const dep::Bytes &bytes)
{
    return dep::WriteFile(scratch.Path(), "input.bin", bytes).string();
}

}

TEST_CASE("MachOParser::ListDependencies returns all five reference kinds in file order",
          "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::string path = WriteImage(scratch, dep::MachOReferencing({
        {dep::kLoadUpwardDylib, "/lib/upward"},
        {dep::kLoadDylib, "/lib/normal"},
        {dep::kLazyLoadDylib, "/lib/lazy"},
        {dep::kLoadWeakDylib, "/lib/weak"},
        {dep::kReexportDylib, "/lib/reexport"},
    }));

    CHECK(MachOParser::ListDependencies(path) ==
          Names{"/lib/upward", "/lib/normal", "/lib/lazy", "/lib/weak", "/lib/reexport"});
}

TEST_CASE("MachOParser::ListDependencies returns each single kind", "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::vector<std::uint32_t> kinds = {dep::kLoadDylib, dep::kLoadWeakDylib, dep::kReexportDylib,
                                              dep::kLazyLoadDylib, dep::kLoadUpwardDylib};
    for(const std::uint32_t kind : kinds)
    {
        DYNAMIC_SECTION("kind 0x" << std::hex << kind)
        {
            const std::string path = WriteImage(scratch, dep::MachOReferencing({{kind, "/lib/one"}}));
            CHECK(MachOParser::ListDependencies(path) == Names{"/lib/one"});
        }
    }
}

TEST_CASE("MachOParser::ListDependencies returns an empty list for a file with no references",
          "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::string path = WriteImage(scratch, dep::MachOReferencing({}));
    CHECK(MachOParser::ListDependencies(path).empty());
}

TEST_CASE("MachOParser::ListDependencies lists a name repeated across kinds once", "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const std::string path = WriteImage(scratch, dep::MachOReferencing({
        {dep::kLoadDylib, "/lib/shared"},
        {dep::kLoadWeakDylib, "/lib/other"},
        {dep::kReexportDylib, "/lib/shared"},
        {dep::kLoadDylib, "/lib/shared"},
        {dep::kLazyLoadDylib, "/lib/other"},
    }));

    CHECK(MachOParser::ListDependencies(path) == Names{"/lib/shared", "/lib/other"});
}

TEST_CASE("MachOParser::ListDependencies covers every slice of a fat file once each",
          "[MachOParser][kinds]")
{
    seedtest::ScratchDir scratch("macho-kinds");
    const dep::Bytes first = dep::MachOReferencing({
        {dep::kLoadDylib, "/lib/common"},
        {dep::kLoadWeakDylib, "/lib/first"},
    });
    const dep::Bytes second = dep::MachOReferencing({
        {dep::kLoadDylib, "/lib/common"},
        {dep::kLazyLoadDylib, "/lib/second"},
        {dep::kLoadUpwardDylib, "/lib/third"},
    });

    SECTION("two slices")
    {
        const std::string path = WriteImage(scratch, dep::MachOUniversal({first, second}, false));
        CHECK(MachOParser::ListDependencies(path) ==
              Names{"/lib/common", "/lib/first", "/lib/second", "/lib/third"});
    }

    SECTION("a slice with no references among others")
    {
        const std::string path =
            WriteImage(scratch, dep::MachOUniversal({dep::MachOReferencing({}), second, first}, false));
        CHECK(MachOParser::ListDependencies(path) ==
              Names{"/lib/common", "/lib/second", "/lib/third", "/lib/first"});
    }
}
