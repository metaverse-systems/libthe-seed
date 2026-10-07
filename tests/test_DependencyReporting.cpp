#include "TestPaths.hpp"
#include "DepFixtures.hpp"

#include <libthe-seed/DependencyLister.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

// A library that is found but cannot be read must stay listed as a dependency
// and be reported in `libraryErrors` with its path, the reason and every named
// input that reaches it. `errors` keeps its meaning: a named input failed.

namespace {
namespace dep = seedtest::dep;
namespace fs = std::filesystem;
using Names = std::vector<std::string>;

std::string Canon(const fs::path &path)
{
    return fs::canonical(path).string();
}

bool StartsWith(const std::string &text, const std::string &prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool Contains(const std::string &text, const std::string &part)
{
    return text.find(part) != std::string::npos;
}

std::set<std::string> Keys(const DependencyResult &result)
{
    std::set<std::string> keys;
    for(const auto &entry : result.dependencies)
    {
        keys.insert(entry.first);
    }
    return keys;
}

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

// A scratch folder that is both the place of the programs and the search
// folder. Names are written under their canonical folder so that keys can be
// predicted.
struct Site
{
    seedtest::ScratchDir scratch;
    fs::path folder;

    Site() : folder(this->scratch.Path() / "site")
    {
        fs::create_directories(this->folder);
        this->folder = fs::canonical(this->folder);
    }

    // A program that needs `needed`, in the folder.
    std::string Program(const std::string &name, const Names &needed)
    {
        return dep::WriteFile(this->folder, name, dep::ElfNeeding(needed)).string();
    }

    std::string Library(const std::string &name, const Names &needed)
    {
        return dep::WriteFile(this->folder, name, dep::ElfNeeding(needed)).string();
    }

    Names Search() const { return {this->folder.string()}; }
    std::string Key(const std::string &name) const { return (this->folder / name).string(); }
};

dep::Bytes TruncatedElf()
{
    dep::Bytes bytes = dep::ElfNeeding({"libnext.so"});
    bytes.resize(40);
    return bytes;
}

dep::Bytes PlainText()
{
    const std::string text = "this is not a library\n";
    return dep::Bytes(text.begin(), text.end());
}

// The four kinds of library that is found by name and cannot be read.
enum class Kind { TruncatedElf, Text, Directory, DanglingLink };

// Creates `libx.so` in the site as the given kind. Returns false (after a
// SKIPPED line) when links cannot be made.
bool MakeBroken(Site &site, Kind kind, const std::string &name)
{
    switch(kind)
    {
    case Kind::TruncatedElf:
        dep::WriteFile(site.folder, name, TruncatedElf());
        return true;
    case Kind::Text:
        dep::WriteFile(site.folder, name, PlainText());
        return true;
    case Kind::Directory:
        fs::create_directories(site.folder / name);
        return true;
    case Kind::DanglingLink:
        return dep::MakeLink("no-such-target", site.folder / name);
    }
    return false;
}

// The key a broken library is listed under: its canonical path when the
// entry resolves, its path in the search folder when the link dangles.
std::string BrokenKey(const Site &site, Kind kind, const std::string &name)
{
    if(kind == Kind::DanglingLink)
    {
        return site.Key(name);
    }
    return Canon(site.folder / name);
}

struct KindCase
{
    const char *label;
    Kind kind;
};

const KindCase kKinds[] = {
    {"truncated ELF library", Kind::TruncatedElf},
    {"text file named like a library", Kind::Text},
    {"directory named like a library", Kind::Directory},
    {"dangling link named like a library", Kind::DanglingLink},
};

// The check of a reason: it starts with the path of the library.
void RequireReasonFor(const LibraryError &error, const std::string &key, Kind kind)
{
    INFO("reason: " << error.reason);
    REQUIRE(StartsWith(error.reason, key));
    REQUIRE(error.reason.size() > key.size());
    if(kind == Kind::TruncatedElf)
    {
        REQUIRE(Contains(error.reason, "ELF"));
    }
    else if(kind == Kind::Text)
    {
        REQUIRE(Contains(error.reason, "format"));
    }
}
} // namespace

TEST_CASE("An unreadable library is listed and reported with its path and reason and input", "[DependencyReporting][US2]")
{
    for(const KindCase &entry : kKinds)
    {
        SECTION(entry.label)
        {
            Site site;
            if(!MakeBroken(site, entry.kind, "libx.so"))
            {
                return;
            }
            const std::string app = site.Program("app", {"libx.so"});
            const std::string key = BrokenKey(site, entry.kind, "libx.so");
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({app}, site.Search());

            REQUIRE(result.errors.empty());
            REQUIRE(result.dependencies.count(key) == 1);
            REQUIRE(result.dependencies.at(key) == Names{app});
            REQUIRE(result.libraryErrors.size() == 1);
            REQUIRE(result.libraryErrors.count(key) == 1);
            const LibraryError &error = result.libraryErrors.at(key);
            REQUIRE(error.inputs == Names{app});
            RequireReasonFor(error, key, entry.kind);
        }
    }
}

TEST_CASE("One unreadable library needed by three programs is reported once naming all three", "[DependencyReporting][US2]")
{
    for(const KindCase &entry : kKinds)
    {
        SECTION(entry.label)
        {
            Site site;
            if(!MakeBroken(site, entry.kind, "libx.so"))
            {
                return;
            }
            // The first reaches it directly, the others through a readable library.
            site.Library("libmid.so", {"libx.so"});
            const std::string app_c = site.Program("appC", {"libx.so"});
            const std::string app_a = site.Program("appA", {"libmid.so"});
            const std::string app_b = site.Program("appB", {"libmid.so", "libx.so"});
            const std::string key = BrokenKey(site, entry.kind, "libx.so");
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({app_c, app_a, app_b}, site.Search());

            REQUIRE(result.errors.empty());
            REQUIRE(result.libraryErrors.size() == 1);
            REQUIRE(result.libraryErrors.count(key) == 1);
            const Names all{app_a, app_b, app_c};
            REQUIRE(result.libraryErrors.at(key).inputs == all);
            REQUIRE(result.dependencies.at(key) == all);
        }
    }
}

TEST_CASE("A library without read permission is reported", "[DependencyReporting][US2][permission]")
{
    if(dep::RunningAsRoot())
    {
        dep::Skip("the permission case needs a non-administrator user (mode 000 does not stop root)");
        return;
    }
    Site site;
    const std::string library = site.Library("libx.so", {});
    const std::string app = site.Program("app", {"libx.so"});
    std::error_code ec;
    fs::permissions(library, fs::perms::none, ec);
    REQUIRE_FALSE(ec);
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({app}, site.Search());

    const std::string key = Canon(library);
    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.at(key) == Names{app});
    REQUIRE(result.libraryErrors.size() == 1);
    REQUIRE(result.libraryErrors.count(key) == 1);
    REQUIRE(result.libraryErrors.at(key).inputs == Names{app});
    INFO("reason: " << result.libraryErrors.at(key).reason);
    REQUIRE(StartsWith(result.libraryErrors.at(key).reason, key));
    REQUIRE(result.libraryErrors.at(key).reason.size() > key.size());
}

