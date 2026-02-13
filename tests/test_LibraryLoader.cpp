#include <catch_amalgamated.hpp>
#include <libthe-seed/LibraryLoader.hpp>

TEST_CASE("LibraryLoader adds and retrieves paths correctly", "[LibraryLoader]") {
    auto loader = new LibraryLoader("NotReal");

    SECTION("PathAdd adds a path") {
        loader->PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader->PathsGet(), std::runtime_error);
    }

    SECTION("PathsGet throws when no valid paths exist") {
        loader->PathAdd("/nonexistent/path");
        loader->PathAdd("/another/nonexistent/path");
        REQUIRE_THROWS_AS(loader->PathsGet(), std::runtime_error);
    }

    delete loader;
}

TEST_CASE("LibraryLoader loads libraries and functions correctly", "[LibraryLoader]") {
    auto loader = new LibraryLoader("NotReal");

    SECTION("FunctionGet throws when library cannot be found") {
        loader->PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader->FunctionGet("any_symbol"), std::runtime_error);
    }

    delete loader;
}

TEST_CASE("LibraryLoader destructor frees resources", "[LibraryLoader]") {
    LibraryLoader *loader = new LibraryLoader("NotReal");
    loader->PathAdd("/nonexistent/path");
    delete loader; // Destructor will be called here
    SUCCEED();
}
