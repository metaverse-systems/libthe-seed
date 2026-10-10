#include <catch_amalgamated.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "LoaderTestSupport.hpp"
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
