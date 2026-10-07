#include "TestPaths.hpp"
#include "DepFixtures.hpp"

#include <libthe-seed/DependencyLister.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
namespace dep = seedtest::dep;
using Deps = std::map<std::string, std::vector<std::string>>;

std::string Canon(const std::filesystem::path &path)
{
    return std::filesystem::canonical(path).string();
}

// The libraries credited to `input` in a result.
std::set<std::string> Slice(const DependencyResult &result, const std::string &input)
{
    std::set<std::string> keys;
    for(const auto &entry : result.dependencies)
    {
        if(std::find(entry.second.begin(), entry.second.end(), input) != entry.second.end())
        {
            keys.insert(entry.first);
        }
    }
    return keys;
}

// The ELF chain in a folder of its own: libbaz <- libbar <- libfoo <- appA, appB.
struct ElfChain
{
    seedtest::ScratchDir scratch;
    std::filesystem::path folder;
    std::string app_a;
    std::string app_b;
    std::string libfoo_input; // libfoo.so as an input (its spelling in the folder)
    std::string libfoo;       // canonical keys
    std::string libbar;
    std::string libbaz;

    ElfChain() : folder(this->scratch.Path() / "chain")
    {
        dep::CopyElfChain(this->folder);
        this->app_a = (this->folder / "appA").string();
        this->app_b = (this->folder / "appB").string();
        this->libfoo_input = (this->folder / "libfoo.so").string();
        this->libfoo = Canon(this->folder / "libfoo.so");
        this->libbar = Canon(this->folder / "libbar.so");
        this->libbaz = Canon(this->folder / "libbaz.so");
    }

    std::vector<std::string> Search() const { return {this->folder.string()}; }
};

std::string Numbered(const std::string &prefix, int index, const std::string &suffix)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%05d", index);
    return prefix + text + suffix;
}

double SecondsFor(DependencyLister &lister, const std::vector<std::string> &inputs,
                  const std::vector<std::string> &search, DependencyResult *keep = nullptr)
{
    double best = 1e9;
    for(int run = 0; run < 3; ++run)
    {
        const auto begin = std::chrono::steady_clock::now();
        DependencyResult result = lister.ListDependencies(inputs, search);
        const auto end = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(end - begin).count());
        if(keep != nullptr && run == 0)
        {
            *keep = std::move(result);
        }
    }
    return best;
}
} // namespace

TEST_CASE("Two programs sharing a chain are both credited with all of it", "[DependencyAttribution][US1]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto result = lister.ListDependencies({chain.app_a, chain.app_b}, chain.Search());

    REQUIRE(result.errors.empty());
    const std::vector<std::string> both{chain.app_a, chain.app_b};
    for(const std::string &key : {chain.libfoo, chain.libbar, chain.libbaz})
    {
        INFO("library " << key);
        REQUIRE(result.dependencies.count(key) == 1);
        REQUIRE(result.dependencies.at(key) == both);
    }
    // Nothing else was found.
    REQUIRE(result.dependencies.size() == 3);
    REQUIRE(result.libraryErrors.empty());
}

TEST_CASE("The same programs in the opposite order give the same result", "[DependencyAttribution][US1]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto forward = lister.ListDependencies({chain.app_a, chain.app_b}, chain.Search());
    const auto backward = lister.ListDependencies({chain.app_b, chain.app_a}, chain.Search());

    REQUIRE(forward.dependencies == backward.dependencies);
    REQUIRE(forward.errors == backward.errors);
}

TEST_CASE("Every order of appA and appB and libfoo gives one result", "[DependencyAttribution][US1]")
{
    ElfChain chain;
    DependencyLister lister;
    std::vector<std::string> inputs{chain.app_a, chain.app_b, chain.libfoo_input};
    std::sort(inputs.begin(), inputs.end());

    std::vector<DependencyResult> results;
    do
    {
        results.push_back(lister.ListDependencies(inputs, chain.Search()));
    } while(std::next_permutation(inputs.begin(), inputs.end()));

    REQUIRE(results.size() == 6);
    for(const DependencyResult &result : results)
    {
        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies == results.front().dependencies);
        REQUIRE(result.libraryErrors.size() == results.front().libraryErrors.size());
    }

    // The libraries of the chain list every input that reaches them, in sorted order.
    const Deps &joint = results.front().dependencies;
    std::vector<std::string> all{chain.app_a, chain.app_b, chain.libfoo_input};
    std::sort(all.begin(), all.end());
    std::vector<std::string> programs{chain.app_a, chain.app_b};
    REQUIRE(joint.at(chain.libfoo) == programs);
    for(const std::string &key : {chain.libbar, chain.libbaz})
    {
        INFO("library " << key);
        REQUIRE(joint.at(key) == all);
    }
}

