#include "LoaderTestSupport.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LibraryLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// A plugin that cannot be loaded says what was being loaded, where, and why: not found lists every
// location with its state, a damaged file names the file and the platform text, a file without the
// entry point names the symbol, and an entry point that produces nothing is an error rather than an
// empty object. Every failure is a LoadError and also a std::runtime_error.

namespace
{
    namespace fs = std::filesystem;
    using seedtest::CopyModule;
    using seedtest::PluginFileName;
    using seedtest::ScratchDir;
    using seedtest::WriteDamagedModule;

    static_assert(std::is_base_of_v<std::runtime_error, LoadError>);

    // Runs `function`, which must throw a LoadError, and returns it. The error is also caught as a
    // std::runtime_error, as callers of earlier versions did.
    template <typename Function>
    LoadError LoadErrorOf(Function function)
    {
        try
        {
            try
            {
                function();
            }
            catch(const std::runtime_error &error)
            {
                const LoadError *load = dynamic_cast<const LoadError *>(&error);
                if(load == nullptr)
                {
                    FAIL("Expected a LoadError but got a plain runtime_error: " << error.what());
                }
                return *load;
            }
        }
        catch(const std::exception &error)
        {
            FAIL("Expected a LoadError but got another exception: " << error.what());
        }
        FAIL("Expected a LoadError but nothing was thrown");
        throw 0;
    }

    bool Mentions(const LoadError &error, const std::string &text)
    {
        return std::string(error.what()).find(text) != std::string::npos;
    }
}

TEST_CASE("Not found lists every location with its state and the file name", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path empty = scratch.Path() / "empty";
    const fs::path missing = scratch.Path() / "missing";
    fs::create_directories(empty);

    ComponentLoader loader;
    loader.PathAdd(empty.string());
    loader.PathAdd(missing.string());

    LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotFound);
    CHECK(error.NameGet() == "testmodule");
    CHECK(Mentions(error, "testmodule"));
    CHECK(Mentions(error, PluginFileName("testmodule")));
    CHECK(Mentions(error, empty.string()));
    CHECK(Mentions(error, missing.string()));
    CHECK(Mentions(error, "does not exist: " + missing.string()));
    CHECK(Mentions(error, "searched: " + empty.string()));
    // The two configured locations, then the development location.
    REQUIRE(error.LocationsGet().size() == 3);
    CHECK(error.LocationsGet()[0].state == LoadError::LocationState::Searched);
    CHECK(error.LocationsGet()[1].state == LoadError::LocationState::Missing);
    CHECK(error.LocationsGet()[2].development);
}

