#include <catch_amalgamated.hpp>
#include <libthe-seed/LoadError.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    bool Contains(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }

    std::vector<LoadError::Location> LocationsMake()
    {
        return {
            {"/etc/app/plugins", false, LoadError::LocationState::Searched},
            {"/opt/missing", false, LoadError::LocationState::Missing},
            {"/home/dev/build", true, LoadError::LocationState::Searched},
        };
    }
}

TEST_CASE("LoadError returns what it was given", "[LoadError]")
{
    const LoadError::Reason reasons[] = {
        LoadError::Reason::InvalidName,
        LoadError::Reason::NotFound,
        LoadError::Reason::NotLoadable,
        LoadError::Reason::EntryPointMissing,
        LoadError::Reason::NoObject,
        LoadError::Reason::SceneUnopenable,
        LoadError::Reason::SceneNotUnderstood,
        LoadError::Reason::SceneComponentFailed,
    };

    for(LoadError::Reason reason : reasons)
    {
        LoadError error(reason, "org/thing", "/some/dir/libthing.so", LocationsMake(), "some detail");

        REQUIRE(error.ReasonGet() == reason);
        REQUIRE(error.NameGet() == "org/thing");
        REQUIRE(error.FileGet() == "/some/dir/libthing.so");
        REQUIRE(error.DetailGet() == "some detail");

        const std::vector<LoadError::Location> &locations = error.LocationsGet();
        REQUIRE(locations.size() == 3);
        REQUIRE(locations[0].path == "/etc/app/plugins");
        REQUIRE_FALSE(locations[0].development);
        REQUIRE(locations[0].state == LoadError::LocationState::Searched);
        REQUIRE(locations[1].path == "/opt/missing");
        REQUIRE(locations[1].state == LoadError::LocationState::Missing);
        REQUIRE(locations[2].path == "/home/dev/build");
        REQUIRE(locations[2].development);
    }
}

TEST_CASE("LoadError accessors keep empty values", "[LoadError]")
{
    LoadError error(LoadError::Reason::NoObject, "", "", {}, "");

    REQUIRE(error.ReasonGet() == LoadError::Reason::NoObject);
    REQUIRE(error.NameGet().empty());
    REQUIRE(error.FileGet().empty());
    REQUIRE(error.LocationsGet().empty());
    REQUIRE(error.DetailGet().empty());
}

TEST_CASE("LoadError is a std::runtime_error", "[LoadError]")
{
    SECTION("Caught as std::runtime_error")
    {
        REQUIRE_THROWS_AS(
            throw LoadError(LoadError::Reason::NotFound, "thing", "", {}, ""),
            std::runtime_error);
    }

    SECTION("Caught as std::exception")
    {
        REQUIRE_THROWS_AS(
            throw LoadError(LoadError::Reason::NotFound, "thing", "", {}, ""),
            std::exception);
    }

    SECTION("Caught as LoadError with its reason")
    {
        try
        {
            throw LoadError(LoadError::Reason::EntryPointMissing, "thing", "/d/libthing.so", {}, "undefined symbol");
            FAIL("not reached");
        }
        catch(const std::runtime_error &error)
        {
            const LoadError *load_error = dynamic_cast<const LoadError *>(&error);
            REQUIRE(load_error != nullptr);
            REQUIRE(load_error->ReasonGet() == LoadError::Reason::EntryPointMissing);
        }
    }
}

TEST_CASE("LoadError is copyable", "[LoadError]")
{
    LoadError original(LoadError::Reason::NotFound, "org/thing", "libthing.so", LocationsMake(), "");
    const std::string message = original.what();

    SECTION("Copy construction")
    {
        LoadError copy(original);

        REQUIRE(copy.ReasonGet() == original.ReasonGet());
        REQUIRE(copy.NameGet() == "org/thing");
        REQUIRE(copy.FileGet() == "libthing.so");
        REQUIRE(copy.LocationsGet().size() == 3);
        REQUIRE(message == copy.what());
    }

    SECTION("Copy assignment")
    {
        LoadError other(LoadError::Reason::NoObject, "other", "", {}, "x");
        other = original;

        REQUIRE(other.ReasonGet() == LoadError::Reason::NotFound);
        REQUIRE(other.NameGet() == "org/thing");
        REQUIRE(message == other.what());
    }

    SECTION("Copy survives the original")
    {
        LoadError *heap = new LoadError(original);
        LoadError copy(*heap);
        delete heap;

        REQUIRE(copy.NameGet() == "org/thing");
        REQUIRE(message == copy.what());
    }
}

TEST_CASE("LoadError message for InvalidName", "[LoadError]")
{
    LoadError error(LoadError::Reason::InvalidName, "a\\b", "", {}, "backslash");
    const std::string message = error.what();

    REQUIRE(Contains(message, "a\\b"));
    REQUIRE(Contains(message, "backslash"));
    REQUIRE(Contains(message, "invalid"));
}

