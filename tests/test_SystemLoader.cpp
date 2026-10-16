#include "LoaderTestSupport.hpp"
#include <libecs-cpp/ecs.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>


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

    SECTION("A system made by the loader outlives it and the plugin stays mapped") {
        std::atomic<int> destroyed{0};
        std::unique_ptr<ecs::System> system;
        {
            auto loader = std::make_unique<the_seed::SystemLoader>();
            loader->PathAdd(seedtest::ModuleDir());
            system = loader->Create("testsystem", &destroyed);
            REQUIRE(system);
        }
        system->UpdateSystem();
        CHECK(system->Export()["updates"] == 1);
        CHECK(destroyed.load() == 0);
        system.reset();
        CHECK(destroyed.load() == 1);
    }
}

TEST_CASE("SystemLoader loads a real system plugin", "[SystemLoader]") {
    std::atomic<int> destroyed{0};
    the_seed::SystemLoader loader;
    loader.PathAdd(seedtest::ModuleDir());
    // The plugin must be built before the loader looks for it.
    seedtest::PluginPath("testsystem");

    std::unique_ptr<ecs::System> system = loader.Create("testsystem", &destroyed);
    REQUIRE(system);
    CHECK(system->Handle == "TestSystem");
    CHECK(system->Export()["handle"] == "TestSystem");
    CHECK(system->Export()["updates"] == 0);

    SECTION("a container runs the system") {
        {
            ecs::Manager manager;
            ecs::Container *container = manager.Container("world");
            ecs::System *added = container->System(std::move(system));
            REQUIRE(added != nullptr);
            REQUIRE(container->Systems.count("TestSystem") == 1);
            CHECK(destroyed.load() == 0);

            // The container keeps the system under its handle and hands back the same object.
            CHECK(container->Systems.at("TestSystem").get() == added);
            added->UpdateSystem();
            CHECK(added->Export()["updates"] == 1);
            CHECK(container->Export().dump().find("TestSystem") != std::string::npos);
            CHECK(destroyed.load() == 0);
        }
        // Destroying the container destroys the system the plugin made.
        CHECK(destroyed.load() == 1);
    }

    SECTION("destroying the system runs the plugin's destructor once") {
        CHECK(destroyed.load() == 0);
        system.reset();
        CHECK(destroyed.load() == 1);
    }

    SECTION("a system made without a counter works the same") {
        std::unique_ptr<ecs::System> plain = loader.Create("testsystem");
        REQUIRE(plain);
        CHECK(plain->Handle == "TestSystem");
        plain.reset();
        CHECK(destroyed.load() == 0);
    }
}
