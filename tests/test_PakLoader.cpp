#include <catch_amalgamated.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "LoaderTestSupport.hpp"
#include "internal/PakFile.hpp"
#include "internal/PakIndex.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

TEST_CASE("PakLoader instance isolation", "[PakLoader]") {
    SECTION("Default construction creates empty state") {
        PakLoader loader;
        REQUIRE(loader.PathsGet().empty());
    }

    SECTION("PathAdd and PathsGet are per-instance") {
        PakLoader a;
        PakLoader b;

        a.PathAdd("/path/a");
        b.PathAdd("/path/b1");
        b.PathAdd("/path/b2");

        auto paths_a = a.PathsGet();
        auto paths_b = b.PathsGet();

        REQUIRE(paths_a.size() == 1);
        REQUIRE(paths_a[0] == "/path/a");

        REQUIRE(paths_b.size() == 2);
        REQUIRE(paths_b[0] == "/path/b1");
        REQUIRE(paths_b[1] == "/path/b2");
    }

    SECTION("Instances do not share search paths") {
        PakLoader first;
        first.PathAdd("/shared/test/path");

        PakLoader second;
        REQUIRE(second.PathsGet().empty());
    }
}

TEST_CASE("PakLoader Load error handling", "[PakLoader]") {
    SECTION("Load throws runtime_error for nonexistent pak") {
        PakLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Load("nonexistent/pak"), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Load("nonexistent/pak"), LoadError);
    }

    SECTION("Filtered Load throws runtime_error for nonexistent pak") {
        PakLoader loader;
        loader.PathAdd("/nonexistent/path");
        std::vector<std::string> names = {"resource1"};
        REQUIRE_THROWS_AS(loader.Load("nonexistent/pak", names), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Load("nonexistent/pak", names), LoadError);
    }
}

TEST_CASE("PakLoader destruction completes", "[PakLoader]") {
    SECTION("Destruction completes without error") {
        auto loader = std::make_unique<PakLoader>();
        loader->PathAdd("/some/path");
        REQUIRE_NOTHROW(loader.reset());
    }
}

#ifndef _WIN32
namespace
{
    // Makes a folder unreadable for the life of the object. Reports whether
    // the restriction can have an effect (it has none for the superuser).
    class Locked
    {
    public:
        explicit Locked(const std::filesystem::path &dir) : dir(dir)
        {
            std::filesystem::permissions(dir, std::filesystem::perms::none);
        }
        ~Locked()
        {
            std::error_code ec;
            std::filesystem::permissions(this->dir, std::filesystem::perms::owner_all, ec);
        }
        Locked(const Locked &) = delete;
        Locked &operator=(const Locked &) = delete;

    private:
        std::filesystem::path dir;
    };
}

TEST_CASE("PakLoader search passes over an unreadable location", "[PakLoader]") {
    if(::geteuid() == 0)
    {
        WARN("skipped: running as root, a folder with no permissions is still readable");
        return;
    }

    seedtest::ScratchDir scratch;
    const std::filesystem::path locked = scratch.Path() / "locked";
    const std::filesystem::path good = scratch.Path() / "good";
    std::filesystem::create_directories(locked);
    seedtest::WritePak(locked, "lockedpak", {{"item", {1}}});
    seedtest::WritePak(good, "lockedpak", {{"item", {2}}});

    SECTION("A later location still loads the pak") {
        Locked lock(locked);
        PakLoader loader;
        loader.PathAdd(locked.string());
        loader.PathAdd(good.string());

        auto resources = loader.Load("lockedpak");
        REQUIRE(resources.size() == 1);
        REQUIRE(resources.at("item")->Data == std::vector<std::uint8_t>{2});
    }

    SECTION("With no other copy the message names the location and the system text") {
        Locked lock(locked);
        PakLoader loader;
        loader.PathAdd(locked.string());

        try
        {
            loader.Load("lockedpak");
            FAIL("Load did not throw");
        }
        catch(const LoadError &error)
        {
            REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
            REQUIRE(error.LocationsGet().size() >= 1);
            REQUIRE(error.LocationsGet()[0].state == LoadError::LocationState::Unreadable);
            REQUIRE(error.LocationsGet()[0].reason == "Permission denied");
            REQUIRE(std::string(error.what()).find("could not be examined: " + locked.string() +
                                                   ": Permission denied") != std::string::npos);
        }
    }
}
#endif

namespace
{
    std::vector<std::uint8_t> Pattern(std::size_t count, std::uint8_t first)
    {
        std::vector<std::uint8_t> bytes(count);
        for(std::size_t i = 0; i < count; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(first + i * 3);
        }
        return bytes;
    }
}

TEST_CASE("PakLoader loads a writer's pak and reads the description once per loader", "[PakLoader]")
{
    using namespace seed::internal;
    seedtest::ScratchDir scratch;
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> contents = {
        {"zeta", Pattern(300, 1)}, {"alpha", Pattern(5, 40)}, {"empty", {}}, {"mid", Pattern(70000, 9)}};
    seedtest::WritePak(scratch.Path(), "several", contents);

    SECTION("Load returns every resource with its exact bytes")
    {
        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        auto resources = loader.Load("several");
        REQUIRE(resources.size() == contents.size());
        for(const auto &item : contents)
        {
            INFO(item.first);
            REQUIRE(resources.count(item.first) == 1);
            REQUIRE(resources.at(item.first)->Data == item.second);
        }
    }

    SECTION("A filtered Load returns only the named resources")
    {
        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        auto resources = loader.Load("several", {"mid", "empty"});
        REQUIRE(resources.size() == 2);
        REQUIRE(resources.at("mid")->Data == contents[3].second);
        REQUIRE(resources.at("empty")->Data.empty());
    }

    SECTION("Two requests through one loader parse the description once")
    {
        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        ResetPakDescriptionParseCount();
        loader.Load("several", {"zeta"});
        REQUIRE(PakDescriptionParseCount() == 1);
        loader.Load("several", {"alpha"});
        loader.Load("several");
        REQUIRE(PakDescriptionParseCount() == 1);
    }

    SECTION("Two loaders each parse the description once")
    {
        PakLoader first;
        PakLoader second;
        first.PathAdd(scratch.Path().string());
        second.PathAdd(scratch.Path().string());
        ResetPakDescriptionParseCount();
        first.Load("several", {"zeta"});
        first.Load("several", {"mid"});
        REQUIRE(PakDescriptionParseCount() == 1);
        second.Load("several", {"zeta"});
        second.Load("several", {"mid"});
        REQUIRE(PakDescriptionParseCount() == 2);
    }

    SECTION("A second request for one resource reads no description")
    {
        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        loader.Load("several", {"zeta"});
        ResetPakBytesReadCount();
        loader.Load("several", {"alpha"});
        REQUIRE(PakBytesReadCount() == 5);
    }

    SECTION("Resources stored with Container::Resources are shared, not copied")
    {
        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        auto resources = loader.Load("several");
        ecs::Manager manager;
        auto *world = manager.Container("world");
        world->Resources(resources);
        for(const auto &entry : resources)
        {
            INFO(entry.first);
            REQUIRE(world->ResourceGet(entry.first).get() == entry.second.get());
        }
    }
}

TEST_CASE("PakLoader loads a pak with no resources", "[PakLoader]")
{
    seedtest::ScratchDir scratch;
    seedtest::WritePak(scratch.Path(), "nothing", {});
    PakLoader loader;
    loader.PathAdd(scratch.Path().string());
    REQUIRE(loader.Load("nothing").empty());
    REQUIRE(loader.Load("nothing", {}).empty());
}
