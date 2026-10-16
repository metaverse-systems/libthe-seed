#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <cerrno>
    #include <cstring>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include "RandomAccessFile.hpp"

#include "PluginSearch.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <system_error>
#include <utility>

namespace the_seed::internal
{

namespace
{
std::atomic<std::uint64_t> bytes_read{0};

// Largest single request handed to the system.
constexpr std::uint64_t chunk_limit = std::uint64_t{1} << 30;

std::string ShortReadText(std::uint64_t offset, std::uint64_t expected, std::uint64_t got)
{
    return "file is shorter than its description: expected " + std::to_string(expected) + " bytes at offset " +
           std::to_string(offset) + ", got " + std::to_string(got);
}
} // namespace

FileShortRead::FileShortRead(std::uint64_t offset, std::uint64_t expected, std::uint64_t got)
    : std::runtime_error(ShortReadText(offset, expected, got)), offset(offset), expected(expected), got(got)
{
}

std::uint64_t BytesReadCount()
{
    return bytes_read.load(std::memory_order_relaxed);
}

void ResetBytesReadCount()
{
    bytes_read.store(0, std::memory_order_relaxed);
}

RandomAccessFile::RandomAccessFile(std::filesystem::path path, std::intptr_t handle) : path(std::move(path)), handle(handle)
{
}

RandomAccessFile::RandomAccessFile(RandomAccessFile &&other) noexcept : path(std::move(other.path)), handle(other.handle)
{
    other.handle = -1;
}

RandomAccessFile &RandomAccessFile::operator=(RandomAccessFile &&other) noexcept
{
    if(this != &other)
    {
        this->Close();
        this->path = std::move(other.path);
        this->handle = other.handle;
        other.handle = -1;
    }
    return *this;
}

RandomAccessFile::~RandomAccessFile()
{
    this->Close();
}

#ifdef _WIN32

namespace
{
std::uint64_t Join(DWORD high, DWORD low)
{
    return (static_cast<std::uint64_t>(high) << 32) | low;
}
} // namespace

RandomAccessFile RandomAccessFile::Open(const std::filesystem::path &path)
{
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
    {
        const DWORD id = GetLastError();
        throw FileOpenError(PlatformMessage(id), id == ERROR_FILE_NOT_FOUND || id == ERROR_PATH_NOT_FOUND);
    }
    return RandomAccessFile(path, reinterpret_cast<std::intptr_t>(handle));
}

void RandomAccessFile::Close() noexcept
{
    if(this->handle != -1)
    {
        CloseHandle(reinterpret_cast<HANDLE>(this->handle));
        this->handle = -1;
    }
}

FileStamp RandomAccessFile::Stamp() const
{
    BY_HANDLE_FILE_INFORMATION info;
    if(!GetFileInformationByHandle(reinterpret_cast<HANDLE>(this->handle), &info))
    {
        throw FileReadError(PlatformMessage(GetLastError()));
    }

    FileStamp stamp;
    stamp.device = info.dwVolumeSerialNumber;
    stamp.file = Join(info.nFileIndexHigh, info.nFileIndexLow);
    stamp.size = Join(info.nFileSizeHigh, info.nFileSizeLow);
    stamp.modified = static_cast<std::int64_t>(Join(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime));
    return stamp;
}

std::uint64_t RandomAccessFile::Size() const
{
    LARGE_INTEGER size;
    if(!GetFileSizeEx(reinterpret_cast<HANDLE>(this->handle), &size))
    {
        throw FileReadError(PlatformMessage(GetLastError()));
    }
    return static_cast<std::uint64_t>(size.QuadPart);
}

void RandomAccessFile::ReadAt(std::uint64_t offset, void *destination, std::uint64_t count) const
{
    std::uint8_t *out = static_cast<std::uint8_t *>(destination);
    std::uint64_t done = 0;
    while(done < count)
    {
        const DWORD want = static_cast<DWORD>(std::min(count - done, chunk_limit));
        const std::uint64_t at = offset + done;
        if(at < offset)
        {
            throw FileShortRead(offset, count, done);
        }

        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(at & 0xFFFFFFFFu);
        overlapped.OffsetHigh = static_cast<DWORD>(at >> 32);

        DWORD got = 0;
        if(!ReadFile(reinterpret_cast<HANDLE>(this->handle), out + done, want, &got, &overlapped))
        {
            const DWORD id = GetLastError();
            if(id == ERROR_HANDLE_EOF)
            {
                throw FileShortRead(offset, count, done);
            }
            throw FileReadError(PlatformMessage(id));
        }
        if(got == 0)
        {
            throw FileShortRead(offset, count, done);
        }
        done += got;
        bytes_read.fetch_add(got, std::memory_order_relaxed);
        if(got < want)
        {
            // A short count from a file read means the end of the file.
            throw FileShortRead(offset, count, done);
        }
    }
}

#else

RandomAccessFile RandomAccessFile::Open(const std::filesystem::path &path)
{
    int descriptor;
    do
    {
        descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while(descriptor < 0 && errno == EINTR);

    if(descriptor < 0)
    {
        const int id = errno;
        throw FileOpenError(std::generic_category().message(id), id == ENOENT || id == ENOTDIR);
    }
    return RandomAccessFile(path, static_cast<std::intptr_t>(descriptor));
}

void RandomAccessFile::Close() noexcept
{
    if(this->handle != -1)
    {
        ::close(static_cast<int>(this->handle));
        this->handle = -1;
    }
}

FileStamp RandomAccessFile::Stamp() const
{
    struct stat info;
    if(::fstat(static_cast<int>(this->handle), &info) != 0)
    {
        throw FileReadError(std::generic_category().message(errno));
    }

#ifdef __APPLE__
    const struct timespec &time = info.st_mtimespec;
#else
    const struct timespec &time = info.st_mtim;
#endif

    FileStamp stamp;
    stamp.device = static_cast<std::uint64_t>(info.st_dev);
    stamp.file = static_cast<std::uint64_t>(info.st_ino);
    stamp.size = static_cast<std::uint64_t>(info.st_size);
    stamp.modified = static_cast<std::int64_t>(time.tv_sec) * 1000000000 + static_cast<std::int64_t>(time.tv_nsec);
    return stamp;
}

std::uint64_t RandomAccessFile::Size() const
{
    const off_t end = ::lseek(static_cast<int>(this->handle), 0, SEEK_END);
    if(end < 0)
    {
        throw FileReadError(std::generic_category().message(errno));
    }
    return static_cast<std::uint64_t>(end);
}

void RandomAccessFile::ReadAt(std::uint64_t offset, void *destination, std::uint64_t count) const
{
    constexpr std::uint64_t offset_limit = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());

    std::uint8_t *out = static_cast<std::uint8_t *>(destination);
    std::uint64_t done = 0;
    while(done < count)
    {
        // A position the system cannot express is past the end of any file.
        if(offset > offset_limit || done > offset_limit - offset)
        {
            throw FileShortRead(offset, count, done);
        }

        const size_t want = static_cast<size_t>(std::min(count - done, chunk_limit));
        const ssize_t got = ::pread(static_cast<int>(this->handle), out + done, want, static_cast<off_t>(offset + done));
        if(got < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            throw FileReadError(std::generic_category().message(errno));
        }
        if(got == 0)
        {
            throw FileShortRead(offset, count, done);
        }
        done += static_cast<std::uint64_t>(got);
        bytes_read.fetch_add(static_cast<std::uint64_t>(got), std::memory_order_relaxed);
    }
}

#endif

} // namespace the_seed::internal
