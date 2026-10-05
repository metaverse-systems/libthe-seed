#pragma once

#include <catch_amalgamated.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined by the build"
#endif
#ifndef LIBRARY_PATH_UNDER_TEST
#error "LIBRARY_PATH_UNDER_TEST must be defined by the build"
#endif
#ifndef MODULE_DIR
#error "MODULE_DIR must be defined by the build"
#endif

namespace seedtest {

namespace detail {

inline std::filesystem::path MustExist(const std::filesystem::path &path)
{
    std::error_code ec;
    if(!std::filesystem::exists(path, ec))
    {
        FAIL("Required test file is missing: " << path.string());
    }
    return path;
}

inline long ProcessId()
{
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

}

// Absolute path of a sample file in the tests/fixtures directory.
// Fails the running test, naming the file, when it is absent.
inline std::string FixturePath(const std::string &name)
{
    return detail::MustExist(std::filesystem::path(FIXTURES_DIR) / name).string();
}

// Absolute path of the library under test.
inline std::string LibraryPath()
{
    return detail::MustExist(std::filesystem::path(LIBRARY_PATH_UNDER_TEST)).string();
}

// Absolute path of the directory holding the test plugin.
inline std::string ModuleDir()
{
    return detail::MustExist(std::filesystem::path(MODULE_DIR)).string();
}

// A uniquely named directory that is removed, with its contents, when the
// object goes out of scope. Tests write every file they create inside it.
class ScratchDir
{
public:
    explicit ScratchDir(const std::string &label = "seed-test")
    {
        static std::atomic<unsigned> counter{0};
        std::random_device device;
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::temp_directory_path();

        for(;;)
        {
            const std::uint64_t suffix =
                (static_cast<std::uint64_t>(device()) << 32) | device();
            std::filesystem::path candidate = base /
                (label + "-" + std::to_string(detail::ProcessId()) + "-" +
                 std::to_string(counter++) + "-" + std::to_string(suffix));
            if(std::filesystem::create_directory(candidate, ec))
            {
                this->dir = candidate;
                return;
            }
            if(ec)
            {
                FAIL("Cannot create scratch directory " << candidate.string()
                     << ": " << ec.message());
            }
        }
    }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    ~ScratchDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(this->dir, ec);
    }

    const std::filesystem::path &Path() const { return this->dir; }

    std::string File(const std::string &name) const
    {
        return (this->dir / name).string();
    }

private:
    std::filesystem::path dir;
};

}
