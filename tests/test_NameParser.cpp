#include <catch_amalgamated.hpp>
#include "../src/NameParser.hpp"

TEST_CASE("NameParser constructs with valid input", "[NameParser]") {
    SECTION("Parses organization and library names") {
        NameParser np("org-name/lib-name");
        REQUIRE(np.org == "org-name");
        REQUIRE(np.library == "lib-name");
    }

    SECTION("Handles only library name") {
        NameParser np("library-only");
        REQUIRE(np.org.empty());
        REQUIRE(np.library == "library-only");
    }
}

TEST_CASE("NameParser throws with invalid input", "[NameParser]") {
    SECTION("Throws when input starts with a slash") {
        REQUIRE_THROWS_AS(NameParser("/invalid"), std::invalid_argument);
    }

    SECTION("Throws when input ends with a slash") {
        REQUIRE_THROWS_AS(NameParser("invalid/"), std::invalid_argument);
    }

    SECTION("Throws when input contains multiple consecutive slashes") {
        REQUIRE_THROWS_AS(NameParser("invalid//name"), std::invalid_argument);
    }

    SECTION("Throws when input contains multiple slashes") {
        REQUIRE_THROWS_AS(NameParser("org/name/extra"), std::invalid_argument);
    }
}
