#include "LoaderTestSupport.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

// Configured locations decide which file a loader opens: they are searched first, in the order they
// were added, the working directory is searched only when it was added, and the first location that
// holds the file decides. The cases run one after another and each works in its own scratch
// directory, so the plugin files they copy never collide.

namespace
{
    namespace fs = std::filesystem;
    using seedtest::CopyModule;
    using seedtest::ScratchDir;
    using seedtest::WorkingDirectoryGuard;

    // The variant of the test module a component came from: 1 for libtestmodule, 2 for libtestmodulealt.
    int VariantOf(ComponentLoader &loader, const std::string &name = "testmodule")
    {
        std::unique_ptr<ecs::Component> component = loader.Create(name);
        REQUIRE(component);
        return component->Export()["variant"].get<int>();
    }

    template <typename Function>
    LoadError LoadErrorOf(Function function)
    {
        try
        {
            function();
        }
        catch(const LoadError &error)
        {
            return error;
        }
        FAIL("Expected a LoadError but nothing was thrown");
        throw 0;
    }

    bool Mentions(const LoadError &error, const std::string &text)
    {
        return std::string(error.what()).find(text) != std::string::npos;
    }

    const LoadError::Location *LocationFor(const LoadError &error, const std::string &path)
    {
        for(const auto &location : error.LocationsGet())
        {
            if(location.path == path)
            {
                return &location;
            }
        }
        return nullptr;
    }

#ifndef _WIN32
    // The file the code at `address` was loaded from.
    fs::path FileOf(void *address)
    {
        Dl_info info{};
        REQUIRE(dladdr(address, &info) != 0);
        REQUIRE(info.dli_fname != nullptr);
        return fs::path(info.dli_fname);
    }
#endif

    std::vector<std::uint8_t> Bytes(std::uint8_t value)
    {
        return {value, value, value};
    }
}

TEST_CASE("A configured location beats the working directory for components", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    CopyModule(scratch, "testmodule", "conf", "testmodule");
    CopyModule(scratch, "testmodulealt", "cwd", "testmodule");
    WorkingDirectoryGuard guard(scratch.Path() / "cwd");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "conf").string());

    for(int attempt = 0; attempt < 3; ++attempt)
    {
        REQUIRE(VariantOf(loader) == 1);
    }

    ComponentLoader again;
    again.PathAdd((scratch.Path() / "conf").string());
    REQUIRE(VariantOf(again) == 1);
}

#ifndef _WIN32
TEST_CASE("A configured location beats the working directory for systems", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::path configured = CopyModule(scratch, "testsystem", "conf", "testsystem");
    CopyModule(scratch, "testsystem", "cwd", "testsystem");
    WorkingDirectoryGuard guard(scratch.Path() / "cwd");

    SystemLoader loader;
    loader.PathAdd((scratch.Path() / "conf").string());

    SystemLoader::SystemCreator creator = loader.Get("testsystem");
    REQUIRE(creator != nullptr);
    REQUIRE(fs::equivalent(FileOf(reinterpret_cast<void *>(creator)), configured));
}
#endif

TEST_CASE("A plugin only in the working directory is not found", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    CopyModule(scratch, "testmodule", "cwd", "testmodule");
    CopyModule(scratch, "testsystem", "cwd", "testsystem");
    fs::create_directories(scratch.Path() / "conf");
    WorkingDirectoryGuard guard(scratch.Path() / "cwd");
    const std::string conf = (scratch.Path() / "conf").string();

    SECTION("component")
    {
        ComponentLoader loader;
        loader.PathAdd(conf);
        LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(Mentions(error, conf));
        REQUIRE(LocationFor(error, conf) != nullptr);
        REQUIRE(error.LocationsGet().size() == 1);
    }

    SECTION("system")
    {
        SystemLoader loader;
        loader.PathAdd(conf);
        LoadError error = LoadErrorOf([&] { loader.Get("testsystem"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(Mentions(error, conf));
        REQUIRE(error.LocationsGet().size() == 1);
    }

    SECTION("no configured locations at all")
    {
        ComponentLoader loader;
        LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(error.LocationsGet().empty());
    }
}

TEST_CASE("The first of several configured locations holding the file wins", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "empty");
    CopyModule(scratch, "testmodulealt", "second", "testmodule");
    CopyModule(scratch, "testmodule", "third", "testmodule");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "empty").string());
    loader.PathAdd((scratch.Path() / "second").string());
    loader.PathAdd((scratch.Path() / "third").string());
    REQUIRE(VariantOf(loader) == 2);

    ComponentLoader reversed;
    reversed.PathAdd((scratch.Path() / "third").string());
    reversed.PathAdd((scratch.Path() / "second").string());
    REQUIRE(VariantOf(reversed) == 1);
}

