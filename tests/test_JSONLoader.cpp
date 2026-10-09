#include "TestPaths.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/JSONLoader.hpp>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>
#include <stdexcept>
#include <string>

namespace
{
    /*! The manager is declared before the loader, as in the readme, so the loader is destroyed first
     *  and the components are destroyed after it. Plugin code stays loaded, so this order is safe. */
    struct Scene
    {
        Scene()
        {
            this->loader.PathAdd(seedtest::ModuleDir());
            this->world = this->manager.Container("world");
        }

        std::string text(const std::string &handle, const nlohmann::json &config) const
        {
            nlohmann::json scene;
            scene["entities"] = nlohmann::json::array();
            nlohmann::json entity;
            entity["Handle"] = handle;
            entity["Components"]["testmodule"] = config;
            scene["entities"].push_back(entity);
            return scene.dump();
        }

        ecs::Manager manager;
        ComponentLoader loader;
        ecs::Container *world = nullptr;
    };
}

TEST_CASE("JSONLoader attaches a component made by a module", "[JSONLoader]")
{
    Scene scene;
    JSONLoader loader(scene.world, scene.loader);

    nlohmann::json config;
    config["value"] = 7;
    REQUIRE_NOTHROW(loader.StringParse(scene.text("e1", config)));

    REQUIRE(scene.world->Entities.contains("e1"));
    REQUIRE(scene.world->ComponentHas("e1", "TestModule"));
    REQUIRE(scene.world->Entity("e1")->ComponentHas("TestModule"));

    auto found = scene.world->ComponentGet("e1", "TestModule");
    REQUIRE(found);
    REQUIRE(found->EntityHandle == "e1");
    REQUIRE(found->Export()["value"] == 7);
    REQUIRE(scene.world->Entity("e1")->ComponentGet("TestModule").get() == found.get());
}

TEST_CASE("JSONLoader replaces a second component of the same type", "[JSONLoader]")
{
    Scene scene;
    JSONLoader loader(scene.world, scene.loader);

    nlohmann::json first;
    first["value"] = 1;
    nlohmann::json second;
    second["value"] = 2;
    loader.StringParse(scene.text("e1", first));
    auto before = scene.world->ComponentGet("e1", "TestModule");
    REQUIRE(before);

    loader.StringParse(scene.text("e1", second));
    auto after = scene.world->ComponentGet("e1", "TestModule");
    REQUIRE(after);
    REQUIRE(after.get() != before.get());
    REQUIRE(after->Export()["value"] == 2);
    REQUIRE(scene.world->Components.at("TestModule").size() == 1);
    // The holder of the first one still has a valid object.
    REQUIRE(before->Export()["value"] == 1);
}

TEST_CASE("JSONLoader attaches nothing when the factory returns no component", "[JSONLoader]")
{
    Scene scene;
    JSONLoader loader(scene.world, scene.loader);

    nlohmann::json config;
    config["null"] = true;
    REQUIRE_THROWS_AS(loader.StringParse(scene.text("e1", config)), std::runtime_error);

    // The entity is created before its components are, so it stays; it has no component.
    REQUIRE(scene.world->Entities.contains("e1"));
    REQUIRE_FALSE(scene.world->ComponentHas("e1", "TestModule"));
    REQUIRE_FALSE(scene.world->Components.contains("TestModule"));

    // The loader and the world are still usable.
    nlohmann::json good;
    good["value"] = 3;
    REQUIRE_NOTHROW(loader.StringParse(scene.text("e1", good)));
    REQUIRE(scene.world->ComponentHas("e1", "TestModule"));
}

TEST_CASE("JSONLoader rejects a component with an empty type", "[JSONLoader]")
{
    Scene scene;
    JSONLoader loader(scene.world, scene.loader);

    nlohmann::json config;
    config["type"] = "";
    try
    {
        loader.StringParse(scene.text("e1", config));
        FAIL("no error was thrown");
    }
    catch(const std::runtime_error &e)
    {
        REQUIRE(std::string(e.what()).find("component type is empty") != std::string::npos);
    }

    REQUIRE(scene.world->Entities.contains("e1"));
    REQUIRE_FALSE(scene.world->Components.contains(""));
    REQUIRE_FALSE(scene.world->ComponentHas("e1", "TestModule"));
    REQUIRE_FALSE(scene.world->ComponentHas("e1", ""));
}
