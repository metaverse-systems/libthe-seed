#include <catch_amalgamated.hpp>
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <atomic>
#include <barrier>
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