TEST_CASE("A damaged file in the first location is reported and the second copy is not used", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    seedtest::WriteDamagedModule(scratch.Path() / "first", "testmodule");
    CopyModule(scratch, "testmodule", "second", "testmodule");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "first").string());
    loader.PathAdd((scratch.Path() / "second").string());

    std::unique_ptr<ecs::Component> component;
    LoadError error = LoadErrorOf([&] { component = loader.Create("testmodule"); });
    REQUIRE(error.ReasonGet() == LoadError::Reason::NotLoadable);
    REQUIRE(!component);

    // Asking again gives the same answer: the good copy is never reached.
    LoadError second = LoadErrorOf([&] { component = loader.Create("testmodule"); });
    REQUIRE(second.ReasonGet() == LoadError::Reason::NotLoadable);
    REQUIRE(!component);
}

TEST_CASE("A relative configured location is resolved against the working directory when loading", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    CopyModule(scratch, "testmodule", "tree/conf", "testmodule");
    CopyModule(scratch, "testmodulealt", "other/conf", "testmodule");

    ComponentLoader loader;
    loader.PathAdd("conf");
    REQUIRE(loader.PathsGet() == std::vector<std::string>{"conf"});

    {
        WorkingDirectoryGuard guard(scratch.Path() / "tree");
        REQUIRE(VariantOf(loader) == 1);
    }

    ComponentLoader elsewhere;
    elsewhere.PathAdd("conf");
    {
        WorkingDirectoryGuard guard(scratch.Path() / "other");
        REQUIRE(VariantOf(elsewhere) == 2);
    }
}

TEST_CASE("A location added after a load affects the next request", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "empty");
    CopyModule(scratch, "testmodule", "late", "testmodule");
    CopyModule(scratch, "testsystem", "late", "testsystem");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "empty").string());
    LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
    REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);

    loader.PathAdd((scratch.Path() / "late").string());
    REQUIRE(VariantOf(loader) == 1);

    SystemLoader systems;
    systems.PathAdd((scratch.Path() / "empty").string());
    REQUIRE_THROWS_AS(systems.Get("testsystem"), LoadError);
    systems.PathAdd((scratch.Path() / "late").string());
    REQUIRE(systems.Get("testsystem") != nullptr);
}

TEST_CASE("A configured location that does not exist is skipped and listed as not existing", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    const std::string missing = (scratch.Path() / "missing").string();
    const std::string present = (scratch.Path() / "present").string();
    fs::create_directories(present);

    SECTION("when the file is nowhere")
    {
        ComponentLoader loader;
        loader.PathAdd(missing);
        loader.PathAdd(present);
        LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(error.LocationsGet().size() == 2);

        const LoadError::Location *missingLocation = LocationFor(error, missing);
        const LoadError::Location *presentLocation = LocationFor(error, present);
        REQUIRE(missingLocation != nullptr);
        REQUIRE(presentLocation != nullptr);
        REQUIRE(missingLocation->state == LoadError::LocationState::Missing);
        REQUIRE(presentLocation->state == LoadError::LocationState::Searched);
        REQUIRE(!missingLocation->development);
        REQUIRE(Mentions(error, "does not exist: " + missing));
        REQUIRE(Mentions(error, "searched: " + present));
    }

    SECTION("when a later location holds the file")
    {
        CopyModule(scratch, "testmodule", "present", "testmodule");
        ComponentLoader loader;
        loader.PathAdd(missing);
        loader.PathAdd(present);
        REQUIRE(VariantOf(loader) == 1);
    }
}

