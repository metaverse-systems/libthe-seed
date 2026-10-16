#include "LoaderTestSupport.hpp"
#include "internal/PluginSearch.hpp"
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <algorithm>
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

namespace
{
    // Checks that `paths` holds exactly the entries "<prefix><thread><separator><index>" for every
    // thread below `threads` and every index below `perThread`, each once, and that for each thread
    // the entries appear in the order that thread added them. Extra leading entries (`skip`) are
    // not looked at.
    void CheckEveryPathOnce(const std::vector<std::string> &paths, const std::string &prefix,
                            const std::string &separator, int threads, int perThread, std::size_t skip = 0)
    {
        REQUIRE(paths.size() == skip + static_cast<std::size_t>(threads) * static_cast<std::size_t>(perThread));
        std::vector<std::vector<int>> seen(static_cast<std::size_t>(threads));
        for(std::size_t at = skip; at < paths.size(); ++at)
        {
            const std::string &path = paths[at];
            INFO("path " << path);
            REQUIRE(path.compare(0, prefix.size(), prefix) == 0);
            const std::size_t cut = path.find(separator, prefix.size());
            REQUIRE(cut != std::string::npos);
            const int thread = std::stoi(path.substr(prefix.size(), cut - prefix.size()));
            const int index = std::stoi(path.substr(cut + separator.size()));
            REQUIRE(thread >= 0);
            REQUIRE(thread < threads);
            seen[static_cast<std::size_t>(thread)].push_back(index);
        }
        for(int thread = 0; thread < threads; ++thread)
        {
            INFO("thread " << thread);
            const std::vector<int> &indexes = seen[static_cast<std::size_t>(thread)];
            REQUIRE(indexes.size() == static_cast<std::size_t>(perThread));
            for(int index = 0; index < perThread; ++index)
            {
                // Present once, and after the entry the same thread added before it.
                REQUIRE(indexes[static_cast<std::size_t>(index)] == index);
            }
        }
    }
}

TEST_CASE("ComponentLoader concurrent PathAdd is thread-safe", "[concurrency]") {
    the_seed::ComponentLoader loader;
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

    CheckEveryPathOnce(loader.PathsGet(), "/path/", "/", NUM_THREADS, 100);
}

TEST_CASE("SystemLoader concurrent PathAdd is thread-safe", "[concurrency]") {
    the_seed::SystemLoader loader;
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

    CheckEveryPathOnce(loader.PathsGet(), "/path/", "/", NUM_THREADS, 100);
}

TEST_CASE("PakLoader concurrent PathAdd is thread-safe", "[concurrency]") {
    the_seed::PakLoader loader;
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

    CheckEveryPathOnce(loader.PathsGet(), "/path/", "/", NUM_THREADS, 100);
}

TEST_CASE("ComponentLoader concurrent PathsGet while PathAdd", "[concurrency]") {
    the_seed::ComponentLoader loader;
    loader.PathAdd("/initial");
    std::barrier sync(NUM_THREADS);
    std::atomic<bool> reader_saw_empty{false};
    // What each reader saw on each call. The threads record; the test thread checks.
    std::vector<std::vector<std::vector<std::string>>> snapshots(NUM_THREADS);

    std::vector<std::thread> threads;
    // Half writers, half readers
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&loader, &sync, &reader_saw_empty, &snapshots, i]() {
            sync.arrive_and_wait();
            if (i % 2 == 0) {
                for (int j = 0; j < 100; ++j)
                    loader.PathAdd("/path/" + std::to_string(i) + "/" + std::to_string(j));
            } else {
                for (int j = 0; j < 100; ++j) {
                    auto paths = loader.PathsGet();
                    if (paths.empty())
                        reader_saw_empty.store(true);
                    snapshots[static_cast<std::size_t>(i)].push_back(std::move(paths));
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

    // The four writers (even indexes) each added 100 paths, every one once and in order.
    std::vector<std::string> added(final_paths.begin() + 1, final_paths.end());
    REQUIRE(added.size() == static_cast<std::size_t>(NUM_THREADS / 2) * 100);
    std::vector<std::vector<int>> seen(NUM_THREADS);
    for (const auto &path : added) {
        INFO("path " << path);
        REQUIRE(path.compare(0, 6, "/path/") == 0);
        const std::size_t cut = path.find('/', 6);
        REQUIRE(cut != std::string::npos);
        const int writer = std::stoi(path.substr(6, cut - 6));
        REQUIRE(writer >= 0);
        REQUIRE(writer < NUM_THREADS);
        REQUIRE(writer % 2 == 0);
        seen[static_cast<std::size_t>(writer)].push_back(std::stoi(path.substr(cut + 1)));
    }
    for (int writer = 0; writer < NUM_THREADS; writer += 2) {
        INFO("writer " << writer);
        REQUIRE(seen[static_cast<std::size_t>(writer)].size() == 100);
        for (int j = 0; j < 100; ++j)
            REQUIRE(seen[static_cast<std::size_t>(writer)][static_cast<std::size_t>(j)] == j);
    }

    // Every list a reader got is the start of the final list: paths are only ever appended.
    for (int reader = 1; reader < NUM_THREADS; reader += 2) {
        const auto &list = snapshots[static_cast<std::size_t>(reader)];
        REQUIRE(list.size() == 100);
        std::size_t previous = 0;
        for (const auto &snapshot : list) {
            REQUIRE(snapshot.size() >= previous);
            REQUIRE(snapshot.size() <= final_paths.size());
            REQUIRE(std::equal(snapshot.begin(), snapshot.end(), final_paths.begin()));
            previous = snapshot.size();
        }
    }
}

namespace
{
    // The outcome of one call that is expected to throw a LoadError.
    struct Failure
    {
        bool threwLoadError = false;
        the_seed::LoadError::Reason reason = the_seed::LoadError::Reason::InvalidName;
        std::string message;
    };

    // Runs `call(item)` on NUM_THREADS threads at once, where item is "<prefix><thread>", and
    // returns what each thread saw. Nothing is asserted inside the threads.
    template <typename Call>
    std::vector<Failure> FailTogether(const std::string &prefix, Call call)
    {
        std::barrier sync(NUM_THREADS);
        std::vector<Failure> failures(NUM_THREADS);
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&, i]() {
                sync.arrive_and_wait();
                try {
                    call(prefix + std::to_string(i));
                } catch (const the_seed::LoadError &error) {
                    failures[static_cast<std::size_t>(i)] = {true, error.ReasonGet(), error.what()};
                } catch (...) {
                }
            });
        }
        for (auto &t : threads)
            t.join();
        return failures;
    }

    void CheckEveryCallFailed(const std::vector<Failure> &failures, const std::string &prefix)
    {
        REQUIRE(failures.size() == static_cast<std::size_t>(NUM_THREADS));
        for (int i = 0; i < NUM_THREADS; ++i) {
            const Failure &failure = failures[static_cast<std::size_t>(i)];
            INFO("thread " << i);
            REQUIRE(failure.threwLoadError);
            REQUIRE(failure.reason == the_seed::LoadError::Reason::NotFound);
            REQUIRE(failure.message.find(prefix + std::to_string(i)) != std::string::npos);
        }
    }
}

