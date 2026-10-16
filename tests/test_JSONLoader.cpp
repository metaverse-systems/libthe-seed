#include "LoaderTestSupport.hpp"
#include "TestPaths.hpp"
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/LibraryLoader.hpp>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <set>
#include <string>
#include <vector>

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
        the_seed::ComponentLoader loader;
        ecs::Container *world = nullptr;
    };
}

TEST_CASE("JSONLoader attaches a component made by a module", "[JSONLoader]")
{
    Scene scene;
    the_seed::JSONLoader loader(scene.world, scene.loader);

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
    the_seed::JSONLoader loader(scene.world, scene.loader);

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
    the_seed::JSONLoader loader(scene.world, scene.loader);

    nlohmann::json config;
    config["null"] = true;
    REQUIRE_THROWS_AS(loader.StringParse(scene.text("e1", config)), std::runtime_error);

    // A scene is all or nothing: the entity is not left behind.
    REQUIRE_FALSE(scene.world->Entities.contains("e1"));
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
    the_seed::JSONLoader loader(scene.world, scene.loader);

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

    REQUIRE_FALSE(scene.world->Entities.contains("e1"));
    REQUIRE_FALSE(scene.world->Components.contains(""));
    REQUIRE_FALSE(scene.world->ComponentHas("e1", "TestModule"));
    REQUIRE_FALSE(scene.world->ComponentHas("e1", ""));
}

namespace
{
    std::size_t ComponentsCount(const ecs::Container *world)
    {
        std::size_t total = 0;
        for(const auto &[type, entries] : world->Components)
        {
            total += entries.size();
        }
        return total;
    }

    /*! The state of a container that must not change when a scene fails. */
    struct Counts
    {
        explicit Counts(const ecs::Container *world)
            : entities(world->Entities.size()), components(ComponentsCount(world))
        {
        }

        bool operator==(const Counts &other) const
        {
            return this->entities == other.entities && this->components == other.components;
        }

        std::size_t entities;
        std::size_t components;
    };

    struct Failure
    {
        the_seed::LoadError::Reason reason;
        std::string message;
    };

    bool Contains(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }

    /*! Runs a scene text and returns the LoadError, after checking that the container did not change.
     *  The container already holds an entity with a component. */
    template <typename Run>
    Failure Fails(Scene &scene, Run run)
    {
        nlohmann::json config;
        config["value"] = 1;
        the_seed::JSONLoader existing(scene.world, scene.loader);
        existing.StringParse(scene.text("existing", config));
        const Counts before(scene.world);
        REQUIRE(before.entities == 1);
        REQUIRE(before.components == 1);

        Failure failure{the_seed::LoadError::Reason::NotFound, ""};
        try
        {
            run();
            FAIL("no error was thrown");
        }
        catch(const the_seed::LoadError &e)
        {
            failure.reason = e.ReasonGet();
            failure.message = e.what();
        }
        REQUIRE(Counts(scene.world) == before);
        REQUIRE(scene.world->Entities.contains("existing"));
        return failure;
    }

    Failure TextFails(Scene &scene, const std::string &text)
    {
        the_seed::JSONLoader loader(scene.world, scene.loader);
        return Fails(scene, [&] { loader.StringParse(text); });
    }

    std::string EntityScene(const nlohmann::json &entity)
    {
        nlohmann::json scene;
        scene["entities"] = nlohmann::json::array();
        scene["entities"].push_back(entity);
        return scene.dump();
    }
}

TEST_CASE("JSONLoader reports a missing scene file as unopenable", "[JSONLoader]")
{
    Scene scene;
    seedtest::ScratchDir scratch;
    the_seed::JSONLoader loader(scene.world, scene.loader);
    const std::string file = scratch.File("absent.json");

    Failure failure = Fails(scene, [&] { loader.FileParse(file); });
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneUnopenable);
    REQUIRE(Contains(failure.message, "could not be opened"));
    REQUIRE(Contains(failure.message, file));
    REQUIRE(Contains(failure.message, "No such file or directory"));
    REQUIRE_FALSE(Contains(failure.message, "parse"));
}