TEST_CASE("Each input alone equals its slice of the joint result", "[DependencyAttribution][US1]")
{
    ElfChain chain;
    DependencyLister lister;
    const std::vector<std::string> inputs{chain.app_a, chain.app_b, chain.libfoo_input};

    const auto joint = lister.ListDependencies(inputs, chain.Search());

    REQUIRE(joint.errors.empty());
    for(const std::string &input : inputs)
    {
        INFO("input " << input);
        const auto alone = lister.ListDependencies({input}, chain.Search());
        REQUIRE(alone.errors.empty());
        REQUIRE_FALSE(alone.dependencies.empty());
        REQUIRE(Slice(alone, input).size() == alone.dependencies.size());
        REQUIRE(Slice(joint, input) == Slice(alone, input));
    }
}

TEST_CASE("Many programs sharing many libraries read each file once", "[DependencyAttribution][US1][cost]")
{
    constexpr int kCount = 200;
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "many";

    // lib00000 <- lib00001 <- ... : each library needs the next one.
    for(int i = 0; i < kCount; ++i)
    {
        std::vector<std::string> needed;
        if(i + 1 < kCount)
        {
            needed.push_back(Numbered("lib", i + 1, ".so"));
        }
        dep::WriteFile(folder, Numbered("lib", i, ".so"), dep::ElfNeeding(needed));
    }
    // Program j needs library j and library (7 j) mod 200.
    std::vector<std::string> programs;
    for(int j = 0; j < kCount; ++j)
    {
        const std::string name = Numbered("prog", j, "");
        dep::WriteFile(folder, name,
                       dep::ElfNeeding({Numbered("lib", j, ".so"), Numbered("lib", (j * 7) % kCount, ".so")}));
        programs.push_back((folder / name).string());
    }
    // The same files as one program that needs every library.
    std::vector<std::string> every_library;
    for(int i = 0; i < kCount; ++i)
    {
        every_library.push_back(Numbered("lib", i, ".so"));
    }
    const std::string combined = dep::WriteFile(folder, "combined", dep::ElfNeeding(every_library)).string();
    const std::vector<std::string> search{folder.string()};
    DependencyLister lister;

    SECTION("each distinct file is read once")
    {
        // 200 programs and 200 libraries.
        dep::ResetReadCount();
        const auto result = lister.ListDependencies(programs, search);
        REQUIRE(result.errors.empty());
        REQUIRE(dep::ReadCount() == static_cast<std::uint64_t>(2 * kCount));
    }

    SECTION("every program is credited with its whole closure")
    {
        const auto result = lister.ListDependencies(programs, search);
        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == static_cast<std::size_t>(kCount));
        // Every program reaches the end of the chain; only program 0 needs library 0.
        std::vector<std::string> sorted_programs = programs;
        std::sort(sorted_programs.begin(), sorted_programs.end());
        REQUIRE(result.dependencies.at(Canon(folder / Numbered("lib", kCount - 1, ".so"))) == sorted_programs);
        REQUIRE(result.dependencies.at(Canon(folder / Numbered("lib", 0, ".so"))) ==
                std::vector<std::string>{programs.front()});
    }

    SECTION("the time is at most twice that of one combined program")
    {
        // Best of three runs each; a floor of 20 ms keeps a request that takes
        // a few milliseconds from failing on timer noise.
        const double combined_seconds = SecondsFor(lister, {combined}, search);
        const double many_seconds = SecondsFor(lister, programs, search);
        INFO("combined " << combined_seconds << " s, many " << many_seconds << " s");
        REQUIRE(many_seconds <= 2.0 * std::max(combined_seconds, 0.020));
    }
}

TEST_CASE("A chain of 10000 libraries is listed without exhausting the stack", "[DependencyAttribution][US1][depth]")
{
    constexpr int kDepth = 10000;
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "deep";

    for(int i = 0; i < kDepth; ++i)
    {
        std::vector<std::string> needed;
        if(i + 1 < kDepth)
        {
            needed.push_back(Numbered("lib", i + 1, ".so"));
        }
        dep::WriteFile(folder, Numbered("lib", i, ".so"), dep::ElfNeeding(needed));
    }
    const std::string program = dep::WriteFile(folder, "program", dep::ElfNeeding({Numbered("lib", 0, ".so")})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.size() == static_cast<std::size_t>(kDepth));
    REQUIRE(result.dependencies.at(Canon(folder / Numbered("lib", kDepth - 1, ".so"))) ==
            std::vector<std::string>{program});
}

