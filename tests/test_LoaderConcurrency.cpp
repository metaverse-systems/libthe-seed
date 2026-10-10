#include "LoaderTestSupport.hpp"
#include "internal/PluginSearch.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static constexpr int NUM_THREADS = 8;

TEST_CASE("ComponentLoader concurrent PathAdd is thread-safe", "[concurrency]") {
    ComponentLoader loader;
    std::barrier sync(NUM_THREADS);

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, i]() {
            sync.arrive_and_wait();
            for (int j = 0; j < 100; ++j) {
                loader.PathAdd("/path/" + std::to_string(i) + "/" + std::to_string(j));
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE(loader.PathsGet().size() == NUM_THREADS * 100);
}

TEST_CASE("SystemLoader concurrent PathAdd is thread-safe", "[concurrency]") {
    SystemLoader loader;
    std::barrier sync(NUM_THREADS);

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, i]() {
            sync.arrive_and_wait();
            for (int j = 0; j < 100; ++j) {
                loader.PathAdd("/path/" + std::to_string(i) + "/" + std::to_string(j));
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE(loader.PathsGet().size() == NUM_THREADS * 100);
}

TEST_CASE("PakLoader concurrent PathAdd is thread-safe", "[concurrency]") {
    PakLoader loader;
    std::barrier sync(NUM_THREADS);

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, i]() {
            sync.arrive_and_wait();
            for (int j = 0; j < 100; ++j) {
                loader.PathAdd("/path/" + std::to_string(i) + "/" + std::to_string(j));
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE(loader.PathsGet().size() == NUM_THREADS * 100);
}

TEST_CASE("ComponentLoader concurrent PathsGet while PathAdd", "[concurrency]") {
    ComponentLoader loader;
    loader.PathAdd("/initial");
    std::barrier sync(NUM_THREADS);
    std::atomic<bool> reader_saw_empty{false};

    std::vector<std::thread> threads;
    // Half writers, half readers
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, &reader_saw_empty, i]() {
            sync.arrive_and_wait();
            if (i % 2 == 0) {
                for (int j = 0; j < 100; ++j)
                    loader.PathAdd("/path/" + std::to_string(i) + "/" + std::to_string(j));
            } else {
                for (int j = 0; j < 100; ++j) {
                    auto paths = loader.PathsGet();
                    if (paths.empty())
                        reader_saw_empty.store(true);
                }
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE_FALSE(reader_saw_empty.load());
    auto final_paths = loader.PathsGet();
    REQUIRE(final_paths.size() >= 1);
    REQUIRE(final_paths[0] == "/initial");
}

TEST_CASE("ComponentLoader concurrent Create for nonexistent libraries", "[concurrency]") {
    ComponentLoader loader;
    loader.PathAdd("/nonexistent");
    std::barrier sync(NUM_THREADS);

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, i]() {
            sync.arrive_and_wait();
            // All threads try to Create — should all throw, no crash/race
            try {
                loader.Create("nonexistent/item_" + std::to_string(i));
            } catch (const std::runtime_error &) {
                // Expected — library not found
            }
        });
    }

    for (auto &t : threads)
        t.join();

    // If we got here without crashing or hanging, concurrency is safe
    REQUIRE(true);
}

TEST_CASE("SystemLoader concurrent Create for nonexistent libraries", "[concurrency]") {
    SystemLoader loader;
    loader.PathAdd("/nonexistent");
    std::barrier sync(NUM_THREADS);

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, i]() {
            sync.arrive_and_wait();
            try {
                loader.Create("nonexistent/item_" + std::to_string(i));
            } catch (const std::runtime_error &) {
                // Expected
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE(true);
}

TEST_CASE("PakLoader concurrent Load for nonexistent paks", "[concurrency]") {
    PakLoader loader;
    loader.PathAdd("/nonexistent");
    std::barrier sync(NUM_THREADS);

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, i]() {
            sync.arrive_and_wait();
            try {
                loader.Load("nonexistent/pak_" + std::to_string(i));
            } catch (const std::runtime_error &) {
                // Expected
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE(true);
}

TEST_CASE("Mixed read/write operations across all loaders", "[concurrency]") {
    ComponentLoader comp;
    SystemLoader sys;
    PakLoader pak;
    std::barrier sync(NUM_THREADS);
    std::atomic<bool> any_empty{false};

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, i]() {
            sync.arrive_and_wait();
            for (int j = 0; j < 50; ++j) {
                auto suffix = std::to_string(i) + "_" + std::to_string(j);
                comp.PathAdd("/comp/" + suffix);
                sys.PathAdd("/sys/" + suffix);
                pak.PathAdd("/pak/" + suffix);

                auto cp = comp.PathsGet();
                auto sp = sys.PathsGet();
                auto pp = pak.PathsGet();

                if (cp.empty() || sp.empty() || pp.empty())
                    any_empty.store(true);
            }
        });
    }

    for (auto &t : threads)
        t.join();

    REQUIRE_FALSE(any_empty.load());
    REQUIRE(comp.PathsGet().size() == NUM_THREADS * 50);
    REQUIRE(sys.PathsGet().size() == NUM_THREADS * 50);
    REQUIRE(pak.PathsGet().size() == NUM_THREADS * 50);
}