TEST_CASE("JSONLoader reports a directory as unopenable", "[JSONLoader]")
{
    Scene scene;
    seedtest::ScratchDir scratch;
    the_seed::JSONLoader loader(scene.world, scene.loader);

    Failure failure = Fails(scene, [&] { loader.FileParse(scratch.Path().string()); });
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneUnopenable);
    REQUIRE(Contains(failure.message, "it is a directory"));
    REQUIRE(Contains(failure.message, scratch.Path().string()));
    REQUIRE_FALSE(Contains(failure.message, "parse"));
}

TEST_CASE("JSONLoader reports an unreadable scene file as unopenable", "[JSONLoader]")
{
#ifdef _WIN32
    std::cerr << "SKIPPED: unreadable scene file case, file permissions are not modelled by chmod on this platform\n";
    return;
#else
    if(geteuid() == 0)
    {
        std::cerr << "SKIPPED: unreadable scene file case, running as root\n";
        return;
    }

    Scene scene;
    seedtest::ScratchDir scratch;
    the_seed::JSONLoader loader(scene.world, scene.loader);
    const std::string file = scratch.File("locked.json");
    {
        std::ofstream out(file);
        out << R"({"entities": []})";
    }
    REQUIRE(chmod(file.c_str(), 0) == 0);

    Failure failure = Fails(scene, [&] { loader.FileParse(file); });
    chmod(file.c_str(), 0600);
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneUnopenable);
    REQUIRE(Contains(failure.message, "Permission denied"));
    REQUIRE(Contains(failure.message, file));
    REQUIRE_FALSE(Contains(failure.message, "parse"));
#endif
}

TEST_CASE("JSONLoader reports an empty file and a syntax error as not understood", "[JSONLoader]")
{
    Scene scene;
    seedtest::ScratchDir scratch;
    the_seed::JSONLoader loader(scene.world, scene.loader);

    const std::string empty = scratch.File("empty.json");
    { std::ofstream out(empty); }
    Failure failure = Fails(scene, [&] { loader.FileParse(empty); });
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
    REQUIRE(Contains(failure.message, "could not be understood"));
    REQUIRE(Contains(failure.message, empty));

    const std::string broken = scratch.File("broken.json");
    {
        std::ofstream out(broken);
        out << "{\"entities\": [\n  {\"Handle\": \"a\",,}\n]}";
    }
    failure = Fails(scene, [&] { loader.FileParse(broken); });
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
    REQUIRE(Contains(failure.message, broken));
    REQUIRE(Contains(failure.message, "line 2"));
    REQUIRE(Contains(failure.message, "column"));

    failure = TextFails(scene, "{\"entities\": [");
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
    REQUIRE(Contains(failure.message, "line 1"));
    REQUIRE(Contains(failure.message, "column"));
}

TEST_CASE("JSONLoader names the scene text for a string", "[JSONLoader]")
{
    Scene scene;
    Failure failure = TextFails(scene, "not json");
    REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
    REQUIRE(Contains(failure.message, "scene text"));
}

