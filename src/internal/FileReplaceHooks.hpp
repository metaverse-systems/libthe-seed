#pragma once

// Replaceable system calls for the file replacement in FileIO.cpp.
//
// Every system call the replacement makes goes through the table returned by
// ActiveFileReplaceHooks(). The table defaults to the real calls. A test
// installs a table with FileReplaceHooksScope to make one step fail, or to
// read back the order in which the steps ran. The replacement calls
// RecordFileReplaceCall() just before each call it makes, so the sequence can
// be inspected even when the real calls are in place.
//
// This header is internal: it is not installed and is not part of the
// library's interface.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace seed::internal
{

#if defined(_WIN32)

struct FileReplaceHooks
{
    // Creates the working file or opens a folder. Same meaning as CreateFileW.
    HANDLE (*create_file)(LPCWSTR path, DWORD access, DWORD share, DWORD creation, DWORD flags);
    // Same meaning as WriteFile, without an overlapped structure.
    BOOL (*write_file)(HANDLE file, const void *data, DWORD size, DWORD *written);
    // Sets the attributes of a file by path. Same meaning as SetFileAttributesW.
    BOOL (*set_attributes)(LPCWSTR path, DWORD attributes);
    // Same meaning as FlushFileBuffers.
    BOOL (*flush_file)(HANDLE file);
    // Same meaning as CloseHandle.
    BOOL (*close_handle)(HANDLE handle);
    // Moves the working file over the target. Same meaning as MoveFileExW.
    BOOL (*move_file)(LPCWSTR from, LPCWSTR to, DWORD flags);
    // Removes a file by path. Same meaning as DeleteFileW.
    BOOL (*delete_file)(LPCWSTR path);
};

#else

struct FileReplaceHooks
{
    // Same meaning as open(2) with a mode argument.
    int (*open)(const char *path, int flags, mode_t mode);
    // Same meaning as write(2).
    ssize_t (*write)(int fd, const void *data, std::size_t size);
    // Same meaning as fchmod(2).
    int (*fchmod)(int fd, mode_t mode);
    // Same meaning as fchown(2).
    int (*fchown)(int fd, uid_t owner, gid_t group);
    // Same meaning as fsync(2).
    int (*fsync)(int fd);
    // Same meaning as close(2).
    int (*close)(int fd);
    // Same meaning as rename(2).
    int (*rename)(const char *from, const char *to);
    // Same meaning as unlink(2).
    int (*unlink)(const char *path);
    // Flushes the folder entry of a path: opens the folder, fsync, close.
    // Returns 0 on success, otherwise an errno value.
    int (*fsync_folder)(const char *folder);
};

#endif

// One recorded step: the name of the call ("open", "write", "fchmod",
// "fchown", "fsync", "close", "rename", "unlink", "fsync_folder", and the
// Win32 names under _WIN32), the path it acted on (empty for descriptor and
// handle calls), and one number that depends on the call: the mode for
// "open" and "fchmod", the byte count for "write", otherwise zero.
struct FileReplaceCall
{
    std::string name;
    std::string path;
    std::uint64_t value = 0;
};

// The table in force. The real system calls unless a scope is active.
const FileReplaceHooks &ActiveFileReplaceHooks();

// A table holding the real system calls.
FileReplaceHooks DefaultFileReplaceHooks();

// Makes a table the active one and returns the one it replaced.
FileReplaceHooks ExchangeFileReplaceHooks(const FileReplaceHooks &hooks);

// Starts or stops keeping the recorded calls. Starting clears the list.
void SetFileReplaceRecording(bool enabled);

// Adds one entry when recording is on; does nothing otherwise. Safe to call
// from several threads.
void RecordFileReplaceCall(const char *name, const std::string &path = std::string(),
                           std::uint64_t value = 0);

// Copy of the recorded calls, in the order they were made.
std::vector<FileReplaceCall> RecordedFileReplaceCalls();

// Installs a table, and turns recording on, for the life of the object.
// The previous table and recording state are restored on destruction.
// Not for use from several threads at once.
class FileReplaceHooksScope
{
public:
    // Keeps the real calls and only records them; use Install() or the
    // constructor taking a table to change a step.
    FileReplaceHooksScope() : FileReplaceHooksScope(DefaultFileReplaceHooks()) {}

    explicit FileReplaceHooksScope(const FileReplaceHooks &hooks)
        : previous(ExchangeFileReplaceHooks(hooks))
    {
        SetFileReplaceRecording(true);
    }

    FileReplaceHooksScope(const FileReplaceHooksScope &) = delete;
    FileReplaceHooksScope &operator=(const FileReplaceHooksScope &) = delete;

    ~FileReplaceHooksScope()
    {
        SetFileReplaceRecording(false);
        ExchangeFileReplaceHooks(this->previous);
    }

    // Replaces the installed table while the scope is active.
    void Install(const FileReplaceHooks &hooks) { ExchangeFileReplaceHooks(hooks); }

    // The calls recorded since the scope began.
    std::vector<FileReplaceCall> Calls() const { return RecordedFileReplaceCalls(); }

private:
    FileReplaceHooks previous;
};

}
