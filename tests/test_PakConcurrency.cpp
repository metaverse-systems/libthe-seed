// Several threads requesting from paks through one loader or one ResourcePak.
//
//   - 8 threads x 50 rounds ask for the same resources of one pak, released
//     together each round: every result is correct and the description is
//     parsed exactly once for the whole run;
//   - a pak replaced by rename between and during the rounds: every result
//     equals one of the two versions in full, never a mix of them;
//   - one ResourcePak object used from 8 threads loads correct bytes;
//   - a version that fails validation is reported to every thread that asked
//     for it as LoadError, is not remembered, and the next request retries.
//
// Catch assertions are made on the main thread only; the threads record what
// they saw.

#include <catch_amalgamated.hpp>
#include <libthe-seed/LoadError.hpp>
#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>
#include "LoaderTestSupport.hpp"
#include "internal/PakIndex.hpp"

#include <atomic>
#include <barrier>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using seed::internal::PakDescriptionParseCount;
using seed::internal::ResetPakDescriptionParseCount;

namespace
{
    constexpr int ThreadCount = 8;
    constexpr int RoundCount = 50;

    using Contents = std::vector<std::pair<std::string, std::vector<std::uint8_t>>>;

    std::vector<std::uint8_t> Pattern(std::size_t count, std::uint8_t first)
    {
        std::vector<std::uint8_t> bytes(count);
        for(std::size_t i = 0; i < count; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(first + i * 5);
        }
        return bytes;
    }

    // The same names in every version; the bytes and sizes depend on `version`.
    Contents VersionContents(std::uint8_t version)
    {
        return {{"one", Pattern(100 + version * 13u, version)},
                {"two", Pattern(5000 + version * 101u, static_cast<std::uint8_t>(version + 50))},
                {"three", Pattern(version, static_cast<std::uint8_t>(version + 90))}};
    }

    // Failure texts recorded by threads, read by the main thread after join.
    class Failures
    {
    public:
        void Add(const std::string &text)
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->texts.push_back(text);
        }

        std::vector<std::string> Get() const
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            return this->texts;
        }

    private:
        mutable std::mutex mutex;
        std::vector<std::string> texts;
    };

    bool Matches(const std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> &resources,
                 const Contents &version)
    {
        if(resources.size() != version.size())
        {
            return false;
        }
        for(const auto &item : version)
        {
            auto found = resources.find(item.first);
            if(found == resources.end() || !found->second || found->second->Data != item.second)
            {
                return false;
            }
        }
        return true;
    }

    // Replaces `target` by a new file holding `contents`, by rename.
    void ReplaceByRename(const std::filesystem::path &stage_dir, const std::filesystem::path &target,
                         const std::string &name, const Contents &contents)
    {
        const std::filesystem::path staged = seedtest::WritePak(stage_dir, name, contents);
        std::filesystem::rename(staged, target);
    }
}

TEST_CASE("PakLoader requests from 8 threads parse the description once", "[PakConcurrency]")
{
    seedtest::ScratchDir scratch;
    const Contents contents = VersionContents(1);
    seedtest::WritePak(scratch.Path(), "shared", contents);

    PakLoader loader;
    loader.PathAdd(scratch.Path().string());

    ResetPakDescriptionParseCount();
    Failures failures;
    std::barrier sync(ThreadCount);
    std::vector<std::thread> threads;
    for(int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&]() {
            for(int round = 0; round < RoundCount; ++round)
            {
                sync.arrive_and_wait();
                try
                {
                    if(!Matches(loader.Load("shared"), contents))
                    {
                        failures.Add("Load(all) returned wrong contents");
                    }
                    if(!Matches(loader.Load("shared", {"three", "one", "two"}), contents))
                    {
                        failures.Add("Load(names) returned wrong contents");
                    }
                }
                catch(const std::exception &error)
                {
                    failures.Add(std::string("threw: ") + error.what());
                }
            }
        });
    }
    for(auto &thread : threads)
    {
        thread.join();
    }

    REQUIRE(failures.Get().empty());
    REQUIRE(PakDescriptionParseCount() == 1);
}

TEST_CASE("PakLoader results during replacement by rename are each one version in full", "[PakConcurrency]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path stage = scratch.Path() / "stage";
    const std::filesystem::path location = scratch.Path() / "location";
    const Contents version_a = VersionContents(1);
    const Contents version_b = VersionContents(2);
    const std::filesystem::path target = seedtest::WritePak(location, "swap", version_a);

    PakLoader loader;
    loader.PathAdd(location.string());

    Failures failures;
    std::atomic<int> seen_a{0};
    std::atomic<int> seen_b{0};
    // The readers and the replacer meet at the start of every round.
    std::barrier sync(ThreadCount + 1);
    std::vector<std::thread> threads;
    for(int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&]() {
            for(int round = 0; round < RoundCount; ++round)
            {
                sync.arrive_and_wait();
                try
                {
                    auto resources = loader.Load("swap");
                    if(Matches(resources, version_a))
                    {
                        ++seen_a;
                    }
                    else if(Matches(resources, version_b))
                    {
                        ++seen_b;
                    }
                    else
                    {
                        failures.Add("a result equals neither version in full");
                    }
                }
                catch(const std::exception &error)
                {
                    failures.Add(std::string("threw: ") + error.what());
                }
            }
        });
    }
    threads.emplace_back([&]() {
        for(int round = 0; round < RoundCount; ++round)
        {
            sync.arrive_and_wait();
            try
            {
                ReplaceByRename(stage, target, "swap", round % 2 == 0 ? version_b : version_a);
            }
            catch(const std::exception &error)
            {
                failures.Add(std::string("replace threw: ") + error.what());
            }
        }
    });
    for(auto &thread : threads)
    {
        thread.join();
    }

    REQUIRE(failures.Get().empty());
    REQUIRE(seen_a.load() + seen_b.load() == ThreadCount * RoundCount);

    // The last round wrote version A (round 49 is odd); a request after the
    // threads stopped sees it.
    REQUIRE(Matches(loader.Load("swap"), version_a));
}