TEST_CASE("Two loaders with different locations each load their own file", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    CopyModule(scratch, "testmodule", "one", "testmodule");
    CopyModule(scratch, "testmodulealt", "two", "testmodule");

    ComponentLoader first;
    ComponentLoader second;
    first.PathAdd((scratch.Path() / "one").string());
    second.PathAdd((scratch.Path() / "two").string());

    REQUIRE(VariantOf(first) == 1);
    REQUIRE(VariantOf(second) == 2);
    // Both again, in the other order, after both have loaded.
    REQUIRE(VariantOf(second) == 2);
    REQUIRE(VariantOf(first) == 1);
}

TEST_CASE("A pak in a configured location beats one in the working directory", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    seedtest::WritePak(scratch.Path() / "conf", "searchpak", {{"item", Bytes(1)}});
    seedtest::WritePak(scratch.Path() / "cwd", "searchpak", {{"item", Bytes(2)}});
    WorkingDirectoryGuard guard(scratch.Path() / "cwd");

    PakLoader loader;
    loader.PathAdd((scratch.Path() / "conf").string());

    for(int attempt = 0; attempt < 2; ++attempt)
    {
        auto resources = loader.Load("searchpak");
        REQUIRE(resources.size() == 1);
        REQUIRE(resources.at("item")->Data == Bytes(1));
    }
}

TEST_CASE("A pak only in the working directory is not found", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    seedtest::WritePak(scratch.Path() / "cwd", "searchpak", {{"item", Bytes(2)}});
    fs::create_directories(scratch.Path() / "conf");
    WorkingDirectoryGuard guard(scratch.Path() / "cwd");
    const std::string conf = (scratch.Path() / "conf").string();

    PakLoader loader;
    loader.PathAdd(conf);

    LoadError error = LoadErrorOf([&] { loader.Load("searchpak"); });
    REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
    REQUIRE(Mentions(error, conf));
    REQUIRE(Mentions(error, "searchpak.pak"));

    LoadError filtered = LoadErrorOf([&] { loader.Load("searchpak", {"item"}); });
    REQUIRE(filtered.ReasonGet() == LoadError::Reason::NotFound);
}

TEST_CASE("The first of several pak locations holding the file wins", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "empty");
    seedtest::WritePak(scratch.Path() / "second", "searchpak", {{"item", Bytes(3)}});
    seedtest::WritePak(scratch.Path() / "third", "searchpak", {{"item", Bytes(4)}});

    PakLoader loader;
    loader.PathAdd((scratch.Path() / "empty").string());
    loader.PathAdd((scratch.Path() / "second").string());
    loader.PathAdd((scratch.Path() / "third").string());

    auto resources = loader.Load("searchpak");
    REQUIRE(resources.at("item")->Data == Bytes(3));
}

// Development locations are the relative directories of a the-seed development tree. They are
// searched only when a loader asks for them, and then only after every configured location. The
// scratch tree used below has the working directory "<scratch>/tree/project", so
// "../../<library>/src/.libs" is the directory "<scratch>/<library>/src/.libs" and
// "../node_modules/<org>/<library>/src/.libs" is under "<scratch>/tree/node_modules".

namespace
{
    const std::string developmentComponent = "../../testmodule/src/.libs";
    const std::string developmentSystem = "../../testsystem/src/.libs";
    const std::string developmentOrganisation = "../node_modules/org/testmodule/src/.libs";
    const std::string developmentPak = "../../searchpak";
    const std::string developmentPakOrganisation = "../node_modules/org/searchpak";

    // The working directory of the development tree used by these cases.
    fs::path ProjectOf(const ScratchDir &scratch)
    {
        fs::path project = scratch.Path() / "tree" / "project";
        fs::create_directories(project);
        return project;
    }

    // The directory the relative development location resolves to from the project directory.
    fs::path DevelopmentDirOf(const ScratchDir &scratch, const std::string &relative)
    {
        return (ProjectOf(scratch) / relative).lexically_normal();
    }
}

