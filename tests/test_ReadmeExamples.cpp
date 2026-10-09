#include "LoaderTestSupport.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libecs-cpp/ecs.hpp>
#include <iostream>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Each loader example of README.md is copied verbatim into a function below, so a change to the
// public API that breaks an example breaks this file's build. The examples name plugins that do
// not exist ("org/my-component"), so running one ends either normally or with a LoadError; any
// other outcome, and any crash, fails the test. The thread example would end a thread with an
// uncaught LoadError, so it is compiled but not run.

namespace
{
    void ComponentLoaderExample()
    {
        int init_data = 0;
        // --- begin README text
        ComponentLoader loader;
        loader.PathAdd("/usr/lib/the-seed/components");

        auto component = loader.Create("org/my-component");
        // Returns std::unique_ptr<ecs::Component>

        // Pass initialization data to the component factory
        auto component2 = loader.Create("org/my-component", &init_data);

        // Get the raw factory function pointer
        auto creator = loader.Get("org/my-component");
        // --- end README text
    }

    void SystemLoaderExample()
    {
        // --- begin README text
        SystemLoader loader;
        loader.PathAdd("/usr/lib/the-seed/systems");

        auto system = loader.Create("org/my-system");
        // Returns std::unique_ptr<ecs::System>
        // --- end README text
    }

    void DevelopmentLocationsExample()
    {
        // --- begin README text
        ComponentLoader loader;
        loader.PathAdd("/usr/lib/the-seed/components");
#ifdef SEED_DEVELOPMENT
        loader.DevelopmentPathsEnable();
#endif

        for(const auto &location : loader.SearchPathsGet("org/my-component"))
        {
            std::cout << location << "\n";
        }
        // --- end README text
    }

    void PakLoaderExample()
    {
        // --- begin README text
        PakLoader loader;
        loader.PathAdd("/usr/share/the-seed/paks");

        // Load all resources from a pak
        auto resources = loader.Load("org/my-pak");
        // Returns std::unordered_map<std::string, std::shared_ptr<ecs::Resource>>

        // Load only specific resources
        auto filtered = loader.Load("org/my-pak", {"texture1", "mesh2"});
        // --- end README text
    }

    void JSONLoaderExample()
    {
        // --- begin README text
        ecs::Manager manager;
        ecs::Container *container = manager.Container("world");
        ComponentLoader comp_loader;
        comp_loader.PathAdd("/usr/lib/the-seed/components");

        JSONLoader json(container, comp_loader);
        json.FileParse("scene.json");
        // The container, json and comp_loader may be destroyed in any order.
        // Or parse from a string:
        // json.StringParse(json_string);
        // --- end README text
    }

    [[maybe_unused]] void ThreadSafetyExample()
    {
        // --- begin README text
        ComponentLoader loader;
        loader.PathAdd("/usr/lib/the-seed/components");

        std::thread t1([&]() { auto c = loader.Create("org/comp-a"); });
        std::thread t2([&]() { auto c = loader.Create("org/comp-b"); });
        t1.join();
        t2.join();
        // --- end README text
    }

    void ErrorHandlingExample()
    {
        ComponentLoader loader;
        // --- begin README text
        try {
            auto component = loader.Create("nonexistent/component");
        } catch (const LoadError &e) {
            if(e.ReasonGet() == LoadError::Reason::NotFound)
            {
                // e.what() lists every location that was searched
            }
        }
        // --- end README text
    }

    // Runs an example. A LoadError is the expected end of an example that names a plugin that
    // is not installed.
    void Run(const std::function<void()> &example)
    {
        try
        {
            example();
        }
        catch(const LoadError &)
        {
        }
    }
}

TEST_CASE("README examples run in the order written without a crash", "[README]")
{
    Run(ComponentLoaderExample);
    Run(SystemLoaderExample);
    Run(DevelopmentLocationsExample);
    Run(PakLoaderExample);
    Run(JSONLoaderExample);
    Run(ErrorHandlingExample);
}

TEST_CASE("README not-found text for a loader with no locations", "[README]")
{
    ComponentLoader loader;
    try
    {
        loader.Create("org/my-component");
        FAIL("expected a LoadError");
    }
    catch(const LoadError &e)
    {
        const std::string text = e.what();
        REQUIRE(text.find("component plugin \"org/my-component\" not found: no locations are configured") == 0);
        REQUIRE(text.find("development locations are off for this loader") != std::string::npos);
    }
}
