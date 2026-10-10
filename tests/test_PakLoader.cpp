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

TEST_CASE("PakLoader reports requested resources that the pak does not contain", "[PakLoader]")
{
    using namespace seed::internal;
    seedtest::ScratchDir scratch;
    const std::filesystem::path file = seedtest::WritePak(
        scratch.Path(), "missing", {{"a", Pattern(40, 1)}, {"b", Pattern(300000, 2)}});

    PakLoader loader;
    loader.PathAdd(scratch.Path().string());

    // Runs the request and returns the error, failing when it does not throw.
    auto errorGet = [&](const std::vector<std::string> &names) {
        try
        {
            loader.Load("missing", names);
        }
        catch(const LoadError &error)
        {
            return error;
        }
        catch(...)
        {
            FAIL("an exception other than LoadError escaped");
        }
        FAIL("Load did not throw");
        throw std::logic_error("unreachable");
    };

    SECTION("A missing name fails the request with ResourceMissing")
    {
        const LoadError error = errorGet({"a", "c"});
        REQUIRE(error.ReasonGet() == LoadError::Reason::ResourceMissing);
        REQUIRE(error.NameGet() == "missing");
        REQUIRE(error.MissingGet() == std::vector<std::string>{"c"});
    }

    SECTION("The error names the absolute pak file and no locations")
    {
        const LoadError error = errorGet({"a", "c"});
        REQUIRE(std::filesystem::path(error.FileGet()).is_absolute());
        REQUIRE(std::filesystem::equivalent(error.FileGet(), file));
        REQUIRE(error.LocationsGet().empty());
    }

    SECTION("The message lists the pak, the file and the missing names")
    {
        const LoadError error = errorGet({"a", "c"});
        REQUIRE(error.DetailGet() == "does not contain \"c\"");
        REQUIRE(std::string(error.what()) ==
                "resource pak \"missing\": " + error.FileGet() + " does not contain \"c\"");
    }

    SECTION("Several missing names are listed in request order, each once")
    {
        const LoadError error = errorGet({"c", "d", "c"});
        REQUIRE(error.MissingGet() == std::vector<std::string>{"c", "d"});
        REQUIRE(error.DetailGet() == "does not contain \"c\", \"d\"");
    }

    SECTION("Missing names are reported with the present ones left out")
    {
        const LoadError error = errorGet({"d", "a", "c", "b"});
        REQUIRE(error.MissingGet() == std::vector<std::string>{"d", "c"});
    }

    SECTION("A missing name reads no resource bytes")
    {
        loader.Load("missing", {"a"});
        ResetPakBytesReadCount();
        errorGet({"b", "c"});
        REQUIRE(PakBytesReadCount() == 0);
    }

    SECTION("A first request that names a missing resource reads only the description")
    {
        ResetPakBytesReadCount();
        errorGet({"b", "c"});
        const std::uint64_t read = PakBytesReadCount();
        REQUIRE(read > 0);
        REQUIRE(read < 70000);
    }

    SECTION("The same names can be requested again once they exist")
    {
        errorGet({"c"});
        auto resources = loader.Load("missing", {"a", "b"});
        REQUIRE(resources.size() == 2);
        REQUIRE(resources.at("a")->Data == Pattern(40, 1));
        REQUIRE(resources.at("b")->Data == Pattern(300000, 2));
    }

    SECTION("Naming every resource returns every resource")
    {
        auto resources = loader.Load("missing", {"a", "b"});
        REQUIRE(resources.size() == 2);
        REQUIRE(resources.at("a")->Data == Pattern(40, 1));
        REQUIRE(resources.at("b")->Data == Pattern(300000, 2));
    }

    SECTION("A name given twice returns the resource once")
    {
        auto resources = loader.Load("missing", {"a", "a"});
        REQUIRE(resources.size() == 1);
        REQUIRE(resources.at("a")->Data == Pattern(40, 1));
    }

    SECTION("An empty list returns an empty map")
    {
        REQUIRE(loader.Load("missing", {}).empty());
    }
}

TEST_CASE("PakLoader reports a name missing from a pak with no resources", "[PakLoader]")
{
    seedtest::ScratchDir scratch;
    seedtest::WritePak(scratch.Path(), "hollow", {});
    PakLoader loader;
    loader.PathAdd(scratch.Path().string());

    try
    {
        loader.Load("hollow", {"x"});
        FAIL("Load did not throw");
    }
    catch(const LoadError &error)
    {
        REQUIRE(error.ReasonGet() == LoadError::Reason::ResourceMissing);
        REQUIRE(error.MissingGet() == std::vector<std::string>{"x"});
    }
}