TEST_CASE("JSONLoader reports structure errors as not understood", "[JSONLoader]")
{
    Scene scene;

    SECTION("entities is missing")
    {
        Failure failure = TextFails(scene, "{}");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entities"));
    }

    SECTION("entities is not an array")
    {
        Failure failure = TextFails(scene, R"({"entities": {"a": 1}})");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "\"entities\" must be an array"));
    }

    SECTION("an entity is not an object")
    {
        Failure failure = TextFails(scene, R"({"entities": [{"Handle": "a", "Components": {}}, 5]})");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entity 1"));
    }

    SECTION("Handle is missing")
    {
        Failure failure = TextFails(scene, R"({"entities": [{"Components": {}}]})");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entity 0"));
        REQUIRE(Contains(failure.message, "\"Handle\" must be a non-empty string"));
    }

    SECTION("Handle is empty")
    {
        Failure failure = TextFails(scene, R"({"entities": [{"Handle": "", "Components": {}}]})");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entity 0"));
        REQUIRE(Contains(failure.message, "\"Handle\" must be a non-empty string"));
    }

    SECTION("Components is missing")
    {
        Failure failure = TextFails(scene, R"({"entities": [{"Handle": "a"}]})");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entity 0"));
        REQUIRE(Contains(failure.message, "\"a\""));
        REQUIRE(Contains(failure.message, "Components"));
    }

    SECTION("Components is not an object")
    {
        Failure failure = TextFails(scene, R"({"entities": [{"Handle": "a", "Components": []}]})");
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entity 0"));
        REQUIRE(Contains(failure.message, "\"a\""));
        REQUIRE(Contains(failure.message, "Components"));
    }

    SECTION("a later entity is wrong, so an earlier good one is not added")
    {
        nlohmann::json config;
        config["value"] = 1;
        nlohmann::json good;
        good["Handle"] = "good";
        good["Components"]["testmodule"] = config;
        nlohmann::json scenejson;
        scenejson["entities"] = nlohmann::json::array({good, nlohmann::json::object({{"Handle", "bad"}})});
        Failure failure = TextFails(scene, scenejson.dump());
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneNotUnderstood);
        REQUIRE(Contains(failure.message, "entity 1"));
        REQUIRE_FALSE(scene.world->Entities.contains("good"));
    }
}

TEST_CASE("JSONLoader reports a plugin failure with the entity and the component type", "[JSONLoader]")
{
    Scene scene;

    SECTION("a plugin that cannot be found")
    {
        nlohmann::json entity;
        entity["Handle"] = "hero";
        entity["Components"]["nosuchplugin"] = nlohmann::json::object();
        Failure failure = TextFails(scene, EntityScene(entity));
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneComponentFailed);
        REQUIRE(Contains(failure.message, "hero"));
        REQUIRE(Contains(failure.message, "nosuchplugin"));
        REQUIRE(Contains(failure.message, "not found"));
    }

    SECTION("a plugin that makes no object")
    {
        nlohmann::json config;
        config["null"] = true;
        Failure failure = TextFails(scene, scene.text("hero", config));
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneComponentFailed);
        REQUIRE(Contains(failure.message, "hero"));
        REQUIRE(Contains(failure.message, "testmodule"));
        REQUIRE(Contains(failure.message, "produced no object"));
    }

    SECTION("a component with an empty type")
    {
        nlohmann::json config;
        config["type"] = "";
        Failure failure = TextFails(scene, scene.text("hero", config));
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneComponentFailed);
        REQUIRE(Contains(failure.message, "hero"));
        REQUIRE(Contains(failure.message, "testmodule"));
        REQUIRE(Contains(failure.message, "component type is empty"));
    }

    SECTION("a failure in a later entity leaves the earlier ones out")
    {
        nlohmann::json config;
        config["value"] = 1;
        nlohmann::json good;
        good["Handle"] = "good";
        good["Components"]["testmodule"] = config;
        nlohmann::json bad;
        bad["Handle"] = "bad";
        bad["Components"]["nosuchplugin"] = nlohmann::json::object();
        nlohmann::json scenejson;
        scenejson["entities"] = nlohmann::json::array({good, bad});
        Failure failure = TextFails(scene, scenejson.dump());
        REQUIRE(failure.reason == the_seed::LoadError::Reason::SceneComponentFailed);
        REQUIRE(Contains(failure.message, "bad"));
        REQUIRE_FALSE(scene.world->Entities.contains("good"));
    }
}

TEST_CASE("JSONLoader accepts an explicit empty entity list and adds nothing", "[JSONLoader]")
{
    Scene scene;
    the_seed::JSONLoader loader(scene.world, scene.loader);
    const Counts before(scene.world);

    REQUIRE_NOTHROW(loader.StringParse(R"({"entities": []})"));
    REQUIRE(Counts(scene.world) == before);
}

