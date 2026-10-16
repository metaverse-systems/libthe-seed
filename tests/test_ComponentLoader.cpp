#include <catch_amalgamated.hpp>
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <stdexcept>

TEST_CASE("ComponentLoader instance isolation", "[ComponentLoader]") {
    SECTION("Default construction creates empty state") {
        the_seed::ComponentLoader loader;
        REQUIRE(loader.PathsGet().empty());
    }

    SECTION("PathAdd and PathsGet are per-instance") {
        the_seed::ComponentLoader a;
        the_seed::ComponentLoader b;

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
        the_seed::ComponentLoader first;
        first.PathAdd("/shared/test/path");

        the_seed::ComponentLoader second;
        REQUIRE(second.PathsGet().empty());
    }
}

TEST_CASE("ComponentLoader Create error handling", "[ComponentLoader]") {
    SECTION("Create throws runtime_error for nonexistent component") {
        the_seed::ComponentLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Create("nonexistent/component"), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Create("nonexistent/component"), the_seed::LoadError);
    }

    SECTION("Create with data throws runtime_error for nonexistent component") {
        the_seed::ComponentLoader loader;
        loader.PathAdd("/nonexistent/path");
        int data = 42;
        REQUIRE_THROWS_AS(loader.Create("nonexistent/component", &data), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Create("nonexistent/component", &data), the_seed::LoadError);
    }
}

TEST_CASE("ComponentLoader Get error handling", "[ComponentLoader]") {
    SECTION("Get throws runtime_error for nonexistent component") {
        the_seed::ComponentLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Get("nonexistent/component"), std::runtime_error);
        REQUIRE_THROWS_AS(loader.Get("nonexistent/component"), the_seed::LoadError);
    }
}

TEST_CASE("ComponentLoader destruction completes and unloads nothing", "[ComponentLoader]") {
    SECTION("Destruction completes without error") {
        auto loader = std::make_unique<the_seed::ComponentLoader>();
        loader->PathAdd("/some/path");
        REQUIRE_NOTHROW(loader.reset());
    }
}
