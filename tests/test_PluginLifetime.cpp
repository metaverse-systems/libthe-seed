#include "LoaderTestSupport.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

// Plugin objects must stay valid and destructible whatever order their loaders, containers and
// managers are destroyed in. Every case runs in its own test case so that a crash names it.

namespace
{
    using seedtest::CounterAddress;
    using seedtest::DestructionCounter;

    nlohmann::json ComponentConfig(DestructionCounter &counter, int value = 1)
    {
        nlohmann::json config;
        config["value"] = value;
        config["counter"] = CounterAddress(counter);
        return config;
    }

    std::string SceneText(const std::vector<std::string> &handles, DestructionCounter &counter)
    {
        nlohmann::json scene;
        scene["entities"] = nlohmann::json::array();
        for(const auto &handle : handles)
        {
            nlohmann::json entity;
            entity["Handle"] = handle;
            entity["Components"]["testmodule"] = ComponentConfig(counter);
            scene["entities"].push_back(entity);
        }
        return scene.dump();
    }

    std::unique_ptr<ComponentLoader> ComponentLoaderMake()
    {
        auto loader = std::make_unique<ComponentLoader>();
        loader->PathAdd(seedtest::ModuleDir());
        return loader;
    }

    std::unique_ptr<SystemLoader> SystemLoaderMake()
    {
        auto loader = std::make_unique<SystemLoader>();
        loader->PathAdd(seedtest::ModuleDir());
        return loader;
    }
}

TEST_CASE("A component outlives its loader", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    std::unique_ptr<ecs::Component> component;
    {
        auto loader = ComponentLoaderMake();
        nlohmann::json config = ComponentConfig(destroyed, 5);
        component = loader->Create("testmodule", &config);
        REQUIRE(component);
    }
    REQUIRE(component->Export()["value"] == 5);
    REQUIRE(destroyed == 0);
    component.reset();
    REQUIRE(destroyed == 1);
}

TEST_CASE("A system is used and destroyed after its loader", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    std::unique_ptr<ecs::System> system;
    {
        auto loader = SystemLoaderMake();
        system = loader->Create("testsystem", &destroyed);
        REQUIRE(system);
    }
    REQUIRE_NOTHROW(system->UpdateSystem());
    REQUIRE(system->Export().is_object());
    REQUIRE(destroyed == 0);
    system.reset();
    REQUIRE(destroyed == 1);
}

TEST_CASE("A creator from Get works after its loader is destroyed", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    ComponentLoader::ComponentCreator componentCreator = nullptr;
    SystemLoader::SystemCreator systemCreator = nullptr;
    {
        auto components = ComponentLoaderMake();
        auto systems = SystemLoaderMake();
        componentCreator = components->Get("testmodule");
        systemCreator = systems->Get("testsystem");
        REQUIRE(componentCreator != nullptr);
        REQUIRE(systemCreator != nullptr);
    }

    nlohmann::json config = ComponentConfig(destroyed, 9);
    std::unique_ptr<ecs::Component> component(componentCreator(&config));
    std::unique_ptr<ecs::System> system(systemCreator(&destroyed));
    REQUIRE(component);
    REQUIRE(system);
    REQUIRE(component->Export()["value"] == 9);
    REQUIRE_NOTHROW(system->UpdateSystem());
    component.reset();
    system.reset();
    REQUIRE(destroyed == 2);
}

#ifndef _WIN32
TEST_CASE("A creator still names its plugin file after its loader is gone", "[PluginLifetime]")
{
    ComponentLoader::ComponentCreator creator = nullptr;
    {
        auto loader = ComponentLoaderMake();
        creator = loader->Get("testmodule");
        REQUIRE(creator != nullptr);
    }

    Dl_info info{};
    REQUIRE(dladdr(reinterpret_cast<void *>(creator), &info) != 0);
    REQUIRE(info.dli_fname != nullptr);
    REQUIRE(std::filesystem::equivalent(info.dli_fname, seedtest::PluginPath("testmodule")));
}
#endif

TEST_CASE("A scene survives both loaders being destroyed before the container", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    {
        ecs::Manager manager;
        ecs::Container *world = manager.Container("world");
        {
            auto components = ComponentLoaderMake();
            auto sceneLoader = std::make_unique<JSONLoader>(world, *components);
            sceneLoader->StringParse(SceneText({"a", "b", "c"}, destroyed));
            REQUIRE(world->ComponentHas("a", "TestModule"));
            sceneLoader.reset();
            components.reset();
        }
        REQUIRE(destroyed == 0);
        REQUIRE(world->ComponentGet("b", "TestModule")->Export()["value"] == 1);
    }
    REQUIRE(destroyed == 3);
}