TEST_CASE("A plugin only in a development location loads with the opt-in and not without", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::path project = ProjectOf(scratch);
    fs::path directory = DevelopmentDirOf(scratch, developmentComponent);
    CopyModule(scratch, "testmodulealt", directory, "testmodule");
    fs::create_directories(scratch.Path() / "conf");
    WorkingDirectoryGuard guard(project);
    const std::string conf = (scratch.Path() / "conf").string();

    SECTION("component without the opt-in")
    {
        ComponentLoader loader;
        loader.PathAdd(conf);
        REQUIRE(!loader.DevelopmentPathsEnabled());
        LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
    }

    SECTION("component with the opt-in")
    {
        ComponentLoader loader;
        loader.PathAdd(conf);
        loader.DevelopmentPathsEnable();
        REQUIRE(loader.DevelopmentPathsEnabled());
        REQUIRE(VariantOf(loader) == 2);
    }

    SECTION("component with no configured location at all")
    {
        ComponentLoader loader;
        loader.DevelopmentPathsEnable();
        REQUIRE(VariantOf(loader) == 2);
    }

    SECTION("the opt-in can be turned off again")
    {
        ComponentLoader loader;
        loader.DevelopmentPathsEnable();
        loader.DevelopmentPathsEnable(false);
        REQUIRE(!loader.DevelopmentPathsEnabled());
        REQUIRE_THROWS_AS(loader.Create("testmodule"), LoadError);
    }

    SECTION("organisation names use the dependency directory")
    {
        fs::path organisation = DevelopmentDirOf(scratch, developmentOrganisation);
        CopyModule(scratch, "testmodule", organisation, "testmodule");
        fs::remove_all(directory);

        ComponentLoader loader;
        loader.DevelopmentPathsEnable();
        REQUIRE(VariantOf(loader, "org/testmodule") == 1);
        REQUIRE_THROWS_AS(loader.Create("testmodule"), LoadError);
    }
}

#ifndef _WIN32
TEST_CASE("A system only in a development location loads with the opt-in and not without", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::path project = ProjectOf(scratch);
    fs::path file = CopyModule(scratch, "testsystem", DevelopmentDirOf(scratch, developmentSystem), "testsystem");
    WorkingDirectoryGuard guard(project);

    SystemLoader without;
    REQUIRE(!without.DevelopmentPathsEnabled());
    REQUIRE_THROWS_AS(without.Get("testsystem"), LoadError);

    SystemLoader with;
    with.DevelopmentPathsEnable();
    REQUIRE(with.DevelopmentPathsEnabled());
    SystemLoader::SystemCreator creator = with.Get("testsystem");
    REQUIRE(creator != nullptr);
    REQUIRE(fs::equivalent(FileOf(reinterpret_cast<void *>(creator)), file));
}
#endif

TEST_CASE("With the opt-in a configured location beats a development location", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::path project = ProjectOf(scratch);
    CopyModule(scratch, "testmodulealt", DevelopmentDirOf(scratch, developmentComponent), "testmodule");
    CopyModule(scratch, "testmodule", "conf", "testmodule");
    WorkingDirectoryGuard guard(project);

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "conf").string());
    loader.DevelopmentPathsEnable();
    for(int attempt = 0; attempt < 3; ++attempt)
    {
        REQUIRE(VariantOf(loader) == 1);
    }

#ifndef _WIN32
    fs::path system = CopyModule(scratch, "testsystem", DevelopmentDirOf(scratch, developmentSystem), "testsystem");
    CopyModule(scratch, "testsystem", "conf", "testsystem");
    SystemLoader systems;
    systems.PathAdd((scratch.Path() / "conf").string());
    systems.DevelopmentPathsEnable();
    SystemLoader::SystemCreator creator = systems.Get("testsystem");
    REQUIRE(creator != nullptr);
    REQUIRE(!fs::equivalent(FileOf(reinterpret_cast<void *>(creator)), system));
#endif
}

TEST_CASE("Without the opt-in no development location appears in the error", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "conf");
    WorkingDirectoryGuard guard(ProjectOf(scratch));
    const std::string conf = (scratch.Path() / "conf").string();

    ComponentLoader loader;
    loader.PathAdd(conf);
    LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
    REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
    REQUIRE(error.LocationsGet().size() == 1);
    for(const auto &location : error.LocationsGet())
    {
        REQUIRE(!location.development);
    }
    REQUIRE(Mentions(error, conf));
    REQUIRE(!Mentions(error, developmentComponent));
    REQUIRE(!Mentions(error, "development location,"));
    REQUIRE(Mentions(error, "development locations are off"));

    ComponentLoader bare;
    LoadError none = LoadErrorOf([&] { bare.Create("testmodule"); });
    REQUIRE(none.LocationsGet().empty());
    REQUIRE(Mentions(none, "development locations are off"));
}

