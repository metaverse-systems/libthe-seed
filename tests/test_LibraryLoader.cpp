#include "LoaderTestSupport.hpp"
#include <libthe-seed/LibraryLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libecs-cpp/ecs.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>

TEST_CASE("LibraryLoader adds and retrieves paths correctly", "[LibraryLoader]") {
    the_seed::LibraryLoader loader("NotReal");

    SECTION("PathsGet is empty before any path is added") {
        REQUIRE(loader.PathsGet().empty());
    }

    SECTION("PathsGet returns the added locations in order") {
        loader.PathAdd("/nonexistent/path");
        loader.PathAdd("/another/nonexistent/path");
        REQUIRE(loader.PathsGet() == std::vector<std::string>{"/nonexistent/path", "/another/nonexistent/path"});
    }

    SECTION("PathsGet never throws, even when no library is present") {
        loader.PathAdd("/nonexistent/path");
        REQUIRE_NOTHROW(loader.PathsGet());
    }

    SECTION("PathsGet returns locations, not candidate files") {
        the_seed::LibraryLoader real("testmodule");
        real.PathAdd(seedtest::ModuleDir());
        REQUIRE(real.PathsGet() == std::vector<std::string>{seedtest::ModuleDir()});
    }
}

TEST_CASE("LibraryLoader loads libraries and functions correctly", "[LibraryLoader]") {
    SECTION("FunctionGet throws LoadError when library cannot be found") {
        the_seed::LibraryLoader loader("NotReal");
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.FunctionGet("any_symbol"), the_seed::LoadError);
    }

    SECTION("The failure is still caught as std::runtime_error") {
        the_seed::LibraryLoader loader("NotReal");
        loader.PathAdd("/nonexistent/path");
        REQUIRE_THROWS_AS(loader.FunctionGet("any_symbol"), std::runtime_error);
    }

    SECTION("The failure is NotFound") {
        the_seed::LibraryLoader loader("NotReal");
        loader.PathAdd("/nonexistent/path");
        try {
            loader.FunctionGet("any_symbol");
            FAIL("expected a LoadError");
        } catch(const the_seed::LoadError &error) {
            REQUIRE(error.ReasonGet() == the_seed::LoadError::Reason::NotFound);
        }
    }

    SECTION("FunctionGet finds a function in a library in a configured location") {
        the_seed::LibraryLoader loader("testmodule");
        loader.PathAdd(seedtest::ModuleDir());
        REQUIRE(loader.FunctionGet("create_component") != nullptr);
    }

    SECTION("The working directory is not searched") {
        seedtest::ScratchDir scratch;
        seedtest::CopyModule(scratch, "testmodule", "cwd", "testmodule");
        seedtest::WorkingDirectoryGuard guard(scratch.Path() / "cwd");
        the_seed::LibraryLoader loader("testmodule");
        REQUIRE_THROWS_AS(loader.FunctionGet("create_component"), the_seed::LoadError);
    }

    SECTION("A name containing a separator or a colon is an invalid name") {
        for(const char *name : {"a/b", "a\\b", "c:x"}) {
            the_seed::LibraryLoader loader(name);
            loader.PathAdd(seedtest::ModuleDir());
            try {
                loader.FunctionGet("create_component");
                FAIL("expected a LoadError for " << name);
            } catch(const the_seed::LoadError &error) {
                REQUIRE(error.ReasonGet() == the_seed::LoadError::Reason::InvalidName);
            }
        }
    }
}

TEST_CASE("LibraryLoader destruction completes and unloads nothing", "[LibraryLoader]") {
    using Creator = void *(*)(void *);
    Creator creator = nullptr;
    {
        auto loader = std::make_unique<the_seed::LibraryLoader>("testmodule");
        loader->PathAdd(seedtest::ModuleDir());
        creator = reinterpret_cast<Creator>(loader->FunctionGet("create_component"));
        REQUIRE(creator != nullptr);
        REQUIRE_NOTHROW(loader.reset());
    }
    // The code is still mapped: the function can be called after the loader is gone.
    ecs::Component *component = reinterpret_cast<ecs::Component *(*)(void *)>(creator)(nullptr);
    REQUIRE(component != nullptr);
    delete component;
}
