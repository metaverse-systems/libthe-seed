#include <catch_amalgamated.hpp>
#include <libthe-seed/LoadError.hpp>
#include "../src/NameParser.hpp"

#include <string>

namespace
{
    const char *const kind = "component plugin";

    bool Contains(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }

    // Parses the name and returns the LoadError it throws; fails the test if
    // it does not throw one.
    LoadError InvalidNameCatch(const std::string &name)
    {
        try
        {
            NameParser parser(name, kind);
        }
        catch(const LoadError &error)
        {
            return error;
        }
        FAIL("no LoadError for name \"" << name << "\"");
        throw 0;
    }

    void InvalidNameCheck(const std::string &name, const std::string &rule)
    {
        LoadError error = InvalidNameCatch(name);

        REQUIRE(error.ReasonGet() == LoadError::Reason::InvalidName);
        REQUIRE(Contains(error.what(), kind));
        REQUIRE(Contains(error.what(), rule));
        REQUIRE(error.LocationsGet().empty());
    }
}

TEST_CASE("NameParser constructs with valid input", "[NameParser]") {
    SECTION("Parses organization and library names") {
        NameParser np("org-name/lib-name", kind);
        REQUIRE(np.org == "org-name");
        REQUIRE(np.library == "lib-name");
    }

    SECTION("Handles only library name") {
        NameParser np("library-only", kind);
        REQUIRE(np.org.empty());
        REQUIRE(np.library == "library-only");
    }

    SECTION("Names in use still parse") {
        NameParser plain("testmodule", kind);
        REQUIRE(plain.org.empty());
        REQUIRE(plain.library == "testmodule");

        NameParser organized("org/my-component", kind);
        REQUIRE(organized.org == "org");
        REQUIRE(organized.library == "my-component");

        NameParser scoped("@scope/name", kind);
        REQUIRE(scoped.org == "@scope");
        REQUIRE(scoped.library == "name");

        NameParser punctuated("a.b-c_d+e", kind);
        REQUIRE(punctuated.org.empty());
        REQUIRE(punctuated.library == "a.b-c_d+e");
    }

    SECTION("Dots inside a part are allowed") {
        NameParser np("my.org/lib.v2", kind);
        REQUIRE(np.org == "my.org");
        REQUIRE(np.library == "lib.v2");
    }
}

TEST_CASE("NameParser throws LoadError with invalid input", "[NameParser]") {
    SECTION("Throws when input starts with a slash") {
        InvalidNameCheck("/invalid", "leading \"/\"");
    }

    SECTION("Throws when input ends with a slash") {
        InvalidNameCheck("invalid/", "empty part");
    }

    SECTION("Throws when input contains multiple consecutive slashes") {
        LoadError error = InvalidNameCatch("invalid//name");
        REQUIRE(error.ReasonGet() == LoadError::Reason::InvalidName);
        REQUIRE((Contains(error.what(), "empty part") || Contains(error.what(), "more than one \"/\"")));
    }

    SECTION("Throws when input contains multiple slashes") {
        InvalidNameCheck("org/name/extra", "more than one \"/\"");
    }

    SECTION("Throws on an empty name") {
        InvalidNameCheck("", "empty part");
    }

    SECTION("Throws on an empty organization or library part") {
        InvalidNameCheck("/", "empty part");
    }
}

TEST_CASE("NameParser rejects names that could leave the plugin directory", "[NameParser]") {
    SECTION("Dot part") {
        InvalidNameCheck(".", "\".\" or \"..\" as a part");
        InvalidNameCheck("org/.", "\".\" or \"..\" as a part");
        InvalidNameCheck("./lib", "\".\" or \"..\" as a part");
    }

    SECTION("Dot-dot part") {
        InvalidNameCheck("..", "\".\" or \"..\" as a part");
        InvalidNameCheck("../lib", "\".\" or \"..\" as a part");
        InvalidNameCheck("org/..", "\".\" or \"..\" as a part");
    }

    SECTION("Backslash") {
        InvalidNameCheck("a\\b", "backslash");
        InvalidNameCheck("org\\lib", "backslash");
        InvalidNameCheck("org/lib\\", "backslash");
    }

    SECTION("Colon and drive prefix") {
        InvalidNameCheck("C:", "\":\"");
        InvalidNameCheck("C:lib", "\":\"");
        InvalidNameCheck("c:/lib", "\":\"");
        InvalidNameCheck("org/a:b", "\":\"");
    }

    SECTION("Absolute path") {
        InvalidNameCheck("/etc/passwd", "leading \"/\"");
        InvalidNameCheck("/lib", "leading \"/\"");
    }
}

TEST_CASE("NameParser rejects control characters and escapes them in the message", "[NameParser]") {
    SECTION("Newline") {
        LoadError error = InvalidNameCatch("lib\nname");
        REQUIRE(error.ReasonGet() == LoadError::Reason::InvalidName);
        REQUIRE(Contains(error.what(), "control character"));
        REQUIRE(Contains(error.what(), "lib"));
        REQUIRE(Contains(error.what(), "name"));
        REQUIRE_FALSE(Contains(error.what(), "lib\nname"));
    }

    SECTION("Tab and carriage return") {
        InvalidNameCheck("a\tb", "control character");
        InvalidNameCheck("a\rb", "control character");
    }

    SECTION("Low control character") {
        std::string name("a");
        name.push_back('\x01');
        name += "b";
        LoadError error = InvalidNameCatch(name);
        REQUIRE(Contains(error.what(), "control character"));
        REQUIRE_FALSE(Contains(error.what(), name));
    }

    SECTION("Embedded NUL") {
        std::string name("a");
        name.push_back('\0');
        name += "b";
        LoadError error = InvalidNameCatch(name);
        REQUIRE(Contains(error.what(), "control character"));
        REQUIRE(std::string(error.what()).find('\0') == std::string::npos);
    }

    SECTION("DEL (0x7F)") {
        std::string name("a");
        name.push_back('\x7f');
        name += "b";
        LoadError error = InvalidNameCatch(name);
        REQUIRE(Contains(error.what(), "control character"));
        REQUIRE_FALSE(Contains(error.what(), name));
    }
}

TEST_CASE("NameParser names the kind and the name in its error", "[NameParser]") {
    SECTION("Kind appears in the message") {
        try
        {
            NameParser parser("..", "resource pak");
            FAIL("no LoadError");
        }
        catch(const LoadError &error)
        {
            REQUIRE(Contains(error.what(), "resource pak"));
        }
    }

    SECTION("Name is kept") {
        LoadError error = InvalidNameCatch("../escape");
        REQUIRE(error.NameGet() == "../escape");
        REQUIRE(Contains(error.what(), "../escape"));
    }
}
