// A process killed in the middle of replacing a file.
//
// A child process replaces a large file in a loop, the parent kills it with
// SIGKILL after a random short delay, and then looks at the folder. The file
// must hold exactly the old or exactly the new content. A working file may be
// left behind by the kill; it must follow the documented name pattern
// .<name>.seedtmp.<pid>.<hex>, and the test counts and removes those. A
// replacement made afterwards must succeed.
//
// Only the child that this test starts is ever killed.
//
// Linux only: on Windows builds the test reports that it was skipped.

#include "ReplaceTestSupport.hpp"

#include "internal/FileIO.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(_WIN32)

TEST_CASE("kill: a replacement interrupted by SIGKILL", "[FileReplaceKill]")
{
    seedtest::replace::SkipWithMessage(
        "SIGKILL of a child process is Linux only; wine process kill semantics are not "
        "representative");
}

#else

namespace {

using namespace seedtest::replace;
namespace fs = std::filesystem;

constexpr std::size_t kLargeSize = 4ull * 1024 * 1024;
constexpr int kRounds = 200;
constexpr int kMaxDelayMicroseconds = 20000;

}

TEST_CASE("kill: a replacement interrupted by SIGKILL leaves the old or the new content",
          "[FileReplaceKill]")
{
    seedtest::ScratchDir scratch("seed-replace-kill");
    const std::string path = scratch.File("target.bin");
    const std::string name = "target.bin";

    // Content 0 is the old content, content 1 the new; the child alternates
    // so that every replacement is a change.
    const Bytes contents[2] = {DistinctContent(0, kLargeSize), DistinctContent(1, kLargeSize)};
    const std::regex leftover_pattern("^\\." + name + "\\.seedtmp\\.[0-9]+\\.[0-9a-f]+$");

    std::mt19937_64 random{std::random_device{}()};
    std::uniform_int_distribution<int> delay(0, kMaxDelayMicroseconds);

    int leftovers_seen = 0;
    int rounds_with_new_content = 0;

    for(int round = 0; round < kRounds; ++round)
    {
        WriteAll(path, contents[0]);

        const pid_t child = ::fork();
        REQUIRE(child >= 0);
        if(child == 0)
        {
            try
            {
                for(;;)
                {
                    WriteFileBytes(path, contents[1]);
                    WriteFileBytes(path, contents[0]);
                }
            }
            catch(...)
            {
                ::_exit(2);
            }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(delay(random)));
        ::kill(child, SIGKILL);
        int status = 0;
        REQUIRE(::waitpid(child, &status, 0) == child);
        REQUIRE(WIFSIGNALED(status));

        const Bytes now = ReadAll(path);
        const bool old_content = now == contents[0];
        const bool new_content = now == contents[1];
        INFO("round " << round << ", file size " << now.size());
        REQUIRE((old_content || new_content));
        if(new_content)
        {
            ++rounds_with_new_content;
        }

        for(const FolderEntry &entry : ListFolder(scratch.Path()))
        {
            if(entry.name == name)
            {
                continue;
            }
            INFO("unexpected entry " << entry.name << " after round " << round);
            REQUIRE(std::regex_match(entry.name, leftover_pattern));
            ++leftovers_seen;
            fs::remove(scratch.Path() / entry.name);
        }

        // The file is still usable and the next replacement succeeds.
        const Bytes next = DistinctContent(100 + static_cast<std::uint64_t>(round), 4096);
        REQUIRE_NOTHROW(WriteFileBytes(path, next));
        REQUIRE(ReadAll(path) == next);
        const auto listing = ListFolder(scratch.Path());
        INFO("folder: " << Describe(listing));
        REQUIRE(listing.size() == 1);
    }

    std::printf("kill test: %d rounds, %d left a working file (removed by the test), %d ended "
                "with the new content\n",
                kRounds, leftovers_seen, rounds_with_new_content);
    std::fflush(stdout);
}

#endif
