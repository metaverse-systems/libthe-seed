#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace the_seed::internal
{

// Identifies one version of a file, taken from an open handle. Two stamps
// are equal when the volume, the file, the size and the modification time all
// are. Not ordered.
struct FileStamp
{
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    std::uint64_t size = 0;
    // Nanoseconds (POSIX) or 100 ns ticks (Windows) since the system's epoch.
    std::int64_t modified = 0;

    bool operator==(const FileStamp &other) const
    {
        return this->device == other.device && this->file == other.file && this->size == other.size &&
               this->modified == other.modified;
    }

    bool operator!=(const FileStamp &other) const
    {
        return !(*this == other);
    }
};

// A file could not be opened. what() is the system's text.
class FileOpenError : public std::runtime_error
{
  public:
    FileOpenError(const std::string &reason, bool missing) : std::runtime_error(reason), missing(missing)
    {
    }

    // True when the file does not exist.
    bool MissingGet() const
    {
        return this->missing;
    }

  private:
    bool missing;
};

// A read asked for bytes the file does not hold (it is shorter than its
// description says, usually because it was changed while being read).
class FileShortRead : public std::runtime_error
{
  public:
    FileShortRead(std::uint64_t offset, std::uint64_t expected, std::uint64_t got);

    std::uint64_t OffsetGet() const
    {
        return this->offset;
    }

    std::uint64_t ExpectedGet() const
    {
        return this->expected;
    }

    std::uint64_t GotGet() const
    {
        return this->got;
    }

  private:
    std::uint64_t offset;
    std::uint64_t expected;
    std::uint64_t got;
};

// A system call failed while reading an open file. what() is the
// system's text.
class FileReadError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// An open read-only handle to one file, closed when the object is
// destroyed. Move-only. Reads are positional, so no read position is shared
// and the file is never mapped.
class RandomAccessFile
{
  public:
    // Opens `path` (absolute) for reading. Throws FileOpenError.
    static RandomAccessFile Open(const std::filesystem::path &path);

    RandomAccessFile(RandomAccessFile &&other) noexcept;
    RandomAccessFile &operator=(RandomAccessFile &&other) noexcept;
    RandomAccessFile(const RandomAccessFile &) = delete;
    RandomAccessFile &operator=(const RandomAccessFile &) = delete;
    ~RandomAccessFile();

    const std::filesystem::path &PathGet() const
    {
        return this->path;
    }

    // The stamp of the open file, read from the handle. Throws FileReadError.
    FileStamp Stamp() const;

    // The size of the open file now, found by moving to its end (so a folder
    // or a file the system cannot size is refused). Throws FileReadError.
    std::uint64_t Size() const;

    // Fills exactly `count` bytes at `destination` from `offset`. Throws
    // FileShortRead when the file ends first, FileReadError when the system
    // refuses. Adds the bytes read to BytesReadCount().
    void ReadAt(std::uint64_t offset, void *destination, std::uint64_t count) const;

  private:
    RandomAccessFile(std::filesystem::path path, std::intptr_t handle);
    void Close() noexcept;

    std::filesystem::path path;
    // A file descriptor (POSIX) or a HANDLE (Windows); -1 when empty.
    std::intptr_t handle = -1;
};

// Bytes read through RandomAccessFile::ReadAt since the process started or since the
// last reset. Tests use it to check how much of a file is read; it has no
// effect on behaviour.
std::uint64_t BytesReadCount();
void ResetBytesReadCount();

// The pak code's names for the same things.
using PakFile = RandomAccessFile;
using PakStamp = FileStamp;
using PakOpenError = FileOpenError;
using PakShortRead = FileShortRead;
using PakReadError = FileReadError;

inline std::uint64_t PakBytesReadCount()
{
    return BytesReadCount();
}

inline void ResetPakBytesReadCount()
{
    ResetBytesReadCount();
}

} // namespace the_seed::internal