TEST_CASE("One ResourcePak object used from 8 threads loads correct bytes", "[PakConcurrency]")
{
    seedtest::ScratchDir scratch;
    const Contents contents = VersionContents(3);
    const std::filesystem::path file = seedtest::WritePak(scratch.Path(), "single", contents);

    ResourcePak pak(file.string());
    Failures failures;
    std::barrier sync(ThreadCount);
    std::vector<std::thread> threads;
    for(int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&]() {
            for(int round = 0; round < RoundCount; ++round)
            {
                sync.arrive_and_wait();
                try
                {
                    for(const auto &item : contents)
                    {
                        if(pak.Load(item.first).Data != item.second)
                        {
                            failures.Add("wrong bytes for " + item.first);
                        }
                    }
                    if(pak.ResourceNames().size() != contents.size())
                    {
                        failures.Add("wrong name count");
                    }
                }
                catch(const std::exception &error)
                {
                    failures.Add(std::string("threw: ") + error.what());
                }
            }
        });
    }
    for(auto &thread : threads)
    {
        thread.join();
    }
    REQUIRE(failures.Get().empty());
}

TEST_CASE("ResourcePak copies used from threads validate a replaced file", "[PakConcurrency]")
{
    seedtest::ScratchDir scratch;
    const Contents version_a = VersionContents(1);
    const Contents version_b = VersionContents(2);
    const std::filesystem::path target = seedtest::WritePak(scratch.Path() / "location", "copies", version_a);
    const ResourcePak original(target.string());

    // The file changes before the threads start; every copy meets the new
    // version together.
    ReplaceByRename(scratch.Path() / "stage", target, "copies", version_b);

    Failures failures;
    std::barrier sync(ThreadCount);
    std::vector<std::thread> threads;
    for(int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&]() {
            ResourcePak copy = original;
            sync.arrive_and_wait();
            try
            {
                for(const auto &item : version_b)
                {
                    if(copy.Load(item.first).Data != item.second)
                    {
                        failures.Add("wrong bytes for " + item.first);
                    }
                }
            }
            catch(const std::exception &error)
            {
                failures.Add(std::string("threw: ") + error.what());
            }
        });
    }
    for(auto &thread : threads)
    {
        thread.join();
    }
    REQUIRE(failures.Get().empty());
}

TEST_CASE("A failed validation reaches every waiting thread and the next request retries", "[PakConcurrency]")
{
    seedtest::ScratchDir scratch;
    const std::filesystem::path location = scratch.Path() / "location";
    const std::filesystem::path stage = scratch.Path() / "stage";
    const Contents sound = VersionContents(1);

    seedtest::PakSpec spec;
    spec.name = "flaky";
    for(const auto &item : sound)
    {
        spec.resources.push_back({item.first, item.second, ""});
    }
    const std::vector<std::uint8_t> damaged = seedtest::PakTruncate(seedtest::PakBytes(spec), 3);
    const std::filesystem::path target = location / "flaky.pak";
    seedtest::WriteBytes(target, damaged);

    PakLoader loader;
    loader.PathAdd(location.string());

    for(int round = 0; round < 10; ++round)
    {
        Failures failures;
        std::barrier sync(ThreadCount);
        std::vector<std::thread> threads;
        for(int t = 0; t < ThreadCount; ++t)
        {
            threads.emplace_back([&]() {
                sync.arrive_and_wait();
                try
                {
                    loader.Load("flaky");
                    failures.Add("Load did not throw");
                }
                catch(const LoadError &error)
                {
                    if(error.ReasonGet() != LoadError::Reason::NotLoadable ||
                       error.DetailGet().rfind("damaged: ", 0) != 0)
                    {
                        failures.Add(std::string("wrong error: ") + error.what());
                    }
                }
                catch(const std::exception &error)
                {
                    failures.Add(std::string("not a LoadError: ") + error.what());
                }
            });
        }
        for(auto &thread : threads)
        {
            thread.join();
        }
        INFO("round " << round);
        REQUIRE(failures.Get().empty());

        // The failure was not remembered: a sound version loads at once, and
        // a damaged one is reported again in the next round.
        ReplaceByRename(stage, target, "flaky", sound);
        REQUIRE(Matches(loader.Load("flaky"), sound));
        seedtest::WriteBytes(target, damaged);
    }
}