TEST_CASE("A request whose libraries are all readable has an empty report", "[DependencyReporting][US2]")
{
    seedtest::ScratchDir scratch;
    const fs::path folder = dep::CopyElfChain(scratch.Path() / "chain");
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies(
        {(folder / "appA").string(), (folder / "appB").string()}, {folder.string()});

    REQUIRE(result.errors.empty());
    REQUIRE(result.dependencies.size() == 3);
    REQUIRE(result.libraryErrors.empty());
}

TEST_CASE("An unreadable library behind another unreadable library is reported once for the first", "[DependencyReporting][US2]")
{
    Site site;
    // libfoo needs libbad, which cannot be parsed; the text that libbad
    // would have named (libworse) is also broken, but nothing reaches it.
    site.Library("libfoo.so", {"libbad.so"});
    dep::WriteFile(site.folder, "libbad.so", TruncatedElf()); // would name libnext.so
    dep::WriteFile(site.folder, "libnext.so", PlainText());
    const std::string app_a = site.Program("appA", {"libfoo.so"});
    const std::string app_b = site.Program("appB", {"libfoo.so"});
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({app_a, app_b}, site.Search());

    const std::string bad = Canon(site.folder / "libbad.so");
    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.size() == 1);
    REQUIRE(result.libraryErrors.count(bad) == 1);
    REQUIRE(result.libraryErrors.at(bad).inputs == Names{app_a, app_b});
    REQUIRE(result.dependencies.count(bad) == 1);
    REQUIRE(result.dependencies.count(Canon(site.folder / "libnext.so")) == 0);
}

