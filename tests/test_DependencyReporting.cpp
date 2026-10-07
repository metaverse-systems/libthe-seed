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

// ---------------------------------------------------------------------------
// Windows libraries loaded on first use (delay-load table, data directory 13)
// ---------------------------------------------------------------------------

namespace {

// Where the optional header's count of data directories sits in the files
// that PeImporting builds.
constexpr std::size_t kDirectoryCountField = 0x44 + 20 + 108;

const dep::DelayNameForm kDelayForms[] = {dep::DelayNameForm::Rva, dep::DelayNameForm::VirtualAddress};

const char *FormLabel(dep::DelayNameForm form)
{
    return form == dep::DelayNameForm::Rva ? "name stored as an RVA" : "name stored as a virtual address";
}

// A scratch folder holding one Windows program built from bytes.
struct PeSite
{
    seedtest::ScratchDir scratch;
    fs::path folder;

    PeSite() : folder(this->scratch.Path() / "pe")
    {
        fs::create_directories(this->folder);
        this->folder = fs::canonical(this->folder);
    }

    std::string Program(const dep::Bytes &bytes) { return dep::WriteFile(this->folder, "app.exe", bytes).string(); }
};

// The names the request lists, as a sorted set, for a program that depends on
// libraries that are not found.
std::set<std::string> ListedNames(const dep::Bytes &program)
{
    PeSite site;
    const std::string app = site.Program(program);
    DependencyLister lister;
    const DependencyResult result = lister.ListDependencies({app}, {});
    REQUIRE(result.errors.empty());
    REQUIRE(result.libraryErrors.empty());
    for(const auto &entry : result.dependencies)
    {
        REQUIRE(entry.second == Names{app});
    }
    return Keys(result);
}

using NameSet = std::set<std::string>;
}

TEST_CASE("A program with an import table only is listed as before", "[DependencyReporting][US3]")
{
    CHECK(ListedNames(dep::PeImporting({"ext1.dll", "ext2.dll"})) == NameSet{"ext1.dll", "ext2.dll"});
}

TEST_CASE("A program with a delay-load table only is listed with those libraries", "[DependencyReporting][US3]")
{
    for(const dep::DelayNameForm form : kDelayForms)
    {
        SECTION(FormLabel(form))
        {
            CHECK(ListedNames(dep::PeImporting({}, {"ext1.dll", "ext2.dll"}, form)) ==
                  NameSet{"ext1.dll", "ext2.dll"});
        }
    }
}

TEST_CASE("A program with both tables is listed with the libraries of both", "[DependencyReporting][US3]")
{
    for(const dep::DelayNameForm form : kDelayForms)
    {
        SECTION(FormLabel(form))
        {
            CHECK(ListedNames(dep::PeImporting({"ext1.dll", "ext2.dll"}, {"ext3.dll"}, form)) ==
                  NameSet{"ext1.dll", "ext2.dll", "ext3.dll"});
        }
    }
}

TEST_CASE("A library named in both tables is listed once", "[DependencyReporting][US3]")
{
    for(const dep::DelayNameForm form : kDelayForms)
    {
        SECTION(FormLabel(form))
        {
            const NameSet listed = ListedNames(dep::PeImporting({"ext1.dll", "ext2.dll"}, {"ext2.dll", "ext3.dll"}, form));
            CHECK(listed == NameSet{"ext1.dll", "ext2.dll", "ext3.dll"});
            // Names are compared without regard to case.
            CHECK(ListedNames(dep::PeImporting({"ext1.dll"}, {"EXT1.DLL"}, form)).size() == 1);
        }
    }
}

TEST_CASE("A program with neither table has no dependencies and no errors", "[DependencyReporting][US3]")
{
    CHECK(ListedNames(dep::PeImporting({})).empty());
}

TEST_CASE("A file declaring 13 or fewer data directories has no delay-load table", "[DependencyReporting][US3]")
{
    for(const std::uint32_t count : {13u, 2u})
    {
        SECTION(std::to_string(count) + " directories")
        {
            dep::Bytes program = dep::PeImporting({"ext1.dll"}, {"ext2.dll"});
            program[kDirectoryCountField] = static_cast<std::uint8_t>(count);
            REQUIRE(program[kDirectoryCountField + 1] == 0);
            CHECK(ListedNames(program) == NameSet{"ext1.dll"});
        }
    }
}

TEST_CASE("A delay-loaded library found in the search folder has its own dependencies credited to the program", "[DependencyReporting][US3]")
{
    for(const dep::DelayNameForm form : kDelayForms)
    {
        SECTION(FormLabel(form))
        {
            PeSite site;
            dep::CopyPeChain(site.folder);
            const std::string app = site.Program(dep::PeImporting({}, {"libfoo.dll"}, form));
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({app}, {site.folder.string()});

            REQUIRE(result.errors.empty());
            REQUIRE(result.libraryErrors.empty());
            const NameSet chain{Canon(site.folder / "libfoo.dll"), Canon(site.folder / "libbar.dll"),
                                Canon(site.folder / "libbaz.dll")};
            REQUIRE(Keys(result) == chain);
            for(const std::string &key : chain)
            {
                CHECK(result.dependencies.at(key) == Names{app});
            }
        }
    }
}

