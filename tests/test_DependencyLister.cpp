#include "TestPaths.hpp"
#include "DepFixtures.hpp"

#include <libthe-seed/DependencyLister.hpp>

#include "../src/PeParser.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::filesystem::path WriteTempFile(const seedtest::ScratchDir &scratch, const std::string &name, const std::vector<std::uint8_t> &content)
{
    const auto path = scratch.Path() / name;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
    output.close();
    return path;
}

std::filesystem::path WriteStaticLikeElf(const seedtest::ScratchDir &scratch, const std::string &name)
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

    return WriteTempFile(scratch, name, bytes);
}

// DLL names are case-insensitive; the sample records them as the linker wrote them.
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool HasDll(const std::vector<std::string> &names, const std::string &dll)
{
    return std::any_of(names.begin(), names.end(),
                       [&](const std::string &name) { return Lower(name) == dll; });
}

bool HasDll(const std::map<std::string, std::vector<std::string>> &dependencies, const std::string &dll)
{
    return std::any_of(dependencies.begin(), dependencies.end(),
                       [&](const auto &entry) { return Lower(entry.first) == dll; });
}

// The canonical form of a file in the scratch folder, which is how the lister
// names a library it found.
std::string Canon(const std::filesystem::path &path)
{
    return std::filesystem::canonical(path).string();
}

// The scratch folder of a test, with the ELF chain copied into a folder of its
// own so the search folder holds nothing else.
struct ElfChain
{
    seedtest::ScratchDir scratch;
    std::filesystem::path folder;
    std::string app_a;
    std::string app_b;
    std::string libfoo;
    std::string libbar;
    std::string libbaz;

    ElfChain() : folder(this->scratch.Path() / "chain")
    {
        seedtest::dep::CopyElfChain(this->folder);
        this->app_a = (this->folder / "appA").string();
        this->app_b = (this->folder / "appB").string();
        this->libfoo = Canon(this->folder / "libfoo.so");
        this->libbar = Canon(this->folder / "libbar.so");
        this->libbaz = Canon(this->folder / "libbaz.so");
    }

    std::vector<std::string> Search() const { return {this->folder.string()}; }
};
} // namespace

TEST_CASE("DependencyLister extracts direct dependencies from ELF", "[DependencyLister][US1]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a}, chain.Search());

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count(chain.libfoo) == 1);
    REQUIRE(result.dependencies.at(chain.libfoo) == std::vector<std::string>{chain.app_a});
}

TEST_CASE("DependencyLister reports missing file errors", "[DependencyLister][US1]")
{
    seedtest::ScratchDir scratch;
    const std::string missing = scratch.File("file-that-does-not-exist");
    DependencyLister lister;

    const auto result = lister.ListDependencies({missing}, {scratch.Path().string()});

    REQUIRE(result.dependencies.empty());
    REQUIRE(result.errors.count(missing) == 1);
}

TEST_CASE("DependencyLister reports non-binary file errors", "[DependencyLister][US1]")
{
    seedtest::ScratchDir scratch;
    const auto text_file = scratch.Path() / "dependency_lister_non_binary.txt";
    {
        std::ofstream output(text_file);
        output << "not a binary";
    }

    DependencyLister lister;
    const auto result = lister.ListDependencies({text_file.string()}, {scratch.Path().string()});

    REQUIRE(result.dependencies.empty());
    REQUIRE(result.errors.count(text_file.string()) == 1);
}

TEST_CASE("DependencyLister continues processing after per-file errors", "[DependencyLister][US1]")
{
    ElfChain chain;
    const std::string missing = chain.scratch.File("missing-binary-for-dependency-lister");
    DependencyLister lister;

    const auto result = lister.ListDependencies({missing, chain.app_a}, chain.Search());

    REQUIRE(result.errors.count(missing) == 1);
    REQUIRE(result.errors.size() == 1);
    REQUIRE(result.dependencies.count(chain.libfoo) == 1);
}

TEST_CASE("ELF file without PT_DYNAMIC produces empty dependencies", "[DependencyLister][US1]")
{
    seedtest::ScratchDir scratch;
    const auto static_like_elf = WriteStaticLikeElf(scratch, "dependency_lister_static_like.elf");

    DependencyLister lister;
    const auto result = lister.ListDependencies({static_like_elf.string()}, {scratch.Path().string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.empty());
}

TEST_CASE("DependencyLister resolves transitive dependencies", "[DependencyLister][US2]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a}, chain.Search());

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count(chain.libfoo) == 1);
    REQUIRE(result.dependencies.count(chain.libbar) == 1);
    REQUIRE(result.dependencies.count(chain.libbaz) == 1);
}