TEST_CASE("JSONLoader loads a good scene file", "[JSONLoader]")
{
    Scene scene;
    seedtest::ScratchDir scratch;
    the_seed::JSONLoader loader(scene.world, scene.loader);
    nlohmann::json config;
    config["value"] = 5;
    const std::string file = scratch.File("good.json");
    {
        std::ofstream out(file);
        out << scene.text("e1", config);
    }

    REQUIRE_NOTHROW(loader.FileParse(file));
    REQUIRE(scene.world->ComponentGet("e1", "TestModule")->Export()["value"] == 5);
}

namespace
{
    /*! A container that already holds several entities with components, and the loader that made them. */
    struct Populated
    {
        Populated()
        {
            the_seed::JSONLoader existing(this->scene.world, this->scene.loader);
            nlohmann::json document;
            document["entities"] = nlohmann::json::array();
            for(int index = 0; index < 3; index++)
            {
                nlohmann::json entity;
                entity["Handle"] = "kept" + std::to_string(index);
                entity["Components"]["testmodule"]["value"] = 10 + index;
                document["entities"].push_back(entity);
            }
            existing.StringParse(document.dump());
            this->before = this->scene.world->Export();
        }

        /*! Runs the action, which must fail with the reason, and checks the whole container is unchanged. */
        Failure Fails(the_seed::LoadError::Reason reason, const std::function<void()> &action)
        {
            Failure failure{the_seed::LoadError::Reason::NotFound, ""};
            try
            {
                action();
                FAIL("no error was thrown");
            }
            catch(const the_seed::LoadError &e)
            {
                failure.reason = e.ReasonGet();
                failure.message = e.what();
            }
            REQUIRE(failure.reason == reason);
            REQUIRE(this->scene.world->Export() == this->before);
            REQUIRE(this->scene.world->Entities.size() == 3);
            return failure;
        }

        Scene scene;
        nlohmann::json before;
    };

    /*! A scene of entities, each with a testmodule component and, for the entities named, a component
     *  that no plugin provides. */
    std::string SceneOf(const std::vector<std::string> &handles, const std::set<std::string> &broken)
    {
        nlohmann::json document;
        document["entities"] = nlohmann::json::array();
        int value = 0;
        for(const std::string &handle : handles)
        {
            nlohmann::json entity;
            entity["Handle"] = handle;
            entity["Components"]["testmodule"]["value"] = ++value;
            if(broken.count(handle) != 0)
            {
                entity["Components"]["missingplugin"] = nlohmann::json::object();
            }
            document["entities"].push_back(entity);
        }
        return document.dump();
    }
}

TEST_CASE("JSONLoader leaves a populated container unchanged when each stage fails", "[JSONLoader]")
{
    Populated world;
    REQUIRE(world.before.dump().find("kept2") != std::string::npos);
    the_seed::JSONLoader loader(world.scene.world, world.scene.loader);
    seedtest::ScratchDir scratch;

    SECTION("the scene file cannot be opened")
    {
        const std::string file = scratch.File("absent.json");
        Failure failure = world.Fails(the_seed::LoadError::Reason::SceneUnopenable, [&] { loader.FileParse(file); });
        REQUIRE(Contains(failure.message, file));
    }

    SECTION("the scene is not understood")
    {
        Failure failure = world.Fails(the_seed::LoadError::Reason::SceneNotUnderstood,
                                      [&] { loader.StringParse(R"({"entities": [{"Handle": "a"}]})"); });
        REQUIRE(Contains(failure.message, "Components"));
    }

    SECTION("a component cannot be made on the first entity")
    {
        Failure failure = world.Fails(the_seed::LoadError::Reason::SceneComponentFailed, [&] {
            loader.StringParse(SceneOf({"first", "second", "third"}, {"first"}));
        });
        REQUIRE(Contains(failure.message, "first"));
        REQUIRE(Contains(failure.message, "missingplugin"));
        REQUIRE_FALSE(world.scene.world->Entities.contains("first"));
        REQUIRE_FALSE(world.scene.world->Entities.contains("second"));
    }

    SECTION("a component cannot be made on the last of several entities")
    {
        Failure failure = world.Fails(the_seed::LoadError::Reason::SceneComponentFailed, [&] {
            loader.StringParse(SceneOf({"first", "second", "third", "last"}, {"last"}));
        });
        REQUIRE(Contains(failure.message, "\"last\""));
        REQUIRE(Contains(failure.message, "missingplugin"));
        REQUIRE(Contains(failure.message, "not found"));
        REQUIRE_FALSE(world.scene.world->Entities.contains("first"));
        REQUIRE_FALSE(world.scene.world->Entities.contains("third"));
    }
}