TEST_CASE("With the opt-in the error lists configured then development locations in search order", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "one");
    WorkingDirectoryGuard guard(ProjectOf(scratch));
    const std::string one = (scratch.Path() / "one").string();
    const std::string two = (scratch.Path() / "two").string();

    SECTION("plain name")
    {
        ComponentLoader loader;
        loader.PathAdd(one);
        loader.PathAdd(two);
        loader.DevelopmentPathsEnable();
        LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);

        const auto &locations = error.LocationsGet();
        REQUIRE(locations.size() == 3);
        REQUIRE(locations[0].path == one);
        REQUIRE(!locations[0].development);
        REQUIRE(locations[0].state == LoadError::LocationState::Searched);
        REQUIRE(locations[1].path == two);
        REQUIRE(!locations[1].development);
        REQUIRE(locations[1].state == LoadError::LocationState::Missing);
        REQUIRE(locations[2].path == developmentComponent);
        REQUIRE(locations[2].development);
        REQUIRE(locations[2].state == LoadError::LocationState::Missing);

        const std::string text = error.what();
        REQUIRE(text.find(one) < text.find(two));
        REQUIRE(text.find(two) < text.find(developmentComponent));
        REQUIRE(Mentions(error, "development location, "));
        REQUIRE(!Mentions(error, "development locations are off"));
    }

    SECTION("organisation name")
    {
        ComponentLoader loader;
        loader.PathAdd(one);
        loader.DevelopmentPathsEnable();
        LoadError error = LoadErrorOf([&] { loader.Create("org/testmodule"); });
        const auto &locations = error.LocationsGet();
        REQUIRE(locations.size() == 3);
        REQUIRE(locations[0].path == one);
        REQUIRE(locations[1].path == "../../testmodule/src/.libs");
        REQUIRE(locations[2].path == developmentOrganisation);
        REQUIRE(locations[1].development);
        REQUIRE(locations[2].development);
    }
}

TEST_CASE("SearchPathsGet returns the lookup order, validates the name and does not touch the file system", "[PluginSearchOrder]")
{
    // None of these locations exists, and the working directory is not a development tree.
    ComponentLoader loader;
    loader.PathAdd("/does/not/exist/one");
    loader.PathAdd("relative/two");

    SECTION("without the opt-in only configured locations")
    {
        REQUIRE(loader.SearchPathsGet("testmodule") ==
                std::vector<std::string>{"/does/not/exist/one", "relative/two"});
        REQUIRE(loader.SearchPathsGet("org/testmodule") == loader.SearchPathsGet("testmodule"));
    }

    SECTION("with the opt-in configured first, then development")
    {
        loader.DevelopmentPathsEnable();
        REQUIRE(loader.SearchPathsGet("testmodule") ==
                std::vector<std::string>{"/does/not/exist/one", "relative/two", developmentComponent});
        REQUIRE(loader.SearchPathsGet("org/testmodule") ==
                std::vector<std::string>{"/does/not/exist/one", "relative/two", developmentComponent,
                                         developmentOrganisation});
        REQUIRE(loader.SearchPathsGet("testmodule").back() != ".");
    }

    SECTION("an invalid name is an InvalidName error")
    {
        for(const char *bad : {"", "..", "a/../b", "a\\b", "c:x", "a/b/c", "/a"})
        {
            LoadError error = LoadErrorOf([&] { loader.SearchPathsGet(bad); });
            REQUIRE(error.ReasonGet() == LoadError::Reason::InvalidName);
        }
    }

    SECTION("with no locations at all")
    {
        ComponentLoader bare;
        REQUIRE(bare.SearchPathsGet("testmodule").empty());
        bare.DevelopmentPathsEnable();
        REQUIRE(bare.SearchPathsGet("testmodule") == std::vector<std::string>{developmentComponent});
    }

    SECTION("the same for systems")
    {
        SystemLoader systems;
        systems.PathAdd("/does/not/exist/one");
        REQUIRE(systems.SearchPathsGet("testsystem") == std::vector<std::string>{"/does/not/exist/one"});
        systems.DevelopmentPathsEnable();
        REQUIRE(!systems.SearchPathsGet("testsystem").empty());
        REQUIRE(systems.SearchPathsGet("testsystem").back() == developmentSystem);
        REQUIRE(systems.SearchPathsGet("org/testsystem").back() ==
                "../node_modules/org/testsystem/src/.libs");
        REQUIRE_THROWS_AS(systems.SearchPathsGet("a\\b"), LoadError);
    }
}