TEST_CASE("A delay-loaded library reached from two programs is credited to both", "[DependencyReporting][US3]")
{
    PeSite site;
    dep::CopyPeChain(site.folder);
    const std::string first = dep::WriteFile(site.folder, "first.exe", dep::PeImporting({}, {"libfoo.dll"})).string();
    const std::string second = dep::WriteFile(site.folder, "second.exe", dep::PeImporting({"libbar.dll"}, {"ext1.dll"})).string();
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({first, second}, {site.folder.string()});

    REQUIRE(result.errors.empty());
    CHECK(Slice(result, first) == NameSet{Canon(site.folder / "libfoo.dll"), Canon(site.folder / "libbar.dll"),
                                          Canon(site.folder / "libbaz.dll")});
    CHECK(Slice(result, second) == NameSet{Canon(site.folder / "libbar.dll"), Canon(site.folder / "libbaz.dll"), "ext1.dll"});
}

// ---------------------------------------------------------------------------
// Mac programs are declined, not reported as having no dependencies
// ---------------------------------------------------------------------------

namespace {

const char kMacDeclined[] = "Mach-O files are not supported for dependency listing";

std::string DeclinedFor(const std::string &path)
{
    return path + ": " + kMacDeclined;
}

struct MagicCase
{
    const char *label;
    std::vector<std::uint8_t> magic;
    bool fat;
};

// The six leading words of a Mach-O file, as the bytes appear in the file.
const MagicCase kMagics[] = {
    {"32-bit, big-endian fields", {0xFE, 0xED, 0xFA, 0xCE}, false},
    {"32-bit, little-endian fields", {0xCE, 0xFA, 0xED, 0xFE}, false},
    {"64-bit, big-endian fields", {0xFE, 0xED, 0xFA, 0xCF}, false},
    {"64-bit, little-endian fields", {0xCF, 0xFA, 0xED, 0xFE}, false},
    {"universal, big-endian count", {0xCA, 0xFE, 0xBA, 0xBE}, true},
    {"universal, little-endian count", {0xBE, 0xBA, 0xFE, 0xCA}, true},
};

// A short file that starts with the magic. A universal file gets an
// architecture count of two in the byte order its magic implies; the rest is
// zero. Detection must not depend on anything past those bytes.
dep::Bytes MacBytes(const MagicCase &entry, std::uint32_t count = 2)
{
    dep::Bytes bytes(64, 0);
    std::copy(entry.magic.begin(), entry.magic.end(), bytes.begin());
    if(entry.fat)
    {
        const bool big = entry.magic[0] == 0xCA;
        for(std::size_t index = 0; index < 4; ++index)
        {
            bytes[4 + (big ? 3 - index : index)] = static_cast<std::uint8_t>(count >> (8 * index));
        }
    }
    return bytes;
}

void RequireDeclined(const DependencyResult &result, const std::string &path)
{
    REQUIRE(result.errors.count(path) == 1);
    CHECK(result.errors.at(path) == DeclinedFor(path));
    CHECK(result.dependencies.empty());
    CHECK(result.libraryErrors.empty());
}
} // namespace

TEST_CASE("A Mach-O file of each of the six magics given alone is declined", "[DependencyReporting][US5]")
{
    for(const MagicCase &entry : kMagics)
    {
        SECTION(entry.label)
        {
            Site site;
            const std::string mac = dep::WriteFile(site.folder, "mac.bin", MacBytes(entry)).string();
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({mac}, site.Search());

            REQUIRE(result.errors.size() == 1);
            RequireDeclined(result, mac);
        }
    }
}

TEST_CASE("The committed Mach-O samples are declined", "[DependencyReporting][US5]")
{
    for(const char *sample : {"tiny-macho-x86_64", "tiny-macho-arm64", "tiny-macho-universal"})
    {
        SECTION(sample)
        {
            Site site;
            const std::string mac = dep::CopyFixture(site.folder, sample).string();
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({mac}, site.Search());

            REQUIRE(result.errors.size() == 1);
            RequireDeclined(result, mac);
        }
    }
}

TEST_CASE("A Mach-O file with library references is declined, not listed", "[DependencyReporting][US5]")
{
    Site site;
    const std::string mac = dep::WriteFile(site.folder, "mac.bin",
        dep::MachOReferencing({{dep::kLoadDylib, "/usr/lib/libone.dylib"}, {dep::kLoadWeakDylib, "libtwo.dylib"}})).string();
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({mac}, site.Search());

    REQUIRE(result.errors.size() == 1);
    RequireDeclined(result, mac);
}