TEST_CASE("JSONLoader adds every entity and component of a valid scene to a populated container", "[JSONLoader]")
{
    Populated world;
    the_seed::JSONLoader loader(world.scene.world, world.scene.loader);

    REQUIRE_NOTHROW(loader.StringParse(SceneOf({"one", "two", "three"}, {})));

    REQUIRE(world.scene.world->Entities.size() == 6);
    for(int index = 0; index < 3; index++)
    {
        REQUIRE(world.scene.world->ComponentGet("kept" + std::to_string(index), "TestModule")->Export()["value"] ==
                10 + index);
    }
    int value = 0;
    for(const std::string handle : {"one", "two", "three"})
    {
        REQUIRE(world.scene.world->Entities.contains(handle));
        REQUIRE(world.scene.world->ComponentHas(handle, "TestModule"));
        REQUIRE(world.scene.world->ComponentGet(handle, "TestModule")->Export()["value"] == ++value);
    }
    REQUIRE(ComponentsCount(world.scene.world) == 6);
}

/*  The document a JSONLoader keeps is private and has no accessor, so the configuration it hands to a
 *  plugin is checked through lifetime. libtestmodule records the pointer each create_component call
 *  receives. If the loader copied each entity's description into a temporary, the pointer would lead into
 *  that temporary, destroyed at the end of its loop iteration. The address-sanitizer run reports reading
 *  it as a heap-use-after-free; a plain run may also show a wrong value but is not relied on. */
TEST_CASE("JSONLoader hands plugins configurations that stay alive with the loader", "[JSONLoader]")
{
    using CountFn = std::size_t (*)();
    using AtFn = const void *(*)(std::size_t);
    using ResetFn = void (*)();

    the_seed::LibraryLoader module("testmodule");
    module.PathAdd(seedtest::ModuleDir());
    auto count = reinterpret_cast<CountFn>(module.FunctionGet("test_module_recorded_count"));
    auto at = reinterpret_cast<AtFn>(module.FunctionGet("test_module_recorded_at"));
    auto reset = reinterpret_cast<ResetFn>(module.FunctionGet("test_module_recorded_reset"));
    REQUIRE(count != nullptr);
    REQUIRE(at != nullptr);
    REQUIRE(reset != nullptr);

    Scene scene;
    the_seed::JSONLoader loader(scene.world, scene.loader);

    nlohmann::json document;
    document["entities"] = nlohmann::json::array();
    std::vector<nlohmann::json> configs;
    for(int index = 0; index < 5; index++)
    {
        nlohmann::json config;
        config["value"] = 100 + index;
        config["label"] = "configuration number " + std::to_string(index) + std::string(40, 'x');
        config["list"] = nlohmann::json::array({index, index + 1, index + 2});
        configs.push_back(config);
        nlohmann::json entity;
        entity["Handle"] = "entity" + std::to_string(index);
        entity["Components"]["testmodule"] = config;
        document["entities"].push_back(entity);
    }

    reset();
    REQUIRE_NOTHROW(loader.StringParse(document.dump()));

    REQUIRE(count() == configs.size());
    std::set<const void *> distinct;
    for(std::size_t index = 0; index < configs.size(); index++)
    {
        const void *pointer = at(index);
        REQUIRE(pointer != nullptr);
        distinct.insert(pointer);
        // The loader is still alive, so the value must be too, and equal to the scene text.
        const auto *value = static_cast<const nlohmann::json *>(pointer);
        REQUIRE(*value == configs[index]);
    }
    REQUIRE(distinct.size() == configs.size());
    reset();
}