TEST_CASE("The report keys are keys of the dependencies", "[DependencyReporting][US2][invariants]")
{
    for(const KindCase &entry : kKinds)
    {
        SECTION(entry.label)
        {
            Site site;
            if(!MakeBroken(site, entry.kind, "libx.so"))
            {
                return;
            }
            site.Library("libgood.so", {});
            const std::string app_a = site.Program("appA", {"libx.so", "libgood.so"});
            const std::string app_b = site.Program("appB", {"libgood.so"});
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({app_a, app_b}, site.Search());

            REQUIRE_FALSE(result.libraryErrors.empty());
            for(const auto &error : result.libraryErrors)
            {
                INFO("report key " << error.first);
                REQUIRE(result.dependencies.count(error.first) == 1);
            }
        }
    }
}

TEST_CASE("An unreadable library leaves the other libraries and programs unaffected", "[DependencyReporting][US2][invariants]")
{
    for(const KindCase &entry : kKinds)
    {
        SECTION(entry.label)
        {
            Site site;
            if(!MakeBroken(site, entry.kind, "libx.so"))
            {
                return;
            }
            site.Library("libgood.so", {"libdeep.so"});
            site.Library("libdeep.so", {});
            const std::string app_a = site.Program("appA", {"libx.so", "libgood.so"});
            const std::string app_b = site.Program("appB", {"libgood.so"});
            const std::string key = BrokenKey(site, entry.kind, "libx.so");
            const std::string good = Canon(site.folder / "libgood.so");
            const std::string deep = Canon(site.folder / "libdeep.so");
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({app_a, app_b}, site.Search());

            const Names both{app_a, app_b};
            REQUIRE(result.dependencies.at(good) == both);
            REQUIRE(result.dependencies.at(deep) == both);
            REQUIRE(result.dependencies.at(key) == Names{app_a});
            REQUIRE(Slice(result, app_b) == std::set<std::string>{good, deep});
            REQUIRE(Slice(result, app_a) == std::set<std::string>{good, deep, key});
            // Only the program that reaches the broken library is named.
            REQUIRE(result.libraryErrors.size() == 1);
            REQUIRE(result.libraryErrors.at(key).inputs == Names{app_a});
            REQUIRE(result.errors.empty());
        }
    }
}

TEST_CASE("A named input that cannot be read goes to errors and not to libraryErrors", "[DependencyReporting][US2][invariants]")
{
    Site site;
    const std::string broken = dep::WriteFile(site.folder, "broken", TruncatedElf()).string();
    const std::string text = dep::WriteFile(site.folder, "notes.txt", PlainText()).string();
    const std::string missing = (site.folder / "missing").string();
    const std::string app = site.Program("app", {});
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({broken, text, missing, app}, site.Search());

    REQUIRE(result.errors.size() == 3);
    REQUIRE(result.errors.count(broken) == 1);
    REQUIRE(result.errors.count(text) == 1);
    REQUIRE(result.errors.count(missing) == 1);
    REQUIRE(result.libraryErrors.empty());
    REQUIRE(Slice(result, broken).empty());
}

TEST_CASE("A program that ignores libraryErrors sees the same keys and errors as before", "[DependencyReporting][US2][invariants]")
{
    for(const KindCase &entry : kKinds)
    {
        SECTION(entry.label)
        {
            // Two folders: one with the library broken, one with a readable
            // stand-in of the same name that needs nothing.
            Site broken;
            Site healthy;
            if(!MakeBroken(broken, entry.kind, "libx.so"))
            {
                return;
            }
            healthy.Library("libx.so", {});
            for(Site *site : {&broken, &healthy})
            {
                site->Library("libgood.so", {});
            }
            const Names needs{"libx.so", "libgood.so", "libabsent.so"};
            const std::string app_broken = broken.Program("app", needs);
            const std::string app_healthy = healthy.Program("app", needs);
            DependencyLister lister;

            const DependencyResult with_broken = lister.ListDependencies({app_broken}, broken.Search());
            const DependencyResult with_healthy = lister.ListDependencies({app_healthy}, healthy.Search());

            // Same kinds of keys: the broken library, the good one and the absent name.
            REQUIRE(with_broken.dependencies.size() == with_healthy.dependencies.size());
            REQUIRE(with_broken.dependencies.count("libabsent.so") == 1);
            REQUIRE(with_broken.dependencies.count(BrokenKey(broken, entry.kind, "libx.so")) == 1);
            REQUIRE(with_broken.dependencies.count(Canon(broken.folder / "libgood.so")) == 1);
            REQUIRE(with_broken.errors == with_healthy.errors);
            REQUIRE(with_broken.errors.empty());
            // Only the report differs.
            REQUIRE(with_broken.libraryErrors.size() == 1);
            REQUIRE(with_healthy.libraryErrors.empty());
        }
    }
}