TEST_CASE("ComponentLoader concurrent Create for nonexistent libraries", "[concurrency]") {
    the_seed::ComponentLoader loader;
    loader.PathAdd("/nonexistent");

    // All threads try to Create at once: every call must fail with NotFound naming its item.
    auto failures = FailTogether("nonexistent/item_", [&loader](const std::string &item) { loader.Create(item); });
    CheckEveryCallFailed(failures, "nonexistent/item_");
}

TEST_CASE("SystemLoader concurrent Create for nonexistent libraries", "[concurrency]") {
    the_seed::SystemLoader loader;
    loader.PathAdd("/nonexistent");

    auto failures = FailTogether("nonexistent/item_", [&loader](const std::string &item) { loader.Create(item); });
    CheckEveryCallFailed(failures, "nonexistent/item_");
}

TEST_CASE("PakLoader concurrent Load for nonexistent paks", "[concurrency]") {
    the_seed::PakLoader loader;
    loader.PathAdd("/nonexistent");

    auto failures = FailTogether("nonexistent/pak_", [&loader](const std::string &item) { loader.Load(item); });
    CheckEveryCallFailed(failures, "nonexistent/pak_");
}

TEST_CASE("Mixed read/write operations across all loaders", "[concurrency]") {
    the_seed::ComponentLoader comp;
    the_seed::SystemLoader sys;
    the_seed::PakLoader pak;
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
    CheckEveryPathOnce(comp.PathsGet(), "/comp/", "_", NUM_THREADS, 50);
    CheckEveryPathOnce(sys.PathsGet(), "/sys/", "_", NUM_THREADS, 50);
    CheckEveryPathOnce(pak.PathsGet(), "/pak/", "_", NUM_THREADS, 50);
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

    // True when the system is the one the test plugin makes and it runs: it reports its handle,
    // is updated once, and reports one update.
    bool SystemWorks(ecs::System *system)
    {
        if(system == nullptr || system->Handle != "TestSystem")
        {
            return false;
        }
        if(system->Export().value("updates", -1) != 0)
        {
            return false;
        }
        system->UpdateSystem();
        return system->Export().value("updates", -1) == 1;
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
        the_seed::ComponentLoader components;
        the_seed::SystemLoader systems;
        components.PathAdd(moduleDir);
        systems.PathAdd(moduleDir);

        std::atomic<int> working{0};
        std::atomic<int> broken{0};
        the_seed::internal::ResetPluginOpenCallCount();

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
                    (SystemWorks(system.get()) ? working : broken)++;
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
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 2);
    }

    // Every thread of the group asks the same loader for the same plugin.
    for(int round = 0; round < ROUNDS; ++round)
    {
        the_seed::ComponentLoader components;
        the_seed::SystemLoader systems;
        components.PathAdd(moduleDir);
        systems.PathAdd(moduleDir);

        std::atomic<int> working{0};
        std::atomic<int> broken{0};
        the_seed::internal::ResetPluginOpenCallCount();

        RunTogether([&](int) {
            try
            {
                std::unique_ptr<ecs::Component> component = components.Create("testmodule");
                std::unique_ptr<ecs::System> system = systems.Create("testsystem");
                (VariantOf(component.get()) == 1 && SystemWorks(system.get()) ? working : broken)++;
            }
            catch(...)
            {
                ++broken;
            }
        });

        REQUIRE(broken.load() == 0);
        REQUIRE(working.load() == NUM_THREADS);
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 2);
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
        the_seed::ComponentLoader components;
        the_seed::SystemLoader systems;
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
            catch(const the_seed::LoadError &error)
            {
                if(error.ReasonGet() != the_seed::LoadError::Reason::NotFound)
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
        the_seed::ComponentLoader loader;
        loader.PathAdd(moduleDir);
        the_seed::internal::ResetPluginOpenCallCount();

        std::unique_ptr<ecs::Component> plain = loader.Create("testmodule");
        std::unique_ptr<ecs::Component> aliased = loader.Create("org/testmodule");

        REQUIRE(VariantOf(plain.get()) == 1);
        REQUIRE(VariantOf(aliased.get()) == 1);
        REQUIRE(loader.Get("testmodule") == loader.Get("org/testmodule"));
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 1);
    }

    SECTION("systems one after the other")
    {
        the_seed::SystemLoader loader;
        loader.PathAdd(moduleDir);
        the_seed::internal::ResetPluginOpenCallCount();

        REQUIRE(loader.Create("testsystem"));
        REQUIRE(loader.Create("org/testsystem"));
        REQUIRE(loader.Get("testsystem") == loader.Get("org/testsystem"));
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 1);
    }

    SECTION("at the same time")
    {
        for(int round = 0; round < ROUNDS; ++round)
        {
            the_seed::ComponentLoader loader;
            loader.PathAdd(moduleDir);
            std::atomic<int> working{0};
            std::atomic<int> broken{0};
            the_seed::internal::ResetPluginOpenCallCount();

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
            REQUIRE(the_seed::internal::PluginOpenCallCount() == 1);
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

    the_seed::ComponentLoader loader;
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
        the_seed::internal::ResetPluginOpenCallCount();
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
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 2);
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
        the_seed::ComponentLoader loader;
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
                    catch(const the_seed::LoadError &error)
                    {
                        if(error.ReasonGet() != the_seed::LoadError::Reason::NotFound)
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

TEST_CASE("component loader: 8+ threads first-time load of an existing plugin", "[concurrency]")
{
    const std::string moduleDir = seedtest::ModuleDir();
    seedtest::PluginPath("testmodule");

    for(int round = 0; round < 20; ++round)
    {
        // A fresh loader: no thread has loaded the plugin before.
        the_seed::ComponentLoader loader;
        loader.PathAdd(moduleDir);
        std::atomic<int> working{0};
        std::atomic<int> broken{0};
        seedtest::DestructionCounter destroyed{0};
        the_seed::internal::ResetPluginOpenCallCount();

        RunTogether([&](int index) {
            try
            {
                nlohmann::json configuration = {{"value", 100 + index},
                                                {"counter", seedtest::CounterAddress(destroyed)}};
                std::unique_ptr<ecs::Component> component = loader.Create("testmodule", &configuration);
                if(component && component->Type == "TestModule" &&
                   component->Export().value("variant", 0) == 1 &&
                   component->Export().value("value", -1) == 100 + index)
                {
                    ++working;
                }
                else
                {
                    ++broken;
                }
            }
            catch(...)
            {
                ++broken;
            }
        });

        REQUIRE(broken.load() == 0);
        REQUIRE(working.load() == NUM_THREADS);
        // Every component was destroyed at the end of its thread's call.
        REQUIRE(destroyed.load() == NUM_THREADS);
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 1);
    }
}

TEST_CASE("system loader: 8+ threads first-time load of an existing plugin", "[concurrency]")
{
    const std::string moduleDir = seedtest::ModuleDir();
    seedtest::PluginPath("testsystem");

    for(int round = 0; round < 20; ++round)
    {
        the_seed::SystemLoader loader;
        loader.PathAdd(moduleDir);
        std::atomic<int> working{0};
        std::atomic<int> broken{0};
        seedtest::DestructionCounter destroyed{0};
        the_seed::internal::ResetPluginOpenCallCount();

        RunTogether([&](int) {
            try
            {
                std::unique_ptr<ecs::System> system = loader.Create("testsystem", &destroyed);
                (SystemWorks(system.get()) ? working : broken)++;
            }
            catch(...)
            {
                ++broken;
            }
        });

        REQUIRE(broken.load() == 0);
        REQUIRE(working.load() == NUM_THREADS);
        // Each system's destructor ran once, adding to the counter handed to its creator.
        REQUIRE(destroyed.load() == NUM_THREADS);
        REQUIRE(the_seed::internal::PluginOpenCallCount() == 1);
    }
}
