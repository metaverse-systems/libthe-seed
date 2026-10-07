#pragma once

// Helpers for the tests of the shared file replacement and of the signers that
// use it: folder listings, whole-file reads and writes, permission bits,
// visible skip messages and distinct contents for the concurrency and kill
// tests.

#include "TestPaths.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seedtest::replace
{

using Bytes = std::vector<std::uint8_t>;

// One entry of a folder: its name, its size and its permission bits (the
// bits are zero on Windows, where they do not apply).
struct FolderEntry
{
    std::string name;
    std::uint64_t size = 0;
    unsigned mode = 0;

    bool operator==(const FolderEntry &other) const
    {
        return this->name == other.name && this->size == other.size && this->mode == other.mode;
    }
};

// Permission bits (07777) of a path, following links. Zero on Windows.
inline unsigned ModeOf(const std::string &path)
{
#if defined(_WIN32)
    (void)path;
    return 0;
#else
    struct stat info;
    if(::stat(path.c_str(), &info) != 0)
    {
        FAIL("Cannot stat " << path);
    }
    return static_cast<unsigned>(info.st_mode & 07777);
#endif
}

// Sets the permission bits (07777) of a path. Does nothing on Windows.
inline void SetModeOf(const std::string &path, unsigned mode)
{
#if defined(_WIN32)
    (void)path;
    (void)mode;
#else
    if(::chmod(path.c_str(), static_cast<mode_t>(mode)) != 0)
    {
        FAIL("Cannot chmod " << path);
    }
#endif
}

// Everything in a folder, sorted by name, with sizes and modes. Links are
// listed as themselves (the size and mode are those of the link).
inline std::vector<FolderEntry> ListFolder(const std::filesystem::path &folder)
{
    std::vector<FolderEntry> entries;
    std::error_code ec;
    for(std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        FolderEntry entry;
        entry.name = it->path().filename().string();
#if defined(_WIN32)
        std::error_code size_ec;
        entry.size = it->is_regular_file(size_ec) ? it->file_size(size_ec) : 0;
#else
        struct stat info;
        if(::lstat(it->path().c_str(), &info) != 0)
        {
            continue;
        }
        entry.size = static_cast<std::uint64_t>(info.st_size);
        entry.mode = static_cast<unsigned>(info.st_mode & 07777);
#endif
        entries.push_back(entry);
    }
    if(ec)
    {
        FAIL("Cannot list " << folder.string() << ": " << ec.message());
    }
    std::sort(entries.begin(), entries.end(),
              [](const FolderEntry &a, const FolderEntry &b) { return a.name < b.name; });
    return entries;
}

// The names in a listing, for messages.
inline std::string Describe(const std::vector<FolderEntry> &entries)
{
    std::string text;
    for(const FolderEntry &entry : entries)
    {
        text += entry.name + " (" + std::to_string(entry.size) + " bytes, mode " +
                std::to_string(entry.mode) + ") ";
    }
    return text;
}

inline Bytes ReadAll(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    {
        FAIL("Cannot open " << path);
    }
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Writes a file in one step, replacing what is there. Not the code under test.
inline void WriteAll(const std::string &path, const Bytes &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out)
    {
        FAIL("Cannot create " << path);
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if(!out)
    {
        FAIL("Cannot write " << path);
    }
}

namespace detail
{

inline std::mutex &SkipLock()
{
    static std::mutex lock;
    return lock;
}

inline std::vector<std::string> &SkipList()
{
    static std::vector<std::string> reasons;
    return reasons;
}

}

// Reports a case that cannot run here. The line starts with "SKIPPED:" so it
// stands out in the test log, and the reason is kept for SkippedReasons().
inline void SkipWithMessage(const std::string &reason)
{
    std::lock_guard<std::mutex> guard(detail::SkipLock());
    detail::SkipList().push_back(reason);
    std::printf("SKIPPED: %s\n", reason.c_str());
    std::fflush(stdout);
}

// The reasons passed to SkipWithMessage so far.
inline std::vector<std::string> SkippedReasons()
{
    std::lock_guard<std::mutex> guard(detail::SkipLock());
    return detail::SkipList();
}

// True when the process runs with the rights of the administrator account.
// Permission-denied cases cannot fail there, so they are skipped with a
// message rather than passed or failed.
inline bool RunningAsRoot()
{
#if defined(_WIN32)
    return false;
#else
    return ::geteuid() == 0;
#endif
}

// Content number `index` of `size` bytes (at least 16). The first eight
// bytes hold the index and the rest follows from it, so two contents never
// share a byte range and a mixture of two is recognised by IdentifyContent.
inline Bytes DistinctContent(std::uint64_t index, std::size_t size)
{
    Bytes bytes(size < 16 ? 16 : size);
    for(std::size_t i = 0; i < 8; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(index >> (8 * i));
    }
    std::uint64_t state = index * 0x9E3779B97F4A7C15ull + 0x1234567ull;
    for(std::size_t i = 8; i < bytes.size(); ++i)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        bytes[i] = static_cast<std::uint8_t>(state >> 24);
    }
    return bytes;
}

// The index of a content made by DistinctContent with the same size, or
// -1 when the bytes are not exactly one of the contents 0 to count - 1.
inline long IdentifyContent(const Bytes &bytes, std::uint64_t count)
{
    if(bytes.size() < 16)
    {
        return -1;
    }
    std::uint64_t index = 0;
    for(std::size_t i = 0; i < 8; ++i)
    {
        index |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    if(index >= count)
    {
        return -1;
    }
    return DistinctContent(index, bytes.size()) == bytes ? static_cast<long>(index) : -1;
}

}