TEST_CASE("LoadError message for NotFound lists every location with its state", "[LoadError]")
{
    LoadError error(LoadError::Reason::NotFound, "org/thing", "libthing.so", LocationsMake(), "");
    const std::string message = error.what();

    REQUIRE(Contains(message, "org/thing"));
    REQUIRE(Contains(message, "libthing.so"));
    REQUIRE(Contains(message, "not found"));
    REQUIRE(Contains(message, "searched: /etc/app/plugins"));
    REQUIRE(Contains(message, "does not exist: /opt/missing"));
    REQUIRE(Contains(message, "development location, searched: /home/dev/build"));

    // Locations appear in search order.
    REQUIRE(message.find("/etc/app/plugins") < message.find("/opt/missing"));
    REQUIRE(message.find("/opt/missing") < message.find("/home/dev/build"));

    // Detail lines are indented by two spaces.
    REQUIRE(Contains(message, "\n  searched: /etc/app/plugins"));
}

TEST_CASE("LoadError message for NotFound with no locations", "[LoadError]")
{
    LoadError error(LoadError::Reason::NotFound, "thing", "libthing.so", {}, "");
    const std::string message = error.what();

    REQUIRE(Contains(message, "thing"));
    REQUIRE(Contains(message, "no locations are configured"));
}

TEST_CASE("LoadError message for NotLoadable", "[LoadError]")
{
    std::vector<LoadError::Location> locations = {
        {"/first", false, LoadError::LocationState::Missing},
        {"/second", false, LoadError::LocationState::Found},
    };
    LoadError error(LoadError::Reason::NotLoadable, "thing", "/second/libthing.so", locations,
                    "invalid ELF header\r\n");
    const std::string message = error.what();

    REQUIRE(Contains(message, "thing"));
    REQUIRE(Contains(message, "/second/libthing.so"));
    REQUIRE(Contains(message, "present but could not be loaded"));
    REQUIRE(Contains(message, "invalid ELF header"));
    REQUIRE(Contains(message, "searched before it"));
    REQUIRE(Contains(message, "/first"));

    // Trailing line breaks of platform text are trimmed.
    REQUIRE(message.back() != '\n');
    REQUIRE(message.back() != '\r');
}

TEST_CASE("LoadError message for EntryPointMissing", "[LoadError]")
{
    LoadError error(LoadError::Reason::EntryPointMissing, "thing", "/d/libthing.so", {},
                    "create_component: undefined symbol");
    const std::string message = error.what();

    REQUIRE(Contains(message, "thing"));
    REQUIRE(Contains(message, "/d/libthing.so"));
    REQUIRE(Contains(message, "create_component"));
    REQUIRE(Contains(message, "no entry point"));
    REQUIRE(Contains(message, "undefined symbol"));
}

TEST_CASE("LoadError message for NoObject", "[LoadError]")
{
    LoadError error(LoadError::Reason::NoObject, "thing", "/d/libthing.so", {}, "create_component");
    const std::string message = error.what();

    REQUIRE(Contains(message, "thing"));
    REQUIRE(Contains(message, "/d/libthing.so"));
    REQUIRE(Contains(message, "create_component"));
    REQUIRE(Contains(message, "produced no object"));
}

TEST_CASE("LoadError message for SceneUnopenable", "[LoadError]")
{
    LoadError error(LoadError::Reason::SceneUnopenable, "", "scenes/level.json", {},
                    "No such file or directory");
    const std::string message = error.what();

    REQUIRE(Contains(message, "scenes/level.json"));
    REQUIRE(Contains(message, "could not be opened"));
    REQUIRE(Contains(message, "No such file or directory"));
    REQUIRE_FALSE(Contains(message, "parse"));
}

TEST_CASE("LoadError message for SceneNotUnderstood", "[LoadError]")
{
    LoadError error(LoadError::Reason::SceneNotUnderstood, "", "scenes/level.json", {},
                    "syntax error at line 3, column 7");
    const std::string message = error.what();

    REQUIRE(Contains(message, "scenes/level.json"));
    REQUIRE(Contains(message, "could not be understood"));
    REQUIRE(Contains(message, "line 3, column 7"));
}

TEST_CASE("LoadError message for SceneComponentFailed", "[LoadError]")
{
    LoadError error(LoadError::Reason::SceneComponentFailed, "", "scenes/level.json", {},
                    "entity \"player\", component \"org/missing\": component plugin \"org/missing\" not found");
    const std::string message = error.what();

    REQUIRE(Contains(message, "scenes/level.json"));
    REQUIRE(Contains(message, "player"));
    REQUIRE(Contains(message, "org/missing"));
    REQUIRE(Contains(message, "not found"));
}