TEST_CASE("A Mach-O input among other inputs leaves the others listed fully", "[DependencyReporting][US5]")
{
    for(const MagicCase &entry : kMagics)
    {
        SECTION(entry.label)
        {
            Site site;
            site.Library("libgood.so", {"libdeep.so"});
            site.Library("libdeep.so", {});
            const std::string first = site.Program("first", {"libgood.so", "libabsent.so"});
            const std::string second = site.Program("second", {"libdeep.so"});
            const std::string mac = dep::WriteFile(site.folder, "mac.bin", MacBytes(entry)).string();
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({first, mac, second}, site.Search());

            REQUIRE(result.errors.size() == 1);
            CHECK(result.errors.at(mac) == DeclinedFor(mac));
            CHECK(result.libraryErrors.empty());
            CHECK(Keys(result) == NameSet{site.Key("libgood.so"), site.Key("libdeep.so"), "libabsent.so"});
            CHECK(result.dependencies.at(site.Key("libgood.so")) == Names{first});
            CHECK(result.dependencies.at("libabsent.so") == Names{first});
            CHECK(Slice(result, first) == NameSet{site.Key("libgood.so"), site.Key("libdeep.so"), "libabsent.so"});
            CHECK(Slice(result, second) == NameSet{site.Key("libdeep.so")});
            CHECK(Slice(result, mac).empty());
        }
    }
}

TEST_CASE("A Mach-O file found as a library is listed and reported through libraryErrors", "[DependencyReporting][US5]")
{
    for(const MagicCase &entry : kMagics)
    {
        SECTION(entry.label)
        {
            Site site;
            dep::WriteFile(site.folder, "libmac.so", MacBytes(entry));
            site.Library("libgood.so", {});
            const std::string first = site.Program("first", {"libmac.so", "libgood.so"});
            const std::string second = site.Program("second", {"libmac.so"});
            const std::string key = Canon(site.folder / "libmac.so");
            DependencyLister lister;

            const DependencyResult result = lister.ListDependencies({second, first}, site.Search());

            CHECK(result.errors.empty());
            CHECK(Keys(result) == NameSet{key, Canon(site.folder / "libgood.so")});
            CHECK(result.dependencies.at(key) == Names{first, second});
            CHECK(result.dependencies.at(Canon(site.folder / "libgood.so")) == Names{first});
            REQUIRE(result.libraryErrors.size() == 1);
            REQUIRE(result.libraryErrors.count(key) == 1);
            CHECK(result.libraryErrors.at(key).reason == DeclinedFor(key));
            CHECK(result.libraryErrors.at(key).inputs == Names{first, second});
        }
    }
}

TEST_CASE("A Java class file is still an unsupported format, not a Mac file", "[DependencyReporting][US5]")
{
    Site site;
    // CA FE BA BE, minor 0, major 52: read as an architecture count it is 52,
    // above the 30 a universal file can plausibly hold.
    dep::Bytes java(64, 0);
    java[0] = 0xCA;
    java[1] = 0xFE;
    java[2] = 0xBA;
    java[3] = 0xBE;
    java[7] = 52;
    const std::string klass = dep::WriteFile(site.folder, "Thing.class", java).string();
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({klass}, site.Search());

    REQUIRE(result.errors.count(klass) == 1);
    CHECK(Contains(result.errors.at(klass), "Unsupported binary format"));
    CHECK_FALSE(Contains(result.errors.at(klass), "Mach-O"));
    CHECK(result.dependencies.empty());
    CHECK(result.libraryErrors.empty());
}

TEST_CASE("A universal file is declined up to 30 architectures and not beyond", "[DependencyReporting][US5]")
{
    for(const MagicCase &entry : kMagics)
    {
        if(!entry.fat)
        {
            continue;
        }
        SECTION(entry.label)
        {
            Site site;
            DependencyLister lister;
            for(const std::uint32_t count : {1u, 2u, 30u})
            {
                INFO("count " << count);
                const std::string mac = dep::WriteFile(site.folder, "mac" + std::to_string(count), MacBytes(entry, count)).string();
                const DependencyResult result = lister.ListDependencies({mac}, site.Search());
                REQUIRE(result.errors.count(mac) == 1);
                CHECK(result.errors.at(mac) == DeclinedFor(mac));
            }
            for(const std::uint32_t count : {31u, 52u, 0x1000u})
            {
                INFO("count " << count);
                const std::string other = dep::WriteFile(site.folder, "other" + std::to_string(count), MacBytes(entry, count)).string();
                const DependencyResult result = lister.ListDependencies({other}, site.Search());
                REQUIRE(result.errors.count(other) == 1);
                CHECK(Contains(result.errors.at(other), "Unsupported binary format"));
            }
        }
    }
}

TEST_CASE("A Java class file found as a library is reported as an unsupported format", "[DependencyReporting][US5]")
{
    Site site;
    dep::Bytes java(64, 0);
    java[0] = 0xCA;
    java[1] = 0xFE;
    java[2] = 0xBA;
    java[3] = 0xBE;
    java[7] = 52;
    dep::WriteFile(site.folder, "libjava.so", java);
    const std::string app = site.Program("app", {"libjava.so"});
    const std::string key = Canon(site.folder / "libjava.so");
    DependencyLister lister;

    const DependencyResult result = lister.ListDependencies({app}, site.Search());

    CHECK(result.errors.empty());
    REQUIRE(result.libraryErrors.count(key) == 1);
    CHECK_FALSE(Contains(result.libraryErrors.at(key).reason, "Mach-O"));
    CHECK(Contains(result.libraryErrors.at(key).reason, "format"));
}