TEST_CASE("Not found with no configured locations lists only the development locations", "[PluginLoadErrors]")
{
    ComponentLoader components;
    LoadError error = LoadErrorOf([&] { components.Create("testmodule"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotFound);
    CHECK(Mentions(error, "no location holds"));
    CHECK(Mentions(error, "testmodule"));
    REQUIRE(error.LocationsGet().size() == 1);
    CHECK(error.LocationsGet()[0].development);
    CHECK(error.LocationsGet()[0].path == "../../testmodule/src/.libs");

    SystemLoader systems;
    error = LoadErrorOf([&] { systems.Create("testsystem"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotFound);
    CHECK(Mentions(error, "no location holds"));
    CHECK(Mentions(error, "testsystem"));
    REQUIRE(error.LocationsGet().size() == 1);
    CHECK(error.LocationsGet()[0].development);
}

TEST_CASE("A damaged file names the absolute file and the platform text", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path damaged = WriteDamagedModule(scratch.Path() / "bad", "testmodule");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "bad").string());

    LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotLoadable);
    CHECK(error.NameGet() == "testmodule");
    CHECK(fs::path(error.FileGet()).is_absolute());
    CHECK(fs::path(error.FileGet()) == damaged);
    CHECK(Mentions(error, damaged.string()));
    CHECK(Mentions(error, "present but could not be loaded"));
    CHECK_FALSE(error.DetailGet().empty());
    CHECK(Mentions(error, error.DetailGet()));
}

TEST_CASE("A damaged file in a system loader and a library loader is not loadable", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path directory = scratch.Path() / "bad";
    WriteDamagedModule(directory, "testsystem");

    SystemLoader systems;
    systems.PathAdd(directory.string());
    LoadError error = LoadErrorOf([&] { systems.Create("testsystem"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotLoadable);
    CHECK(Mentions(error, "present but could not be loaded"));

    LibraryLoader library("testsystem");
    library.PathAdd(directory.string());
    error = LoadErrorOf([&] { library.FunctionGet("create_system"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotLoadable);
    CHECK(Mentions(error, "present but could not be loaded"));
}

TEST_CASE("A component plugin without create_component is an entry point error", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path file = CopyModule(scratch, "testsystem", "components", "testsystem");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "components").string());

    LoadError error = LoadErrorOf([&] { loader.Create("testsystem"); });
    CHECK(error.ReasonGet() == LoadError::Reason::EntryPointMissing);
    CHECK(error.NameGet() == "testsystem");
    CHECK(Mentions(error, "create_component"));
    CHECK(Mentions(error, file.string()));
    CHECK(Mentions(error, "testsystem"));
}

TEST_CASE("A system plugin without create_system is an entry point error", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path file = CopyModule(scratch, "testmodule", "systems", "testmodule");

    SystemLoader loader;
    loader.PathAdd((scratch.Path() / "systems").string());

    LoadError error = LoadErrorOf([&] { loader.Create("testmodule"); });
    CHECK(error.ReasonGet() == LoadError::Reason::EntryPointMissing);
    CHECK(error.NameGet() == "testmodule");
    CHECK(Mentions(error, "create_system"));
    CHECK(Mentions(error, file.string()));
}

TEST_CASE("A missing symbol in a library loader is an entry point error", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path file = CopyModule(scratch, "testmodule", "libs", "testmodule");

    LibraryLoader loader("testmodule");
    loader.PathAdd((scratch.Path() / "libs").string());

    LoadError error = LoadErrorOf([&] { loader.FunctionGet("no_such_function"); });
    CHECK(error.ReasonGet() == LoadError::Reason::EntryPointMissing);
    CHECK(Mentions(error, "no_such_function"));
    CHECK(Mentions(error, file.string()));

    // The symbol that exists is still found afterwards.
    CHECK(loader.FunctionGet("create_component") != nullptr);
}

TEST_CASE("An entry point that produces nothing is NoObject", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path file = CopyModule(scratch, "testmodule", "components", "testmodule");

    ComponentLoader loader;
    loader.PathAdd((scratch.Path() / "components").string());

    nlohmann::json config;
    config["null"] = true;

    std::unique_ptr<ecs::Component> component;
    LoadError error = LoadErrorOf([&] { component = loader.Create("testmodule", &config); });
    CHECK(error.ReasonGet() == LoadError::Reason::NoObject);
    CHECK(error.NameGet() == "testmodule");
    CHECK(Mentions(error, "create_component"));
    CHECK(Mentions(error, file.string()));
    CHECK(Mentions(error, "produced no object"));
    CHECK_FALSE(component);

    // An ordinary request still works afterwards.
    CHECK(loader.Create("testmodule") != nullptr);
}

TEST_CASE("Invalid names are LoadError for every loader", "[PluginLoadErrors]")
{
    const std::vector<std::string> names = {"", "a//b", "..", "a/..", "a\\b", "C:x", "/abs", "a/b/c", "tab\tname"};

    ComponentLoader components;
    SystemLoader systems;
    PakLoader paks;
    for(const std::string &name : names)
    {
        INFO("name: " << name);
        LoadError error = LoadErrorOf([&] { components.Create(name); });
        CHECK(error.ReasonGet() == LoadError::Reason::InvalidName);
        CHECK(Mentions(error, "component plugin"));

        error = LoadErrorOf([&] { systems.Create(name); });
        CHECK(error.ReasonGet() == LoadError::Reason::InvalidName);
        CHECK(Mentions(error, "system plugin"));

        error = LoadErrorOf([&] { paks.Load(name); });
        CHECK(error.ReasonGet() == LoadError::Reason::InvalidName);
        CHECK(Mentions(error, "resource pak"));
    }
}

TEST_CASE("A load retried after a failure succeeds once the plugin is there", "[PluginLoadErrors]")
{
    ScratchDir scratch;
    const fs::path directory = scratch.Path() / "late";
    fs::create_directories(directory);

    ComponentLoader components;
    components.PathAdd(directory.string());
    SystemLoader systems;
    systems.PathAdd(directory.string());

    LoadError error = LoadErrorOf([&] { components.Create("testmodule"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotFound);
    error = LoadErrorOf([&] { systems.Create("testsystem"); });
    CHECK(error.ReasonGet() == LoadError::Reason::NotFound);

    CopyModule(scratch, "testmodule", directory, "testmodule");
    CopyModule(scratch, "testsystem", directory, "testsystem");

    CHECK(components.Create("testmodule") != nullptr);
    CHECK(systems.Create("testsystem") != nullptr);
}
