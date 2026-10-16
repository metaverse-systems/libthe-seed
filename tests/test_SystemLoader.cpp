#include <catch_amalgamated.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <stdexcept>

TEST_CASE("SystemLoader instance isolation", "[SystemLoader]") {
    SECTION("Default construction creates empty state") {
        the_seed::SystemLoader loader;
        REQUIRE(loader.PathsGet().empty());
    }

    SECTION("PathAdd and PathsGet are per-instance") {
        the_seed::SystemLoader a;
        the_seed::SystemLoader b;

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
        the_seed::SystemLoader first;
        first.PathAdd("/shared/test/path");

        the_seed::SystemLoader second;
        REQUIRE(second.PathsGet().empty());
    }
}

TEST_CASE("SystemLoader Create error handling", "[SystemLoader]") {
    SECTION("Create throws runtime_error for nonexistent system") {
        the_seed::SystemLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Create("nonexistent/system"), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Create("nonexistent/system"), the_seed::LoadError);
    }

    SECTION("Create with data throws runtime_error for nonexistent system") {
        the_seed::SystemLoader loader;
        loader.PathAdd("/nonexistent/path");
        int data = 42;
        REQUIRE_THROWS_AS(loader.Create("nonexistent/system", &data), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Create("nonexistent/system", &data), the_seed::LoadError);
    }
}

TEST_CASE("SystemLoader Get error handling", "[SystemLoader]") {
    SECTION("Get throws runtime_error for nonexistent system") {
        the_seed::SystemLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Get("nonexistent/system"), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Get("nonexistent/system"), the_seed::LoadError);
    }
}

TEST_CASE("SystemLoader destruction completes and unloads nothing", "[SystemLoader]") {
    SECTION("Destruction completes without error") {
        auto loader = std::make_unique<the_seed::SystemLoader>();
        loader->PathAdd("/some/path");
        REQUIRE_NOTHROW(loader.reset());
    }
}