TEST_CASE("LoadError message for ResourceMissing", "[LoadError]")
{
    const std::vector<std::string> missing = {"tex_hero", "snd_jump"};
    LoadError error(LoadError::Reason::ResourceMissing, "org/art", "/game/paks/art.pak", {}, "", "resource pak",
                    missing);
    const std::string message = error.what();

    REQUIRE(error.ReasonGet() == LoadError::Reason::ResourceMissing);
    REQUIRE(error.NameGet() == "org/art");
    REQUIRE(error.FileGet() == "/game/paks/art.pak");
    REQUIRE(error.KindGet() == "resource pak");
    REQUIRE(error.LocationsGet().empty());

    // MissingGet returns the list unchanged: same names, same order.
    REQUIRE(error.MissingGet() == missing);

    REQUIRE(Contains(message, "resource pak \"org/art\""));
    REQUIRE(Contains(message, "/game/paks/art.pak"));
    REQUIRE(Contains(message, "does not contain \"tex_hero\", \"snd_jump\""));
    REQUIRE(message.find("\"tex_hero\"") < message.find("\"snd_jump\""));
    REQUIRE(message.find('\n') == std::string::npos);

    SECTION("The list survives a copy")
    {
        LoadError copy(error);
        REQUIRE(copy.MissingGet() == missing);
        REQUIRE(std::string(copy.what()) == message);
    }

    SECTION("Control characters in a missing name are escaped")
    {
        LoadError odd(LoadError::Reason::ResourceMissing, "org/art", "/game/paks/art.pak", {}, "", "resource pak",
                      {"two\nlines", "tab\there", "bell\x07"});
        const std::string text = odd.what();

        REQUIRE(Contains(text, "\"two\\nlines\""));
        REQUIRE(Contains(text, "\"tab\\there\""));
        REQUIRE(Contains(text, "\"bell\\x07\""));
        REQUIRE(text.find('\n') == std::string::npos);
        REQUIRE(text.find('\t') == std::string::npos);
        // The names themselves are returned as they were given.
        REQUIRE(odd.MissingGet()[0] == "two\nlines");
    }
}

TEST_CASE("LoadError MissingGet is empty for every other reason", "[LoadError]")
{
    const LoadError::Reason reasons[] = {
        LoadError::Reason::InvalidName,
        LoadError::Reason::NotFound,
        LoadError::Reason::NotLoadable,
        LoadError::Reason::EntryPointMissing,
        LoadError::Reason::NoObject,
        LoadError::Reason::SceneUnopenable,
        LoadError::Reason::SceneNotUnderstood,
        LoadError::Reason::SceneComponentFailed,
    };

    for(LoadError::Reason reason : reasons)
    {
        LoadError error(reason, "org/thing", "/some/dir/libthing.so", LocationsMake(), "some detail");
        REQUIRE(error.MissingGet().empty());
    }
}

TEST_CASE("LoadError message for an unreadable location", "[LoadError]")
{
    std::vector<LoadError::Location> locations = {
        {"/game/paks", false, LoadError::LocationState::Searched},
        {"/locked/paks", false, LoadError::LocationState::Unreadable, "Permission denied"},
        {"../../art", true, LoadError::LocationState::Missing},
    };
    LoadError error(LoadError::Reason::NotFound, "org/art", "art.pak", locations, "", "resource pak");
    const std::string message = error.what();

    REQUIRE(error.LocationsGet().size() == 3);
    REQUIRE(error.LocationsGet()[1].state == LoadError::LocationState::Unreadable);
    REQUIRE(error.LocationsGet()[1].reason == "Permission denied");
    REQUIRE(error.LocationsGet()[0].reason.empty());

    REQUIRE(Contains(message, "\n  searched: /game/paks"));
    REQUIRE(Contains(message, "\n  could not be examined: /locked/paks: Permission denied"));
    REQUIRE(Contains(message, "\n  development location, does not exist: ../../art"));
    REQUIRE(message.find("/game/paks") < message.find("/locked/paks"));
    REQUIRE(message.find("/locked/paks") < message.find("../../art"));
}

TEST_CASE("LoadError message for NotFound with no locations and a detail", "[LoadError]")
{
    LoadError error(LoadError::Reason::NotFound, "/game/paks/art.pak", "/game/paks/art.pak", {},
                    "No such file or directory", "resource pak");
    const std::string message = error.what();

    REQUIRE(message ==
            "resource pak \"/game/paks/art.pak\" not found: /game/paks/art.pak: No such file or directory");
    REQUIRE_FALSE(Contains(message, "no locations are configured"));
}

TEST_CASE("LoadError Location still takes three members", "[LoadError]")
{
    const LoadError::Location location{"/some/dir", true, LoadError::LocationState::NotReached};

    REQUIRE(location.path == "/some/dir");
    REQUIRE(location.development);
    REQUIRE(location.state == LoadError::LocationState::NotReached);
    REQUIRE(location.reason.empty());
}