TEST_CASE("PakLoader validates the pak before an empty list returns", "[PakLoader]")
{
    seedtest::ScratchDir scratch;

    SECTION("A damaged pak still reports damaged")
    {
        const std::filesystem::path file = seedtest::WritePak(scratch.Path(), "broken", {{"a", {1, 2, 3}}});
        auto bytes = seedtest::PakBytes({"broken", {{"a", {1, 2, 3}, ""}}});
        seedtest::WriteBytes(file, seedtest::PakTruncate(bytes, 2));

        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        try
        {
            loader.Load("broken", {});
            FAIL("Load did not throw");
        }
        catch(const LoadError &error)
        {
            REQUIRE(error.ReasonGet() == LoadError::Reason::NotLoadable);
            REQUIRE(error.DetailGet().rfind("damaged: ", 0) == 0);
        }
    }

    SECTION("An absent pak is still not found")
    {
        PakLoader loader;
        loader.PathAdd(scratch.Path().string());
        try
        {
            loader.Load("nowhere", {});
            FAIL("Load did not throw");
        }
        catch(const LoadError &error)
        {
            REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        }
    }
}

TEST_CASE("PakLoader picks up a pak that is replaced, rewritten or removed", "[PakLoader]")
{
    using namespace seed::internal;
    seedtest::ScratchDir scratch;
    const std::filesystem::path location = scratch.Path() / "location";
    const std::filesystem::path stage = scratch.Path() / "stage";
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> first = {
        {"a", Pattern(40, 1)}, {"b", Pattern(9000, 2)}};
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> second = {
        {"a", Pattern(55, 77)}, {"b", Pattern(12000, 78)}};
    const std::filesystem::path file = seedtest::WritePak(location, "changing", first);

    PakLoader loader;
    loader.PathAdd(location.string());

    SECTION("A pak replaced by rename is returned new by the next request")
    {
        auto before = loader.Load("changing");
        const std::vector<std::uint8_t> before_a = before.at("a")->Data;
        const ecs::Resource *before_a_address = before.at("a").get();

        std::filesystem::rename(seedtest::WritePak(stage, "changing", second), file);

        auto after = loader.Load("changing");
        REQUIRE(after.at("a")->Data == second[0].second);
        REQUIRE(after.at("b")->Data == second[1].second);
        REQUIRE(after.at("a").get() != before_a_address);

        // What was returned before is unchanged: same bytes, same objects.
        REQUIRE(before.at("a").get() == before_a_address);
        REQUIRE(before.at("a")->Data == before_a);
        REQUIRE(before.at("b")->Data == first[1].second);
    }

    SECTION("A pak replaced by a damaged one is reported damaged, and a later sound one loads")
    {
        loader.Load("changing");
        auto bytes = seedtest::PakBytes({"changing", {{"a", second[0].second, ""}, {"b", second[1].second, ""}}});
        seedtest::WriteBytes(stage / "changing.pak", seedtest::PakTruncate(bytes, 4));
        std::filesystem::rename(stage / "changing.pak", file);

        for(int attempt = 0; attempt < 2; ++attempt)
        {
            INFO("attempt " << attempt);
            try
            {
                loader.Load("changing");
                FAIL("Load did not throw");
            }
            catch(const LoadError &error)
            {
                REQUIRE(error.ReasonGet() == LoadError::Reason::NotLoadable);
                REQUIRE(error.DetailGet().rfind("damaged: ", 0) == 0);
            }
        }

        std::filesystem::rename(seedtest::WritePak(stage, "changing", second), file);
        auto resources = loader.Load("changing");
        REQUIRE(resources.at("a")->Data == second[0].second);
        REQUIRE(resources.at("b")->Data == second[1].second);
    }

    SECTION("A pak deleted after a request is not found")
    {
        loader.Load("changing");
        std::filesystem::remove(file);
        try
        {
            loader.Load("changing");
            FAIL("Load did not throw");
        }
        catch(const LoadError &error)
        {
            REQUIRE(error.ReasonGet() == LoadError::Reason::NotFound);
        }
    }

    SECTION("A pak deleted after a request is found again where a later copy exists")
    {
        const std::filesystem::path later = scratch.Path() / "later";
        seedtest::WritePak(later, "changing", second);
        loader.PathAdd(later.string());

        REQUIRE(loader.Load("changing").at("a")->Data == first[0].second);
        std::filesystem::remove(file);
        REQUIRE(loader.Load("changing").at("a")->Data == second[0].second);
    }

    SECTION("A pak rewritten in place with a different size is validated again")
    {
        loader.Load("changing", {"a"});
        ResetPakDescriptionParseCount();
        loader.Load("changing", {"a"});
        REQUIRE(PakDescriptionParseCount() == 0);

        seedtest::WritePak(location, "changing", second);
        REQUIRE(std::filesystem::file_size(file) != 0);
        auto resources = loader.Load("changing");
        REQUIRE(PakDescriptionParseCount() == 1);
        REQUIRE(resources.at("a")->Data == second[0].second);
        REQUIRE(resources.at("b")->Data == second[1].second);
    }

    SECTION("Deleting or renaming over the pak succeeds while the loader holds its description")
    {
        loader.Load("changing");
        std::error_code ec;

        std::filesystem::rename(seedtest::WritePak(stage, "changing", second), file, ec);
        REQUIRE_FALSE(ec);
        REQUIRE(loader.Load("changing").at("b")->Data == second[1].second);

        REQUIRE(std::filesystem::remove(file, ec));
        REQUIRE_FALSE(ec);
        REQUIRE_FALSE(std::filesystem::exists(file));
    }
}
