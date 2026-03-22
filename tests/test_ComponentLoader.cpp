#include <catch_amalgamated.hpp>
#include <libthe-seed/ComponentLoader.hpp>

TEST_CASE("ComponentLoader instance isolation", "[ComponentLoader]") {
    SECTION("Default construction creates empty state") {
        ComponentLoader loader;
        REQUIRE(loader.PathsGet().empty());
    }

    SECTION("PathAdd and PathsGet are per-instance") {
        ComponentLoader a;
        ComponentLoader b;

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
        ComponentLoader first;
        first.PathAdd("/shared/test/path");

        ComponentLoader second;
        REQUIRE(second.PathsGet().empty());
    }
}

TEST_CASE("ComponentLoader Create error handling", "[ComponentLoader]") {
    SECTION("Create throws runtime_error for nonexistent component") {
        ComponentLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Create("nonexistent/component"), std::runtime_error);
    }

    SECTION("Create with data throws runtime_error for nonexistent component") {
        ComponentLoader loader;
        loader.PathAdd("/nonexistent/path");
        int data = 42;
        REQUIRE_THROWS_AS(loader.Create("nonexistent/component", &data), std::runtime_error);
    }
}

TEST_CASE("ComponentLoader Get error handling", "[ComponentLoader]") {
    SECTION("Get throws runtime_error for nonexistent component") {
        ComponentLoader loader;
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.Get("nonexistent/component"), std::runtime_error);
    }
}

TEST_CASE("ComponentLoader destruction releases cached handles", "[ComponentLoader]") {
    SECTION("Destruction completes without error") {
        auto loader = std::make_unique<ComponentLoader>();
        loader->PathAdd("/some/path");
        REQUIRE_NOTHROW(loader.reset());
    }
}