TEST_CASE("A pak only in a development location loads with the opt-in and not without", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::path project = ProjectOf(scratch);
    seedtest::WritePak(DevelopmentDirOf(scratch, developmentPak), "searchpak", {{"item", Bytes(5)}});
    seedtest::WritePak(scratch.Path() / "conf", "otherpak", {{"item", Bytes(6)}});
    WorkingDirectoryGuard guard(project);
    const std::string conf = (scratch.Path() / "conf").string();

    SECTION("without the opt-in")
    {
        PakLoader loader;
        loader.PathAdd(conf);
        REQUIRE(!loader.DevelopmentPathsEnabled());
        LoadError error = LoadErrorOf([&] { loader.Load("searchpak"); });
        REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(error.LocationsGet().size() == 1);
        REQUIRE(!Mentions(error, developmentPak));
        REQUIRE(Mentions(error, "development locations are off"));
    }

    SECTION("with the opt-in")
    {
        PakLoader loader;
        loader.PathAdd(conf);
        loader.DevelopmentPathsEnable();
        REQUIRE(loader.DevelopmentPathsEnabled());
        auto resources = loader.Load("searchpak");
        REQUIRE(resources.at("item")->Data == Bytes(5));
    }

    SECTION("organisation names use the dependency directory")
    {
        seedtest::WritePak(DevelopmentDirOf(scratch, developmentPakOrganisation), "searchpak", {{"item", Bytes(7)}});
        fs::remove_all(DevelopmentDirOf(scratch, developmentPak));

        PakLoader loader;
        loader.DevelopmentPathsEnable();
        REQUIRE(loader.Load("org/searchpak").at("item")->Data == Bytes(7));
        REQUIRE_THROWS_AS(loader.Load("searchpak"), LoadError);
    }
}

TEST_CASE("With the opt-in a configured pak beats a development pak", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::path project = ProjectOf(scratch);
    seedtest::WritePak(DevelopmentDirOf(scratch, developmentPak), "searchpak", {{"item", Bytes(5)}});
    seedtest::WritePak(scratch.Path() / "conf", "searchpak", {{"item", Bytes(8)}});
    WorkingDirectoryGuard guard(project);

    PakLoader loader;
    loader.PathAdd((scratch.Path() / "conf").string());
    loader.DevelopmentPathsEnable();
    REQUIRE(loader.Load("searchpak").at("item")->Data == Bytes(8));
}

TEST_CASE("Pak search paths and error list development locations after configured ones", "[PluginSearchOrder]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "conf");
    WorkingDirectoryGuard guard(ProjectOf(scratch));
    const std::string conf = (scratch.Path() / "conf").string();

    PakLoader loader;
    loader.PathAdd(conf);
    REQUIRE(loader.SearchPathsGet("searchpak") == std::vector<std::string>{conf});

    loader.DevelopmentPathsEnable();
    REQUIRE(loader.SearchPathsGet("searchpak") == std::vector<std::string>{conf, developmentPak});
    REQUIRE(loader.SearchPathsGet("org/searchpak") ==
            std::vector<std::string>{conf, developmentPak, developmentPakOrganisation});
    REQUIRE_THROWS_AS(loader.SearchPathsGet("../searchpak"), LoadError);

    LoadError error = LoadErrorOf([&] { loader.Load("org/searchpak"); });
    REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
    const auto &locations = error.LocationsGet();
    REQUIRE(locations.size() == 3);
    REQUIRE(locations[0].path == conf);
    REQUIRE(!locations[0].development);
    REQUIRE(locations[1].path == developmentPak);
    REQUIRE(locations[1].development);
    REQUIRE(locations[2].path == developmentPakOrganisation);
    REQUIRE(locations[2].development);
    REQUIRE(!Mentions(error, "development locations are off"));
}