// Loads that run at the same time share one load per plugin and loader. The counter of platform
// opens is process-wide, so every case below runs one group of threads at a time and resets the
// counter before the group starts. Threads never use Catch assertions: they record what they saw
// and the test thread checks it after joining them.

namespace
{
    namespace fs = std::filesystem;
    using seedtest::CopyModule;
    using seedtest::ScratchDir;
    using seedtest::WorkingDirectoryGuard;

    constexpr int ROUNDS = 100;

    // Runs `work(index)` on NUM_THREADS threads that all start once the latch opens.
    template <typename Function>
    void RunTogether(Function work)
    {
        std::latch start(NUM_THREADS);
        std::vector<std::thread> threads;
        for(int i = 0; i < NUM_THREADS; ++i)
        {
            threads.emplace_back([&work, &start, i]() {
                start.arrive_and_wait();
                work(i);
            });
        }
        for(auto &thread : threads)
        {
            thread.join();
        }
    }

    // The variant of the test module a component came from, or 0 when it is not usable.
    int VariantOf(ecs::Component *component)
    {
        if(component == nullptr)
        {
            return 0;
        }
        return component->Export().value("variant", 0);
    }
}

TEST_CASE("Concurrent Create of an existing plugin opens it once per loader", "[concurrency]")
{
    const auto begin = std::chrono::steady_clock::now();
    const std::string moduleDir = seedtest::ModuleDir();
    // Make sure both plugins exist before the threads start.
    seedtest::PluginPath("testmodule");
    seedtest::PluginPath("testsystem");

    for(int round = 0; round < ROUNDS; ++round)
    {
        ComponentLoader components;
        SystemLoader systems;
        components.PathAdd(moduleDir);
        systems.PathAdd(moduleDir);

        std::atomic<int> working{0};
        std::atomic<int> broken{0};
        seed::internal::ResetPluginOpenCallCount();

        RunTogether([&](int index) {
            try
            {
                if(index % 2 == 0)
                {
                    std::unique_ptr<ecs::Component> component = components.Create("testmodule");
                    (VariantOf(component.get()) == 1 ? working : broken)++;
                }
                else
                {
                    std::unique_ptr<ecs::System> system = systems.Create("testsystem");
                    (system ? working : broken)++;
                }
            }
            catch(...)
            {
                ++broken;
            }
        });

        REQUIRE(broken.load() == 0);
        REQUIRE(working.load() == NUM_THREADS);
        // One open for the component loader and one for the system loader.
        REQUIRE(seed::internal::PluginOpenCallCount() == 2);
    }

    // Every thread of the group asks the same loader for the same plugin.
    for(int round = 0; round < ROUNDS; ++round)
    {
        ComponentLoader components;
        SystemLoader systems;
        components.PathAdd(moduleDir);
        systems.PathAdd(moduleDir);

        std::atomic<int> working{0};
        std::atomic<int> broken{0};
        seed::internal::ResetPluginOpenCallCount();

        RunTogether([&](int) {
            try
            {
                std::unique_ptr<ecs::Component> component = components.Create("testmodule");
                std::unique_ptr<ecs::System> system = systems.Create("testsystem");
                (VariantOf(component.get()) == 1 && system ? working : broken)++;
            }
            catch(...)
            {
                ++broken;
            }
        });

        REQUIRE(broken.load() == 0);
        REQUIRE(working.load() == NUM_THREADS);
        REQUIRE(seed::internal::PluginOpenCallCount() == 2);
    }

    const auto elapsed = std::chrono::steady_clock::now() - begin;
    REQUIRE(elapsed < std::chrono::seconds(60));
}