TEST_CASE("An input that is also a library is credited with its own dependencies", "[DependencyAttribution][US1]")
{
    ElfChain chain;
    DependencyLister lister;

    for(const auto &inputs : {std::vector<std::string>{chain.app_a, chain.libfoo_input},
                              std::vector<std::string>{chain.libfoo_input, chain.app_a}})
    {
        const auto result = lister.ListDependencies(inputs, chain.Search());

        REQUIRE(result.errors.empty());
        // Listed under the other input as a library ...
        REQUIRE(result.dependencies.at(chain.libfoo) == std::vector<std::string>{chain.app_a});
        // ... and credited with what it needs as an input in its own right.
        for(const std::string &key : {chain.libbar, chain.libbaz})
        {
            INFO("library " << key);
            std::vector<std::string> expected{chain.app_a, chain.libfoo_input};
            std::sort(expected.begin(), expected.end());
            REQUIRE(result.dependencies.at(key) == expected);
        }
    }
}

TEST_CASE("The same input named twice is one input", "[DependencyAttribution][US1]")
{
    ElfChain chain;
    DependencyLister lister;

    const auto once = lister.ListDependencies({chain.app_a}, chain.Search());
    const auto twice = lister.ListDependencies({chain.app_a, chain.app_a}, chain.Search());
    const auto mixed = lister.ListDependencies({chain.app_a, chain.app_b, chain.app_a}, chain.Search());

    REQUIRE(twice.dependencies == once.dependencies);
    REQUIRE(twice.errors == once.errors);
    for(const auto &entry : mixed.dependencies)
    {
        INFO("library " << entry.first);
        REQUIRE(entry.second == std::vector<std::string>{chain.app_a, chain.app_b});
    }
}

TEST_CASE("A library that cannot be opened is still credited to every program", "[DependencyAttribution][US1][permission]")
{
    if(dep::RunningAsRoot())
    {
        dep::Skip("the permission case needs a non-administrator user (mode 000 does not stop root)");
        return;
    }
    ElfChain chain;
    std::error_code ec;
    std::filesystem::permissions(chain.folder / "libbar.so", std::filesystem::perms::none, ec);
    REQUIRE_FALSE(ec);
    DependencyLister lister;

    const auto forward = lister.ListDependencies({chain.app_a, chain.app_b}, chain.Search());
    const auto backward = lister.ListDependencies({chain.app_b, chain.app_a}, chain.Search());

    REQUIRE(forward.dependencies == backward.dependencies);
    REQUIRE(forward.errors.empty());
    const std::vector<std::string> both{chain.app_a, chain.app_b};
    REQUIRE(forward.dependencies.at(chain.libfoo) == both);
    REQUIRE(forward.dependencies.at(chain.libbar) == both);
    // Behind the unreadable library nothing can be listed.
    REQUIRE(forward.dependencies.count(chain.libbaz) == 0);
}

// ---------------------------------------------------------------------------
// Loops, aliases, letter case, absent libraries and names that try to leave
// the search folders.
// ---------------------------------------------------------------------------

namespace {
std::vector<std::string> Sorted(std::vector<std::string> values)
{
    std::sort(values.begin(), values.end());
    return values;
}

// Writes the named PE files (no imports) into `folder`, in the order given, and
// returns false, after a SKIPPED line, when the file system folded two names
// that differ only in letter case into one file.
bool WritePeFilesDifferingInCase(const std::filesystem::path &folder, const std::vector<std::string> &names)
{
    for(const std::string &name : names)
    {
        dep::WriteFile(folder, name, dep::PeImporting({}));
    }
    std::size_t count = 0;
    for(const auto &entry : std::filesystem::directory_iterator(folder))
    {
        (void)entry;
        ++count;
    }
    if(count != names.size())
    {
        dep::Skip("the file system does not keep file names that differ only in letter case apart");
        return false;
    }
    return true;
}

// True when the file system treats "name" and its upper-case spelling as one file.
bool FoldsCase(const std::filesystem::path &folder, const std::string &name, const std::string &other_spelling)
{
    return std::filesystem::exists(folder / other_spelling) && std::filesystem::exists(folder / name);
}
} // namespace