TEST_CASE("A system in a container survives its loader", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    {
        ecs::Manager manager;
        ecs::Container *world = manager.Container("world");
        {
            auto loader = SystemLoaderMake();
            REQUIRE(world->System(loader->Create("testsystem", &destroyed)) != nullptr);
        }
        REQUIRE_NOTHROW(world->Update());
        REQUIRE(destroyed == 0);
    }
    REQUIRE(destroyed == 1);
}

TEST_CASE("Two loaders of one plugin with the first destroyed first", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    auto first = ComponentLoaderMake();
    auto second = ComponentLoaderMake();
    nlohmann::json config = ComponentConfig(destroyed);
    auto fromFirst = first->Create("testmodule", &config);
    auto fromSecond = second->Create("testmodule", &config);
    REQUIRE(fromFirst);
    REQUIRE(fromSecond);

    first.reset();
    REQUIRE(fromFirst->Export()["value"] == 1);
    REQUIRE(fromSecond->Export()["value"] == 1);
    fromFirst.reset();
    REQUIRE(destroyed == 1);
    second.reset();
    REQUIRE(fromSecond->Export()["value"] == 1);
    fromSecond.reset();
    REQUIRE(destroyed == 2);
}

TEST_CASE("Two loaders of one plugin with the second destroyed first", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    auto first = ComponentLoaderMake();
    auto second = ComponentLoaderMake();
    nlohmann::json config = ComponentConfig(destroyed);
    auto fromFirst = first->Create("testmodule", &config);
    auto fromSecond = second->Create("testmodule", &config);

    second.reset();
    REQUIRE(fromSecond->Export()["value"] == 1);
    REQUIRE(fromFirst->Export()["value"] == 1);
    first.reset();
    REQUIRE(fromFirst->Export()["value"] == 1);
    fromSecond.reset();
    fromFirst.reset();
    REQUIRE(destroyed == 2);
}

TEST_CASE("Objects are destroyed before their loader", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    auto components = ComponentLoaderMake();
    auto systems = SystemLoaderMake();
    nlohmann::json config = ComponentConfig(destroyed);
    auto component = components->Create("testmodule", &config);
    auto system = systems->Create("testsystem", &destroyed);

    component.reset();
    system.reset();
    REQUIRE(destroyed == 2);
    components.reset();
    systems.reset();
    REQUIRE(destroyed == 2);
}

TEST_CASE("The container is destroyed before the loaders", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    auto components = ComponentLoaderMake();
    auto systems = SystemLoaderMake();
    {
        ecs::Manager manager;
        ecs::Container *world = manager.Container("world");
        JSONLoader sceneLoader(world, *components);
        sceneLoader.StringParse(SceneText({"a", "b"}, destroyed));
        world->System(systems->Create("testsystem", &destroyed));
    }
    REQUIRE(destroyed == 3);
    components.reset();
    systems.reset();
    REQUIRE(destroyed == 3);
}

TEST_CASE("Loaders and objects and the container are destroyed interleaved", "[PluginLifetime]")
{
    DestructionCounter destroyed{0};
    auto components = ComponentLoaderMake();
    auto systems = SystemLoaderMake();
    auto manager = std::make_unique<ecs::Manager>();
    ecs::Container *world = manager->Container("world");
    {
        JSONLoader sceneLoader(world, *components);
        sceneLoader.StringParse(SceneText({"a", "b"}, destroyed));
    }
    world->System(systems->Create("testsystem", &destroyed));
    nlohmann::json config = ComponentConfig(destroyed);
    auto loose = components->Create("testmodule", &config);
    auto looseSystem = systems->Create("testsystem", &destroyed);

    components.reset();
    REQUIRE(loose->Export()["value"] == 1);
    loose.reset();
    REQUIRE(destroyed == 1);
    systems.reset();
    REQUIRE_NOTHROW(looseSystem->UpdateSystem());
    REQUIRE_NOTHROW(world->Update());
    manager.reset();
    REQUIRE(destroyed == 4);
    looseSystem.reset();
    REQUIRE(destroyed == 5);
}