TEST_CASE("Concurrent Create of a missing plugin gives every thread the same NotFound", "[concurrency]")
{
    ScratchDir scratch;
    fs::create_directories(scratch.Path() / "empty");
    const std::string empty = (scratch.Path() / "empty").string();

    for(int round = 0; round < 20; ++round)
    {
        ComponentLoader components;
        SystemLoader systems;
        components.PathAdd(empty);
        systems.PathAdd(empty);

        std::mutex mutex;
        std::vector<std::string> componentMessages;
        std::vector<std::string> systemMessages;
        std::atomic<int> wrong{0};

        RunTogether([&](int index) {
            try
            {
                if(index % 2 == 0)
                {
                    components.Create("missingmodule");
                }
                else
                {
                    systems.Create("missingsystem");
                }
                ++wrong;
            }
            catch(const LoadError &error)
            {
                if(error.ReasonGet() != LoadError::Reason::NotFound)
                {
                    ++wrong;
                    return;
                }
                std::lock_guard<std::mutex> lock(mutex);
                (index % 2 == 0 ? componentMessages : systemMessages).push_back(error.what());
            }
            catch(...)
            {
                ++wrong;
            }
        });

        REQUIRE(wrong.load() == 0);
        REQUIRE(componentMessages.size() == NUM_THREADS / 2);
        REQUIRE(systemMessages.size() == NUM_THREADS / 2);
        for(const auto &message : componentMessages)
        {
            REQUIRE(message == componentMessages.front());
        }
        for(const auto &message : systemMessages)
        {
            REQUIRE(message == systemMessages.front());
        }
        REQUIRE(componentMessages.front().find("missingmodule") != std::string::npos);
        REQUIRE(systemMessages.front().find("missingsystem") != std::string::npos);
    }
}

TEST_CASE("Names that resolve to one file share one open", "[concurrency]")
{
    const std::string moduleDir = seedtest::ModuleDir();

    SECTION("one after the other")
    {
        ComponentLoader loader;
        loader.PathAdd(moduleDir);
        seed::internal::ResetPluginOpenCallCount();

        std::unique_ptr<ecs::Component> plain = loader.Create("testmodule");
        std::unique_ptr<ecs::Component> aliased = loader.Create("org/testmodule");

        REQUIRE(VariantOf(plain.get()) == 1);
        REQUIRE(VariantOf(aliased.get()) == 1);
        REQUIRE(loader.Get("testmodule") == loader.Get("org/testmodule"));
        REQUIRE(seed::internal::PluginOpenCallCount() == 1);
    }

    SECTION("systems one after the other")
    {
        SystemLoader loader;
        loader.PathAdd(moduleDir);
        seed::internal::ResetPluginOpenCallCount();

        REQUIRE(loader.Create("testsystem"));
        REQUIRE(loader.Create("org/testsystem"));
        REQUIRE(loader.Get("testsystem") == loader.Get("org/testsystem"));
        REQUIRE(seed::internal::PluginOpenCallCount() == 1);
    }

    SECTION("at the same time")
    {
        for(int round = 0; round < ROUNDS; ++round)
        {
            ComponentLoader loader;
            loader.PathAdd(moduleDir);
            std::atomic<int> working{0};
            std::atomic<int> broken{0};
            seed::internal::ResetPluginOpenCallCount();

            RunTogether([&](int index) {
                try
                {
                    std::unique_ptr<ecs::Component> component =
                        loader.Create(index % 2 == 0 ? "testmodule" : "org/testmodule");
                    (VariantOf(component.get()) == 1 ? working : broken)++;
                }
                catch(...)
                {
                    ++broken;
                }
            });

            REQUIRE(broken.load() == 0);
            REQUIRE(working.load() == NUM_THREADS);
            REQUIRE(seed::internal::PluginOpenCallCount() == 1);
        }
    }
}