TEST_CASE("DependencyLister uses canonical absolute keys when resolvable", "[DependencyLister][US2]")
{
    ElfChain chain;
    // Reach the folder through a spelling with a dot segment: the key is
    // still the canonical path of the file.
    const std::string indirect = (chain.folder / ".").string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a}, {indirect});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count(chain.libfoo) == 1);
    REQUIRE(result.dependencies.count(chain.libbar) == 1);
    for(const auto &entry : result.dependencies)
    {
        if(entry.first.front() == '/')
        {
            REQUIRE(std::filesystem::path(entry.first).is_absolute());
            REQUIRE(entry.first == Canon(entry.first));
        }
    }
}

TEST_CASE("DependencyLister aggregates shared dependencies", "[DependencyLister][US2]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a, chain.app_b}, chain.Search());

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count(chain.libfoo) == 1);
    REQUIRE(result.dependencies.at(chain.libfoo) == std::vector<std::string>{chain.app_a, chain.app_b});
}

TEST_CASE("DependencyLister uses recorded name when unresolved", "[DependencyLister][US2]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a}, {});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count("libfoo.so") == 1);
    REQUIRE(result.dependencies.at("libfoo.so") == std::vector<std::string>{chain.app_a});
    // Nothing is found, so nothing beyond the program's own references.
    REQUIRE(result.dependencies.count("libbar.so") == 0);
}

TEST_CASE("DependencyLister searches only the given folders", "[DependencyLister][US1]")
{
    ElfChain chain;
    // A decoy folder with a copy of the whole chain, not named in the request,
    // and an empty folder that is: every library found must come from the
    // named folder, and no system folder may be added.
    seedtest::dep::CopyElfChain(chain.scratch.Path() / "decoy");
    const std::string empty = (chain.scratch.Path() / "empty").string();
    std::filesystem::create_directories(empty);
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a}, {empty, chain.folder.string()});

    REQUIRE(result.errors.empty());
    const std::string decoy_root = Canon(chain.scratch.Path() / "decoy");
    const std::string chain_root = Canon(chain.folder);
    for(const auto &entry : result.dependencies)
    {
        INFO("key " << entry.first);
        REQUIRE(entry.first.find(decoy_root) == std::string::npos);
        if(entry.first.front() == '/')
        {
            REQUIRE(entry.first.compare(0, chain_root.size(), chain_root) == 0);
        }
        else
        {
            // Not found anywhere named: the recorded name, never a system path.
            REQUIRE(entry.first.find('/') == std::string::npos);
        }
    }
    REQUIRE(result.dependencies.count(chain.libfoo) == 1);

    // With only the empty folder, nothing is found at all.
    const auto none = lister.ListDependencies({chain.app_a}, {empty});
    for(const auto &entry : none.dependencies)
    {
        REQUIRE(entry.first.front() != '/');
    }
}

TEST_CASE("PeParser reads DLL dependencies from fixture", "[DependencyLister][US3]")
{
    const auto dependencies = PeParser::ListDependencies(seedtest::FixturePath("test.dll"));

    REQUIRE(HasDll(dependencies, "kernel32.dll"));
    REQUIRE(HasDll(dependencies, "msvcrt.dll"));
}

TEST_CASE("DependencyLister auto-detects PE format", "[DependencyLister][US3]")
{
    DependencyLister lister;

    const auto result = lister.ListDependencies({seedtest::FixturePath("test.dll")}, {});

    REQUIRE(result.errors.empty());
    REQUIRE(HasDll(result.dependencies, "kernel32.dll"));
    REQUIRE(HasDll(result.dependencies, "msvcrt.dll"));
}

TEST_CASE("DependencyLister resolves the PE fixture chain", "[DependencyLister][US3]")
{
    seedtest::ScratchDir scratch;
    const auto folder = seedtest::dep::CopyPeChain(scratch.Path() / "pe");
    const std::string app = (folder / "appA.exe").string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({app}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.count(Canon(folder / "libfoo.dll")) == 1);
    REQUIRE(result.dependencies.count(Canon(folder / "libbar.dll")) == 1);
    REQUIRE(result.dependencies.count(Canon(folder / "libbaz.dll")) == 1);
    REQUIRE(result.dependencies.at(Canon(folder / "libbaz.dll")) == std::vector<std::string>{app});
}

TEST_CASE("DependencyLister reports errors for truncated PE files", "[DependencyLister][US3]")
{
    seedtest::ScratchDir scratch;
    const auto corrupt_file = WriteTempFile(scratch, "dependency_lister_truncated.dll", {'M', 'Z'});

    DependencyLister lister;
    const auto result = lister.ListDependencies({corrupt_file.string()}, {});

    REQUIRE(result.dependencies.empty());
    REQUIRE(result.errors.count(corrupt_file.string()) == 1);
}