TEST_CASE("Libraries needing each other in a loop are each credited once, in any order of inputs",
          "[DependencyAttribution][US6][loop]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "loop";
    dep::WriteFile(folder, "libx.so", dep::ElfNeeding({"liby.so"}));
    dep::WriteFile(folder, "liby.so", dep::ElfNeeding({"libx.so"}));
    const std::string by_x = dep::WriteFile(folder, "progx", dep::ElfNeeding({"libx.so"})).string();
    const std::string by_y = dep::WriteFile(folder, "progy", dep::ElfNeeding({"liby.so"})).string();
    const std::string libx = Canon(folder / "libx.so");
    const std::string liby = Canon(folder / "liby.so");
    const std::vector<std::string> search{folder.string()};
    DependencyLister lister;

    SECTION("one program")
    {
        dep::ResetReadCount();
        const auto result = lister.ListDependencies({by_x}, search);
        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
        REQUIRE(result.dependencies.size() == 2);
        REQUIRE(result.dependencies.at(libx) == std::vector<std::string>{by_x});
        REQUIRE(result.dependencies.at(liby) == std::vector<std::string>{by_x});
        // The program and the two libraries, each read once.
        REQUIRE(dep::ReadCount() == 3);
    }

    SECTION("two programs entering the loop at different places, in both orders")
    {
        const std::vector<std::string> both = Sorted({by_x, by_y});
        std::vector<std::string> inputs = both;
        do
        {
            const auto result = lister.ListDependencies(inputs, search);
            REQUIRE(result.errors.empty());
            REQUIRE(result.dependencies.size() == 2);
            REQUIRE(result.dependencies.at(libx) == both);
            REQUIRE(result.dependencies.at(liby) == both);
        } while(std::next_permutation(inputs.begin(), inputs.end()));
    }

    SECTION("a loop of three with a tail")
    {
        dep::WriteFile(folder, "liba.so", dep::ElfNeeding({"libb.so"}));
        dep::WriteFile(folder, "libb.so", dep::ElfNeeding({"libc.so"}));
        dep::WriteFile(folder, "libc.so", dep::ElfNeeding({"liba.so", "libtail.so"}));
        dep::WriteFile(folder, "libtail.so", dep::ElfNeeding({}));
        const std::string program = dep::WriteFile(folder, "progloop", dep::ElfNeeding({"libb.so"})).string();

        const auto result = lister.ListDependencies({program}, search);

        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == 4);
        for(const char *name : {"liba.so", "libb.so", "libc.so", "libtail.so"})
        {
            INFO("library " << name);
            REQUIRE(result.dependencies.at(Canon(folder / name)) == std::vector<std::string>{program});
        }
    }

    SECTION("a loop of Windows libraries, matched without regard to case")
    {
        dep::WriteFile(folder, "ping.dll", dep::PeImporting({"PONG.DLL"}));
        dep::WriteFile(folder, "pong.dll", dep::PeImporting({"Ping.dll"}));
        const std::string program = dep::WriteFile(folder, "prog.exe", dep::PeImporting({"ping.dll"})).string();

        const auto result = lister.ListDependencies({program}, search);

        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == 2);
        REQUIRE(result.dependencies.at(Canon(folder / "ping.dll")) == std::vector<std::string>{program});
        REQUIRE(result.dependencies.at(Canon(folder / "pong.dll")) == std::vector<std::string>{program});
    }
}

TEST_CASE("A library needing itself is listed once", "[DependencyAttribution][US6][loop]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "self";
    dep::WriteFile(folder, "libself.so", dep::ElfNeeding({"libself.so", "libself.so"}));
    const std::string program = dep::WriteFile(folder, "prog", dep::ElfNeeding({"libself.so"})).string();
    DependencyLister lister;

    dep::ResetReadCount();
    const auto result = lister.ListDependencies({program}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.size() == 1);
    REQUIRE(result.dependencies.at(Canon(folder / "libself.so")) == std::vector<std::string>{program});
    REQUIRE(dep::ReadCount() == 2);
}