TEST_CASE("Names that resolve to different files stay apart", "[concurrency]")
{
    // The scratch tree has the working directory "<scratch>/tree/project", so
    // "../node_modules/<org>/testmodule/src/.libs" is under "<scratch>/tree/node_modules".
    ScratchDir scratch;
    fs::path project = scratch.Path() / "tree" / "project";
    fs::create_directories(project);
    CopyModule(scratch, "testmodule", project / ".." / "node_modules" / "org1" / "testmodule" / "src" / ".libs",
               "testmodule");
    CopyModule(scratch, "testmodulealt", project / ".." / "node_modules" / "org2" / "testmodule" / "src" / ".libs",
               "testmodule");
    WorkingDirectoryGuard guard(project);

    ComponentLoader loader;
    REQUIRE(loader.PathsGet().empty());

    SECTION("one after the other")
    {
        REQUIRE(VariantOf(loader.Create("org1/testmodule").get()) == 1);
        REQUIRE(VariantOf(loader.Create("org2/testmodule").get()) == 2);
        REQUIRE(VariantOf(loader.Create("org1/testmodule").get()) == 1);
        REQUIRE(loader.Get("org1/testmodule") != loader.Get("org2/testmodule"));
    }

    SECTION("at the same time")
    {
        std::atomic<int> wrong{0};
        seed::internal::ResetPluginOpenCallCount();
        RunTogether([&](int index) {
            try
            {
                const bool first = index % 2 == 0;
                std::unique_ptr<ecs::Component> component =
                    loader.Create(first ? "org1/testmodule" : "org2/testmodule");
                if(VariantOf(component.get()) != (first ? 1 : 2))
                {
                    ++wrong;
                }
            }
            catch(...)
            {
                ++wrong;
            }
        });
        REQUIRE(wrong.load() == 0);
        REQUIRE(seed::internal::PluginOpenCallCount() == 2);
    }
}

TEST_CASE("PathAdd while other threads load the same plugin", "[concurrency]")
{
    ScratchDir scratch;
    CopyModule(scratch, "testmodule", "late", "testmodule");
    fs::create_directories(scratch.Path() / "early");
    const std::string early = (scratch.Path() / "early").string();
    const std::string late = (scratch.Path() / "late").string();

    for(int round = 0; round < 20; ++round)
    {
        ComponentLoader loader;
        loader.PathAdd(early);

        std::atomic<bool> added{false};
        std::atomic<int> wrong{0};
        std::atomic<int> sawNewLocation{0};

        RunTogether([&](int index) {
            try
            {
                if(index == 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    loader.PathAdd(late);
                    added.store(true);
                    return;
                }

                // Until the location is added a request may find the plugin or not; either is
                // acceptable, but a failure must be a NotFound and never a crash.
                while(!added.load())
                {
                    try
                    {
                        std::unique_ptr<ecs::Component> component = loader.Create("testmodule");
                        if(VariantOf(component.get()) != 1)
                        {
                            ++wrong;
                        }
                    }
                    catch(const LoadError &error)
                    {
                        if(error.ReasonGet() != LoadError::Reason::NotFound)
                        {
                            ++wrong;
                        }
                    }
                }

                // A request started after PathAdd returned sees the new location.
                std::unique_ptr<ecs::Component> component = loader.Create("testmodule");
                if(VariantOf(component.get()) == 1)
                {
                    ++sawNewLocation;
                }
                else
                {
                    ++wrong;
                }
            }
            catch(...)
            {
                ++wrong;
            }
        });

        REQUIRE(wrong.load() == 0);
        REQUIRE(sawNewLocation.load() == NUM_THREADS - 1);
        const std::vector<std::string> paths = loader.PathsGet();
        REQUIRE(paths.size() == 2);
        REQUIRE(paths.back() == late);
    }
}
