#include <catch_amalgamated.hpp>

#include <libthe-seed/DependencyLister.hpp>

#include "../src/PeParser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> DefaultSearchPaths()
{
    return {
        "/usr/lib/x86_64-linux-gnu",
        "/lib/x86_64-linux-gnu",
        "/usr/lib64",
        "/lib64",
        "/usr/lib",
        "/lib",
    };
}

std::string ExistingElfBinaryPath()
{
    const std::string built_library = "../src/.libs/libthe-seed.so";
    if(std::filesystem::exists(built_library))
    {
        return built_library;
    }

    return "/bin/ls";
}

std::filesystem::path WriteTempFile(const std::string &name, const std::vector<std::uint8_t> &content)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
    output.close();
    return path;
}

std::filesystem::path WriteStaticLikeElf(const std::string &name)
{
    std::vector<std::uint8_t> bytes(64 + 56, 0);

    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;

    auto write16 = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };

    auto write32 = [&](std::size_t offset, std::uint32_t value) {
        for(std::size_t i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
    };

    auto write64 = [&](std::size_t offset, std::uint64_t value) {
        for(std::size_t i = 0; i < 8; ++i)
        {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
    };

    write16(16, 3);
    write16(18, 62);
    write32(20, 1);
    write64(32, 64);
    write16(52, 64);
    write16(54, 56);
    write16(56, 1);

    write32(64, 1);
    write64(72, 0);
    write64(80, 0x400000);
    write64(96, 0);
    write64(104, 0);

    return WriteTempFile(name, bytes);
}

bool ContainsKeyFragment(const std::map<std::string, std::vector<std::string>> &dependencies, const std::string &needle)
{
    return std::any_of(
        dependencies.begin(),
        dependencies.end(),
        [&](const auto &entry) { return entry.first.find(needle) != std::string::npos; }
    );
}
} // namespace

TEST_CASE("DependencyLister extracts direct dependencies from ELF", "[DependencyLister][US1]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({ExistingElfBinaryPath()}, DefaultSearchPaths());

    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.dependencies.empty());
}

TEST_CASE("DependencyLister reports missing file errors", "[DependencyLister][US1]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({"/tmp/file-that-does-not-exist-1234567"}, DefaultSearchPaths());

    REQUIRE(result.dependencies.empty());
    REQUIRE(result.errors.count("/tmp/file-that-does-not-exist-1234567") == 1);
}

TEST_CASE("DependencyLister reports non-binary file errors", "[DependencyLister][US1]")
{
    const auto text_file = std::filesystem::temp_directory_path() / "dependency_lister_non_binary.txt";
    {
        std::ofstream output(text_file);
        output << "not a binary";
    }

    DependencyLister lister;
    const auto result = lister.ListDependencies({text_file.string()}, DefaultSearchPaths());

    REQUIRE(result.dependencies.empty());
    REQUIRE(result.errors.count(text_file.string()) == 1);

    std::filesystem::remove(text_file);
}

TEST_CASE("DependencyLister continues processing after per-file errors", "[DependencyLister][US1]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies(
        {
            "/tmp/missing-binary-for-dependency-lister",
            ExistingElfBinaryPath(),
        },
        DefaultSearchPaths()
    );

    REQUIRE(result.errors.count("/tmp/missing-binary-for-dependency-lister") == 1);
    REQUIRE_FALSE(result.dependencies.empty());
}

TEST_CASE("ELF file without PT_DYNAMIC produces empty dependencies", "[DependencyLister][US1]")
{
    const auto static_like_elf = WriteStaticLikeElf("dependency_lister_static_like.elf");

    DependencyLister lister;
    const auto result = lister.ListDependencies({static_like_elf.string()}, DefaultSearchPaths());

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.empty());

    std::filesystem::remove(static_like_elf);
}

TEST_CASE("DependencyLister resolves transitive dependencies", "[DependencyLister][US2]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({ExistingElfBinaryPath()}, DefaultSearchPaths());

    REQUIRE(result.errors.empty());
    REQUIRE(ContainsKeyFragment(result.dependencies, "libc.so"));
}

TEST_CASE("DependencyLister uses canonical absolute keys when resolvable", "[DependencyLister][US2]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({ExistingElfBinaryPath()}, DefaultSearchPaths());

    REQUIRE(result.errors.empty());

    bool found_absolute = false;
    for(const auto &entry : result.dependencies)
    {
        if(!entry.first.empty() && entry.first.front() == '/')
        {
            found_absolute = true;
            REQUIRE(std::filesystem::path(entry.first).is_absolute());
        }
    }

    REQUIRE(found_absolute);
}

TEST_CASE("DependencyLister aggregates shared dependencies", "[DependencyLister][US2]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({"/bin/ls", "/bin/cat"}, DefaultSearchPaths());

    REQUIRE(result.errors.empty());

    bool found_shared = false;
    for(const auto &entry : result.dependencies)
    {
        if(entry.first.find("libc.so") != std::string::npos)
        {
            const auto &dependents = entry.second;
            const bool has_ls = std::find(dependents.begin(), dependents.end(), "/bin/ls") != dependents.end();
            const bool has_cat = std::find(dependents.begin(), dependents.end(), "/bin/cat") != dependents.end();
            if(has_ls && has_cat)
            {
                found_shared = true;
                break;
            }
        }
    }

    REQUIRE(found_shared);
}

TEST_CASE("DependencyLister uses recorded name when unresolved", "[DependencyLister][US2]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({"/bin/ls"}, {});

    REQUIRE(result.errors.empty());
    REQUIRE(ContainsKeyFragment(result.dependencies, "lib"));
}

TEST_CASE("PeParser reads DLL dependencies from fixture", "[DependencyLister][US3]")
{
    const auto dependencies = PeParser::ListDependencies("fixtures/test.dll");

    REQUIRE(std::find(dependencies.begin(), dependencies.end(), "kernel32.dll") != dependencies.end());
    REQUIRE(std::find(dependencies.begin(), dependencies.end(), "msvcrt.dll") != dependencies.end());
}

TEST_CASE("DependencyLister auto-detects PE format", "[DependencyLister][US3]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({"fixtures/test.dll"}, {});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count("kernel32.dll") == 1);
    REQUIRE(result.dependencies.count("msvcrt.dll") == 1);
}

TEST_CASE("DependencyLister reports errors for truncated PE files", "[DependencyLister][US3]")
{
    const auto corrupt_file = WriteTempFile("dependency_lister_truncated.dll", {'M', 'Z'});

    DependencyLister lister;
    const auto result = lister.ListDependencies({corrupt_file.string()}, {});

    REQUIRE(result.dependencies.empty());
    REQUIRE(result.errors.count(corrupt_file.string()) == 1);

    std::filesystem::remove(corrupt_file);
}
