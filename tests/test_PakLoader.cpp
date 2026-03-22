#include <catch_amalgamated.hpp>
#include <libthe-seed/PakLoader.hpp>

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
    }

    SECTION("Filtered Load throws runtime_error for nonexistent pak") {
        PakLoader loader;
        loader.PathAdd("/nonexistent/path");
        std::vector<std::string> names = {"resource1"};
        REQUIRE_THROWS_AS(loader.Load("nonexistent/pak", names), std::runtime_error);
    }
}

TEST_CASE("PakLoader destruction releases paths", "[PakLoader]") {
    SECTION("Destruction completes without error") {
        auto loader = std::make_unique<PakLoader>();
        loader->PathAdd("/some/path");
        REQUIRE_NOTHROW(loader.reset());
    }
}