TEST_CASE("A library in a loop that is also an input is credited as a library and an input",
          "[DependencyAttribution][US6][loop]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "loopinput";
    const std::string libx_input = dep::WriteFile(folder, "libx.so", dep::ElfNeeding({"liby.so"})).string();
    dep::WriteFile(folder, "liby.so", dep::ElfNeeding({"libx.so"}));
    const std::string program = dep::WriteFile(folder, "prog", dep::ElfNeeding({"libx.so"})).string();
    DependencyLister lister;
    const std::vector<std::string> both = Sorted({libx_input, program});

    for(const auto &inputs : {std::vector<std::string>{program, libx_input}, std::vector<std::string>{libx_input, program}})
    {
        const auto result = lister.ListDependencies(inputs, {folder.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == 2);
        // The input reaches itself through the loop, so it is credited with both.
        REQUIRE(result.dependencies.at(Canon(folder / "libx.so")) == both);
        REQUIRE(result.dependencies.at(Canon(folder / "liby.so")) == both);
    }
}

TEST_CASE("A link and its target are one library, credited with every input and read once",
          "[DependencyAttribution][US6][alias]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "alias";
    dep::WriteFile(folder, "libx.so", dep::ElfNeeding({}));
    if(!dep::MakeLink("libx.so", folder / "libx.so.1"))
    {
        return;
    }
    const std::string direct = dep::WriteFile(folder, "direct", dep::ElfNeeding({"libx.so"})).string();
    dep::WriteFile(folder, "libmid.so", dep::ElfNeeding({"libx.so.1"}));
    const std::string indirect = dep::WriteFile(folder, "indirect", dep::ElfNeeding({"libmid.so"})).string();
    const std::string both_names = dep::WriteFile(folder, "both", dep::ElfNeeding({"libx.so.1", "libx.so"})).string();
    const std::string target = Canon(folder / "libx.so");
    DependencyLister lister;

    dep::ResetReadCount();
    const auto result = lister.ListDependencies({direct, indirect, both_names}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    // libx.so (once, under the real file's path) and libmid.so; no entry for the link.
    REQUIRE(result.dependencies.size() == 2);
    REQUIRE(result.dependencies.count((folder / "libx.so.1").string()) == 0);
    REQUIRE(result.dependencies.at(target) == Sorted({direct, indirect, both_names}));
    REQUIRE(result.dependencies.at(Canon(folder / "libmid.so")) == std::vector<std::string>{indirect});
    // Three programs, libmid.so and libx.so: five files, each read once.
    REQUIRE(dep::ReadCount() == 5);
}

TEST_CASE("A link in a search folder is listed under the real file's path", "[DependencyAttribution][US6][alias]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path real = scratch.Path() / "real";
    const std::filesystem::path other = scratch.Path() / "other";
    dep::WriteFile(real, "libreal.so", dep::ElfNeeding({}));
    std::filesystem::create_directories(other);
    if(!dep::MakeLink(real / "libreal.so", other / "libalias.so"))
    {
        return;
    }
    const std::string program = dep::WriteFile(scratch.Path() / "prog", "prog", dep::ElfNeeding({"libalias.so"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {other.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.size() == 1);
    REQUIRE(result.dependencies.at(Canon(real / "libreal.so")) == std::vector<std::string>{program});
}

TEST_CASE("The same file reached through two search folders is one entry", "[DependencyAttribution][US6][alias]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path real = scratch.Path() / "real";
    const std::filesystem::path linked = scratch.Path() / "linked";
    dep::WriteFile(real, "libshared.so", dep::ElfNeeding({}));
    if(!dep::MakeLink(real, linked))
    {
        return;
    }
    const std::string program = dep::WriteFile(scratch.Path() / "prog", "prog", dep::ElfNeeding({"libshared.so"})).string();
    DependencyLister lister;
    const std::string key = Canon(real / "libshared.so");

    for(const std::vector<std::string> &search :
        {std::vector<std::string>{linked.string(), real.string()}, std::vector<std::string>{real.string(), linked.string()},
         std::vector<std::string>{(real / ".." / "real").string(), linked.string()}})
    {
        const auto result = lister.ListDependencies({program}, search);

        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == 1);
        REQUIRE(result.dependencies.at(key) == std::vector<std::string>{program});
    }
}

TEST_CASE("Two names of one file by a hard link are each listed under the real path of the name found",
          "[DependencyAttribution][US6][alias]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "hard";
    dep::WriteFile(folder, "libone.so", dep::ElfNeeding({}));
    if(!dep::MakeHardLink(folder / "libone.so", folder / "libtwo.so"))
    {
        return;
    }
    const std::string first = dep::WriteFile(folder, "first", dep::ElfNeeding({"libone.so"})).string();
    const std::string second = dep::WriteFile(folder, "second", dep::ElfNeeding({"libtwo.so"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({first, second}, {folder.string()});

    // A hard link is a second directory entry with no pointer to the first, so
    // each name keeps its own canonical path; both programs are credited.
    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.at(Canon(folder / "libone.so")) == std::vector<std::string>{first});
    REQUIRE(result.dependencies.at(Canon(folder / "libtwo.so")) == std::vector<std::string>{second});
    REQUIRE(result.dependencies.size() == 2);
}

TEST_CASE("A Windows reference in another letter case finds the file and is listed under the file's path",
          "[DependencyAttribution][US6][case]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "pecase";
    dep::WriteFile(folder, "mylib.dll", dep::PeImporting({}));
    dep::WriteFile(folder, "Other.DLL", dep::PeImporting({}));
    const std::string program = dep::WriteFile(folder, "prog.exe", dep::PeImporting({"MyLib.DLL", "OTHER.dll"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.size() == 2);
    REQUIRE(result.dependencies.at(Canon(folder / "mylib.dll")) == std::vector<std::string>{program});
    REQUIRE(result.dependencies.at(Canon(folder / "Other.DLL")) == std::vector<std::string>{program});
    REQUIRE(result.dependencies.count("MyLib.DLL") == 0);
    REQUIRE(result.dependencies.count("OTHER.dll") == 0);
}

TEST_CASE("Two spellings of one Windows library are one entry", "[DependencyAttribution][US6][case]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "twospell";
    dep::WriteFile(folder, "shared.dll", dep::PeImporting({}));
    const std::string one = dep::WriteFile(folder, "one.exe", dep::PeImporting({"SHARED.DLL"})).string();
    const std::string two = dep::WriteFile(folder, "two.exe", dep::PeImporting({"Shared.dll"})).string();
    DependencyLister lister;

    dep::ResetReadCount();
    const auto result = lister.ListDependencies({one, two}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.size() == 1);
    REQUIRE(result.dependencies.at(Canon(folder / "shared.dll")) == Sorted({one, two}));
    REQUIRE(dep::ReadCount() == 3);
}

TEST_CASE("Windows files differing only in case: the exact case wins, otherwise the smallest by byte order",
          "[DependencyAttribution][US6][case]")
{
    seedtest::ScratchDir scratch;
    DependencyLister lister;

    // "LIB.dll" < "Lib.dll" < "lib.dll" in byte order. The files are written in
    // two different orders so that the answer cannot follow the creation order.
    const std::vector<std::vector<std::string>> orders{{"lib.dll", "Lib.dll", "LIB.dll"},
                                                       {"LIB.dll", "Lib.dll", "lib.dll"}};
    int index = 0;
    for(const std::vector<std::string> &order : orders)
    {
        const std::filesystem::path folder = scratch.Path() / ("variants" + std::to_string(index++));
        if(!WritePeFilesDifferingInCase(folder, order))
        {
            return;
        }
        const struct
        {
            const char *recorded;
            const char *chosen;
        } cases[] = {{"Lib.dll", "Lib.dll"}, {"lib.dll", "lib.dll"}, {"LIB.dll", "LIB.dll"},
                     {"lIb.dll", "LIB.dll"}, {"LiB.DLL", "LIB.dll"}};
        for(const auto &one : cases)
        {
            INFO("recorded " << one.recorded << " in folder " << folder.string());
            const std::string program =
                dep::WriteFile(scratch.Path() / "programs", std::string("p") + std::to_string(index) + one.recorded + ".exe",
                               dep::PeImporting({one.recorded}))
                    .string();
            const auto result = lister.ListDependencies({program}, {folder.string()});
            REQUIRE(result.errors.empty());
            REQUIRE(result.dependencies.size() == 1);
            REQUIRE(result.dependencies.at(Canon(folder / one.chosen)) == std::vector<std::string>{program});
        }
    }
}

TEST_CASE("Windows files differing only in case: without the exact case the smallest by byte order is chosen",
          "[DependencyAttribution][US6][case]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "nolower";
    // "Lib.dll" < "lib.dll" ('L' is 0x4C, 'l' is 0x6C).
    if(!WritePeFilesDifferingInCase(folder, {"lib.dll", "Lib.dll"}))
    {
        return;
    }
    const std::string program = dep::WriteFile(scratch.Path() / "prog", "p.exe", dep::PeImporting({"LiB.dll"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {folder.string()});

    REQUIRE(result.dependencies.size() == 1);
    REQUIRE(result.dependencies.at(Canon(folder / "Lib.dll")) == std::vector<std::string>{program});
}

TEST_CASE("A Linux reference matches the file name exactly", "[DependencyAttribution][US6][case]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "elfcase";
    dep::WriteFile(folder, "libq.so", dep::ElfNeeding({}));
    if(FoldsCase(folder, "libq.so", "LIBQ.SO"))
    {
        dep::Skip("the file system treats names that differ in letter case as one file");
        return;
    }
    const std::string program =
        dep::WriteFile(folder, "prog", dep::ElfNeeding({"LIBQ.SO", "libQ.so", "libq.so"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.size() == 3);
    REQUIRE(result.dependencies.at(Canon(folder / "libq.so")) == std::vector<std::string>{program});
    // The names in another case are not found: listed under what was recorded.
    REQUIRE(result.dependencies.at("LIBQ.SO") == std::vector<std::string>{program});
    REQUIRE(result.dependencies.at("libQ.so") == std::vector<std::string>{program});
}

TEST_CASE("Linux files differing only in case are two libraries", "[DependencyAttribution][US6][case]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "elftwo";
    dep::WriteFile(folder, "libq.so", dep::ElfNeeding({}));
    dep::WriteFile(folder, "libQ.so", dep::ElfNeeding({}));
    std::size_t count = 0;
    for(const auto &entry : std::filesystem::directory_iterator(folder))
    {
        (void)entry;
        ++count;
    }
    if(count != 2)
    {
        dep::Skip("the file system does not keep file names that differ only in letter case apart");
        return;
    }
    const std::string lower = dep::WriteFile(scratch.Path() / "progs", "lower", dep::ElfNeeding({"libq.so"})).string();
    const std::string upper = dep::WriteFile(scratch.Path() / "progs", "upper", dep::ElfNeeding({"libQ.so"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({lower, upper}, {folder.string()});

    REQUIRE(result.dependencies.size() == 2);
    REQUIRE(result.dependencies.at(Canon(folder / "libq.so")) == std::vector<std::string>{lower});
    REQUIRE(result.dependencies.at(Canon(folder / "libQ.so")) == std::vector<std::string>{upper});
}

TEST_CASE("A library that is named but absent is listed under its recorded name and is not an error",
          "[DependencyAttribution][US6][absent]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "absent";
    dep::WriteFile(folder, "libmid.so", dep::ElfNeeding({"libgone.so", "libgone2.so"}));
    const std::string direct = dep::WriteFile(folder, "direct", dep::ElfNeeding({"libgone.so"})).string();
    const std::string indirect = dep::WriteFile(folder, "indirect", dep::ElfNeeding({"libmid.so"})).string();
    DependencyLister lister;

    for(const auto &inputs : {std::vector<std::string>{direct, indirect}, std::vector<std::string>{indirect, direct}})
    {
        const auto result = lister.ListDependencies(inputs, {folder.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
        REQUIRE(result.dependencies.size() == 3);
        REQUIRE(result.dependencies.at("libgone.so") == Sorted({direct, indirect}));
        REQUIRE(result.dependencies.at("libgone2.so") == std::vector<std::string>{indirect});
        REQUIRE(result.dependencies.at(Canon(folder / "libmid.so")) == std::vector<std::string>{indirect});
    }
}

TEST_CASE("A Windows library that is absent keeps the spelling it was recorded in",
          "[DependencyAttribution][US6][absent]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "peabsent";
    const std::string program = dep::WriteFile(folder, "prog.exe", dep::PeImporting({"Missing.DLL"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.size() == 1);
    REQUIRE(result.dependencies.at("Missing.DLL") == std::vector<std::string>{program});
}

TEST_CASE("A recorded name with a folder part is never read from outside the search folders",
          "[DependencyAttribution][US6][escape]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path search = scratch.Path() / "search";
    const std::filesystem::path outside = scratch.Path() / "outside";
    const std::filesystem::path outside_file = dep::WriteFile(outside, "secret.so", dep::ElfNeeding({"libdeeper.so"}));
    dep::WriteFile(outside, "secret.dll", dep::PeImporting({"deeper.dll"}));
    dep::WriteFile(search, "inner.so", dep::ElfNeeding({}));
    dep::WriteFile(search, "inner.dll", dep::PeImporting({}));
    dep::WriteFile(search / "sub", "nested.so", dep::ElfNeeding({}));
    DependencyLister lister;

    const std::string absolute = outside_file.string();
    const std::vector<std::string> elf_names{"../outside/secret.so", "./inner.so", "sub/nested.so", "..", ".", absolute};
    const std::vector<std::string> pe_names{"..\\outside\\secret.dll", ".\\inner.dll", "../outside/secret.dll",
                                            "C:secret.dll", "C:\\secret.dll", "..", "sub\\inner.dll"};

    SECTION("Linux program")
    {
        const std::string program = dep::WriteFile(search, "prog", dep::ElfNeeding(elf_names)).string();
        dep::ResetReadCount();
        const auto result = lister.ListDependencies({program}, {search.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
        REQUIRE(result.dependencies.size() == elf_names.size());
        for(const std::string &name : elf_names)
        {
            INFO("recorded name " << name);
            REQUIRE(result.dependencies.at(name) == std::vector<std::string>{program});
        }
        // Only the program was read: not the files outside, and not a file named with a folder part.
        REQUIRE(dep::ReadCount() == 1);
    }

    SECTION("Windows program")
    {
        const std::string program = dep::WriteFile(search, "prog.exe", dep::PeImporting(pe_names)).string();
        dep::ResetReadCount();
        const auto result = lister.ListDependencies({program}, {search.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
        REQUIRE(result.dependencies.size() == pe_names.size());
        for(const std::string &name : pe_names)
        {
            INFO("recorded name " << name);
            REQUIRE(result.dependencies.at(name) == std::vector<std::string>{program});
        }
        REQUIRE(dep::ReadCount() == 1);
    }

    SECTION("through a library, credited to the program that reaches it")
    {
        dep::WriteFile(search, "libhop.so", dep::ElfNeeding({"../outside/secret.so"}));
        const std::string program = dep::WriteFile(search, "prog", dep::ElfNeeding({"libhop.so"})).string();
        dep::ResetReadCount();
        const auto result = lister.ListDependencies({program}, {search.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == 2);
        REQUIRE(result.dependencies.at("../outside/secret.so") == std::vector<std::string>{program});
        REQUIRE(result.dependencies.at(Canon(search / "libhop.so")) == std::vector<std::string>{program});
        REQUIRE(dep::ReadCount() == 2);
    }
}

TEST_CASE("Search folders that do not exist or are not folders are ignored", "[DependencyAttribution][US6][edge]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "real";
    dep::WriteFile(folder, "libfound.so", dep::ElfNeeding({}));
    const std::string program = dep::WriteFile(folder, "prog", dep::ElfNeeding({"libfound.so", "libnot.so"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({program}, {(scratch.Path() / "nowhere").string(), "", program,
                                                            (scratch.Path() / "nowhere" / "deeper").string(),
                                                            folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(result.dependencies.size() == 2);
    REQUIRE(result.dependencies.at(Canon(folder / "libfound.so")) == std::vector<std::string>{program});
    REQUIRE(result.dependencies.at("libnot.so") == std::vector<std::string>{program});
}

TEST_CASE("An empty request and a request of only unreadable inputs give empty results with the report",
          "[DependencyAttribution][US6][edge]")
{
    seedtest::ScratchDir scratch;
    DependencyLister lister;

    SECTION("nothing at all")
    {
        const auto result = lister.ListDependencies({}, {});
        REQUIRE(result.dependencies.empty());
        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
    }

    SECTION("no inputs but search folders")
    {
        const auto result = lister.ListDependencies({}, {scratch.Path().string()});
        REQUIRE(result.dependencies.empty());
        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
    }

    SECTION("only inputs that cannot be read")
    {
        const std::string missing = (scratch.Path() / "missing").string();
        const std::string text = dep::WriteFile(scratch.Path(), "notes.txt", dep::Bytes{'h', 'i', ' ', 't', 'h', 'e', 'r', 'e'}).string();
        const std::string folder = (scratch.Path() / "folder").string();
        std::filesystem::create_directories(folder);

        const auto result = lister.ListDependencies({missing, text, folder, missing}, {scratch.Path().string()});

        REQUIRE(result.dependencies.empty());
        REQUIRE(result.libraryErrors.empty());
        REQUIRE(result.errors.size() == 3);
        for(const std::string &input : {missing, text, folder})
        {
            INFO("input " << input);
            REQUIRE(result.errors.count(input) == 1);
            REQUIRE_FALSE(result.errors.at(input).empty());
        }
    }
}

TEST_CASE("A Linux program needing a Windows library, and the reverse, lists across formats",
          "[DependencyAttribution][US6][cross]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "cross";
    // linuxprog -> win.dll -> HELPER.DLL (found as helper.dll) -> libback.so -> libdeep.so
    dep::WriteFile(folder, "win.dll", dep::PeImporting({"HELPER.DLL", "absent.dll"}));
    dep::WriteFile(folder, "helper.dll", dep::PeImporting({"libback.so", "LIBDEEP.SO"}));
    dep::WriteFile(folder, "libback.so", dep::ElfNeeding({"libdeep.so"}));
    dep::WriteFile(folder, "libdeep.so", dep::ElfNeeding({}));
    const std::string linux_program = dep::WriteFile(folder, "linuxprog", dep::ElfNeeding({"win.dll"})).string();
    const std::string windows_program = dep::WriteFile(folder, "winprog.exe", dep::PeImporting({"libback.so"})).string();
    DependencyLister lister;

    SECTION("a Linux program reaching Windows libraries")
    {
        const auto result = lister.ListDependencies({linux_program}, {folder.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.libraryErrors.empty());
        REQUIRE(result.dependencies.at(Canon(folder / "win.dll")) == std::vector<std::string>{linux_program});
        REQUIRE(result.dependencies.at(Canon(folder / "helper.dll")) == std::vector<std::string>{linux_program});
        REQUIRE(result.dependencies.at("absent.dll") == std::vector<std::string>{linux_program});
        REQUIRE(result.dependencies.at(Canon(folder / "libback.so")) == std::vector<std::string>{linux_program});
        REQUIRE(result.dependencies.at(Canon(folder / "libdeep.so")) == std::vector<std::string>{linux_program});
        // The reference "LIBDEEP.SO" sits in a Windows file, so it matches without regard to case.
        REQUIRE(result.dependencies.size() == 5);
    }

    SECTION("a Windows program reaching a Linux library")
    {
        const auto result = lister.ListDependencies({windows_program}, {folder.string()});

        REQUIRE(result.errors.empty());
        REQUIRE(result.dependencies.size() == 2);
        REQUIRE(result.dependencies.at(Canon(folder / "libback.so")) == std::vector<std::string>{windows_program});
        REQUIRE(result.dependencies.at(Canon(folder / "libdeep.so")) == std::vector<std::string>{windows_program});
    }
}

TEST_CASE("An input that is also a library in a Windows chain is credited both ways",
          "[DependencyAttribution][US6][input]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path folder = scratch.Path() / "peinput";
    dep::WriteFile(folder, "base.dll", dep::PeImporting({}));
    const std::string middle = dep::WriteFile(folder, "middle.dll", dep::PeImporting({"BASE.dll"})).string();
    const std::string program = dep::WriteFile(folder, "prog.exe", dep::PeImporting({"Middle.DLL"})).string();
    DependencyLister lister;

    const auto result = lister.ListDependencies({middle, program}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.size() == 2);
    REQUIRE(result.dependencies.at(Canon(folder / "middle.dll")) == std::vector<std::string>{program});
    REQUIRE(result.dependencies.at(Canon(folder / "base.dll")) == Sorted({middle, program}));
}
