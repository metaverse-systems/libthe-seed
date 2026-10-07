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
