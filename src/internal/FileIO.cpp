#include "FileIO.hpp"
#include "FileReplaceHooks.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seed::internal
{

namespace
{

#if defined(_WIN32)

FileReplaceHooks RealHooks()
{
    FileReplaceHooks hooks{};
    hooks.create_file = [](LPCWSTR path, DWORD access, DWORD share, DWORD creation, DWORD flags) {
        return ::CreateFileW(path, access, share, nullptr, creation, flags, nullptr);
    };
    hooks.write_file = [](HANDLE file, const void *data, DWORD size, DWORD *written) {
        return ::WriteFile(file, data, size, written, nullptr);
    };
    hooks.set_attributes = [](LPCWSTR path, DWORD attributes) {
        return ::SetFileAttributesW(path, attributes);
    };
    hooks.flush_file = [](HANDLE file) { return ::FlushFileBuffers(file); };
    hooks.close_handle = [](HANDLE handle) { return ::CloseHandle(handle); };
    hooks.move_file = [](LPCWSTR from, LPCWSTR to, DWORD flags) {
        return ::MoveFileExW(from, to, flags);
    };
    hooks.delete_file = [](LPCWSTR path) { return ::DeleteFileW(path); };
    return hooks;
}

#else

FileReplaceHooks RealHooks()
{
    FileReplaceHooks hooks{};
    hooks.open = [](const char *path, int flags, mode_t mode) {
        return ::open(path, flags, mode);
    };
    hooks.write = [](int fd, const void *data, std::size_t size) {
        return ::write(fd, data, size);
    };
    hooks.fchmod = [](int fd, mode_t mode) { return ::fchmod(fd, mode); };
    hooks.fchown = [](int fd, uid_t owner, gid_t group) { return ::fchown(fd, owner, group); };
    hooks.fsync = [](int fd) { return ::fsync(fd); };
    hooks.close = [](int fd) { return ::close(fd); };
    hooks.rename = [](const char *from, const char *to) { return ::rename(from, to); };
    hooks.unlink = [](const char *path) { return ::unlink(path); };
    hooks.fsync_folder = [](const char *folder) {
        const int fd = ::open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if(fd < 0)
        {
            return errno;
        }
        const int result = ::fsync(fd) == 0 ? 0 : errno;
        ::close(fd);
        return result;
    };
    return hooks;
}

#endif

struct HookState
{
    std::mutex lock;
    FileReplaceHooks hooks = RealHooks();
    bool recording = false;
    std::vector<FileReplaceCall> calls;
};

HookState &State()
{
    static HookState state;
    return state;
}

}

const FileReplaceHooks &ActiveFileReplaceHooks()
{
    return State().hooks;
}

FileReplaceHooks DefaultFileReplaceHooks()
{
    return RealHooks();
}

FileReplaceHooks ExchangeFileReplaceHooks(const FileReplaceHooks &hooks)
{
    HookState &state = State();
    std::lock_guard<std::mutex> guard(state.lock);
    const FileReplaceHooks previous = state.hooks;
    state.hooks = hooks;
    return previous;
}

void SetFileReplaceRecording(bool enabled)
{
    HookState &state = State();
    std::lock_guard<std::mutex> guard(state.lock);
    state.recording = enabled;
    if(enabled)
    {
        state.calls.clear();
    }
}

void RecordFileReplaceCall(const char *name, const std::string &path, std::uint64_t value)
{
    HookState &state = State();
    std::lock_guard<std::mutex> guard(state.lock);
    if(state.recording)
    {
        state.calls.push_back(FileReplaceCall{name, path, value});
    }
}

std::vector<FileReplaceCall> RecordedFileReplaceCalls()
{
    HookState &state = State();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.calls;
}

}

namespace
{
std::atomic<std::uint64_t> read_file_bytes_calls{0};
}

std::uint64_t ReadFileBytesCallCount()
{
    return read_file_bytes_calls.load();
}

void ResetReadFileBytesCallCount()
{
    read_file_bytes_calls.store(0);
}

std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path)
{
    ++read_file_bytes_calls;
    std::ifstream input(file_path, std::ios::binary);
    if(!input.is_open())
    {
        throw std::runtime_error("Unable to open file: " + file_path);
    }
    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);
    if(size < 0)
    {
        throw std::runtime_error("Unable to read file size: " + file_path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(size > 0)
    {
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        if(!input)
        {
            throw std::runtime_error("Unable to read file: " + file_path);
        }
    }
    return bytes;
}

namespace
{

namespace fs = std::filesystem;
using seed::internal::ActiveFileReplaceHooks;
using seed::internal::FileReplaceHooks;
using seed::internal::RecordFileReplaceCall;

constexpr int kMaxNameAttempts = 100;
constexpr int kMoveRetries = 200;

// Random part of a working file name: 16 hex digits from the system random
// source mixed with a counter, so names are not predictable and two calls in
// one process never agree.
std::string RandomSuffix()
{
    static std::atomic<std::uint64_t> counter{0};
    static std::random_device device;
    static std::mutex lock;
    std::uint64_t value;
    {
        std::lock_guard<std::mutex> guard(lock);
        value = (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }
    value ^= (counter.fetch_add(1) + 1) * 0x9E3779B97F4A7C15ULL;
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

unsigned long ProcessId()
{
#if defined(_WIN32)
    return static_cast<unsigned long>(::GetCurrentProcessId());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

// Name of a working file beside the target: .<name>.seedtmp.<pid>.<16 hex>
fs::path WorkingName(const fs::path &target)
{
    return target.parent_path() /
           ("." + target.filename().string() + ".seedtmp." + std::to_string(ProcessId()) + "." +
            RandomSuffix());
}

// Resolves links and refuses what cannot be replaced. Returns the resolved
// target. The link itself is never replaced: the caller acts on the result.
fs::path ResolveTarget(const std::string &file_path)
{
    std::error_code error;
    const fs::file_status link_status = fs::symlink_status(file_path, error);
    const fs::path resolved = fs::canonical(file_path, error);
    if(error)
    {
        if(link_status.type() == fs::file_type::symlink)
        {
            throw std::runtime_error("Unable to replace " + file_path +
                                     ": dangling link, the file it points to does not exist");
        }
        throw std::runtime_error("Unable to replace " + file_path +
                                 ": the file does not exist (" + error.message() + ")");
    }
    const fs::file_status status = fs::status(resolved, error);
    if(error || status.type() == fs::file_type::not_found)
    {
        throw std::runtime_error("Unable to replace " + file_path + ": the file does not exist");
    }
    if(status.type() == fs::file_type::directory)
    {
        throw std::runtime_error("Unable to replace " + file_path +
                                 ": it is a folder (directory), not a regular file");
    }
    if(status.type() != fs::file_type::regular)
    {
        throw std::runtime_error("Unable to replace " + file_path +
                                 ": not a regular file (special file such as a fifo, pipe or device)");
    }
    return resolved;
}

#if defined(_WIN32)

std::string SystemMessage(DWORD code)
{
    char *text = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<char *>(&text), 0, nullptr);
    std::string message = length != 0 && text != nullptr ? std::string(text, length)
                                                         : "error " + std::to_string(code);
    if(text != nullptr)
    {
        ::LocalFree(text);
    }
    while(!message.empty() && (message.back() == '\n' || message.back() == '\r' ||
                               message.back() == ' ' || message.back() == '.'))
    {
        message.pop_back();
    }
    return message;
}

[[noreturn]] void Fail(const std::string &what, const std::string &target, DWORD code)
{
    throw std::runtime_error(what + " " + target + ": " + SystemMessage(code));
}

// Owns the working file once it exists. Closes the handle and deletes exactly
// that name on every exit except after a committed move.
class WorkingFile
{
public:
    explicit WorkingFile(fs::path path) : path(std::move(path)) {}
    WorkingFile(const WorkingFile &) = delete;
    WorkingFile &operator=(const WorkingFile &) = delete;

    ~WorkingFile()
    {
        this->Close();
        if(this->created && !this->committed)
        {
            const std::wstring wide = this->path.wstring();
            RecordFileReplaceCall("DeleteFileW", this->path.string());
            ActiveFileReplaceHooks().delete_file(wide.c_str());
        }
    }

    HANDLE Handle() const { return this->handle; }
    const fs::path &Path() const { return this->path; }
    void SetPath(fs::path next) { this->path = std::move(next); }
    void Opened(HANDLE opened)
    {
        this->handle = opened;
        this->created = true;
    }
    void Commit() { this->committed = true; }

    // Closes the handle once; returns false when the close itself failed.
    bool Close()
    {
        if(this->handle == INVALID_HANDLE_VALUE)
        {
            return true;
        }
        const HANDLE closing = this->handle;
        this->handle = INVALID_HANDLE_VALUE;
        RecordFileReplaceCall("CloseHandle");
        return ActiveFileReplaceHooks().close_handle(closing) != 0;
    }

private:
    fs::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool created = false;
    bool committed = false;
};

#else

// Owns the working file once it exists. Closes the descriptor and unlinks
// exactly that name on every exit except after a committed rename.
class WorkingFile
{
public:
    explicit WorkingFile(fs::path path) : path(std::move(path)) {}
    WorkingFile(const WorkingFile &) = delete;
    WorkingFile &operator=(const WorkingFile &) = delete;

    ~WorkingFile()
    {
        this->Close();
        if(this->created && !this->committed)
        {
            RecordFileReplaceCall("unlink", this->path.string());
            ActiveFileReplaceHooks().unlink(this->path.c_str());
        }
    }

    int Descriptor() const { return this->fd; }
    const fs::path &Path() const { return this->path; }
    void SetPath(fs::path next) { this->path = std::move(next); }
    void Opened(int opened)
    {
        this->fd = opened;
        this->created = true;
    }
    void Commit() { this->committed = true; }

    // Closes the descriptor once; returns false when the close itself failed.
    bool Close()
    {
        if(this->fd < 0)
        {
            return true;
        }
        const int closing = this->fd;
        this->fd = -1;
        RecordFileReplaceCall("close");
        return ActiveFileReplaceHooks().close(closing) == 0;
    }

private:
    fs::path path;
    int fd = -1;
    bool created = false;
    bool committed = false;
};

[[noreturn]] void Fail(const std::string &what, const std::string &target, int code)
{
    throw std::runtime_error(what + " " + target + ": " + std::strerror(code));
}

#endif

}

void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes)
{
    const fs::path target = ResolveTarget(file_path);
    const FileReplaceHooks &hooks = ActiveFileReplaceHooks();

#if defined(_WIN32)

    const std::wstring wide_target = target.wstring();
    const DWORD attributes = ::GetFileAttributesW(wide_target.c_str());
    if(attributes == INVALID_FILE_ATTRIBUTES)
    {
        Fail("Unable to read the attributes of", file_path, ::GetLastError());
    }
    if((attributes & FILE_ATTRIBUTE_READONLY) != 0)
    {
        throw std::runtime_error("Unable to replace " + file_path +
                                 ": the file is read-only; clear the read-only attribute "
                                 "(attrib -r) and try again");
    }
    constexpr DWORD kKeptAttributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                                      FILE_ATTRIBUTE_ARCHIVE |
                                      FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;

    WorkingFile working{fs::path()};
    {
        int attempt = 0;
        for(;; ++attempt)
        {
            if(attempt >= kMaxNameAttempts)
            {
                Fail("Unable to create a working file for", file_path, ERROR_FILE_EXISTS);
            }
            const fs::path name = WorkingName(target);
            const std::wstring wide = name.wstring();
            RecordFileReplaceCall("CreateFileW", name.string());
            const HANDLE handle = hooks.create_file(
                wide.c_str(), GENERIC_WRITE, 0, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
            if(handle != INVALID_HANDLE_VALUE)
            {
                working.SetPath(name);
                working.Opened(handle);
                break;
            }
            const DWORD code = ::GetLastError();
            if(code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
            {
                Fail("Unable to create a working file for", file_path, code);
            }
        }
    }

    std::size_t offset = 0;
    while(offset < bytes.size())
    {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size() - offset, 1u << 30));
        DWORD written = 0;
        RecordFileReplaceCall("WriteFile", std::string(), chunk);
        if(!hooks.write_file(working.Handle(), bytes.data() + offset, chunk, &written))
        {
            Fail("Unable to write", file_path, ::GetLastError());
        }
        offset += written;
    }

    RecordFileReplaceCall("FlushFileBuffers");
    if(!hooks.flush_file(working.Handle()))
    {
        Fail("Unable to flush", file_path, ::GetLastError());
    }
    if(!working.Close())
    {
        Fail("Unable to flush and close the working file for", file_path, ::GetLastError());
    }

    const std::wstring wide_working = working.Path().wstring();
    RecordFileReplaceCall("SetFileAttributesW", working.Path().string(), attributes & kKeptAttributes);
    if(!hooks.set_attributes(wide_working.c_str(), (attributes & kKeptAttributes) != 0
                                                       ? (attributes & kKeptAttributes)
                                                       : FILE_ATTRIBUTE_NORMAL))
    {
        Fail("Unable to set the attributes of the working file for", file_path, ::GetLastError());
    }

    // Windows refuses the move for a moment while another operation is
    // replacing or has just replaced the same file; a short bounded retry
    // lets that finish.
    for(int attempt = 0;; ++attempt)
    {
        RecordFileReplaceCall("MoveFileExW", working.Path().string());
        if(hooks.move_file(wide_working.c_str(), wide_target.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            break;
        }
        const DWORD code = ::GetLastError();
        const bool busy = code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION ||
                          code == ERROR_LOCK_VIOLATION;
        if(!busy || attempt >= kMoveRetries)
        {
            Fail("Unable to replace (move the working file over)", file_path, code);
        }
        ::Sleep(5 + 5 * static_cast<DWORD>(attempt % 8));
    }
    working.Commit();

#else

    struct stat info;
    if(::stat(target.c_str(), &info) != 0)
    {
        Fail("Unable to read the status of", file_path, errno);
    }
    const fs::path folder = target.parent_path();
    if(::access(target.c_str(), W_OK) != 0)
    {
        throw std::runtime_error("Unable to replace " + file_path +
                                 ": the file is read-only (no write permission); make it "
                                 "writable with chmod and try again");
    }
    if(::access(folder.c_str(), W_OK | X_OK) != 0)
    {
        throw std::runtime_error("Unable to replace " + file_path + ": the folder " +
                                 folder.string() +
                                 " is read-only (no write permission to the directory)");
    }

    WorkingFile working{fs::path()};
    for(int attempt = 0;; ++attempt)
    {
        if(attempt >= kMaxNameAttempts)
        {
            Fail("Unable to create a working file for", file_path, EEXIST);
        }
        const fs::path name = WorkingName(target);
        RecordFileReplaceCall("open", name.string(), 0);
        const int fd = hooks.open(name.c_str(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0);
        if(fd >= 0)
        {
            working.SetPath(name);
            working.Opened(fd);
            break;
        }
        if(errno != EEXIST)
        {
            Fail("Unable to create a working file for", file_path, errno);
        }
    }

    std::size_t offset = 0;
    while(offset < bytes.size())
    {
        RecordFileReplaceCall("write", std::string(), bytes.size() - offset);
        const ssize_t result =
            hooks.write(working.Descriptor(), bytes.data() + offset, bytes.size() - offset);
        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            Fail("Unable to write", file_path, errno);
        }
        if(result == 0)
        {
            Fail("Unable to write", file_path, ENOSPC);
        }
        offset += static_cast<std::size_t>(result);
    }

    // Ownership first (it clears setuid and setgid), best effort; then the
    // mode, which is never wider than the original's.
    RecordFileReplaceCall("fchown");
    (void)hooks.fchown(working.Descriptor(), info.st_uid, info.st_gid);
    const mode_t mode = info.st_mode & 07777;
    RecordFileReplaceCall("fchmod", std::string(), mode);
    if(hooks.fchmod(working.Descriptor(), mode) != 0)
    {
        Fail("Unable to set the permissions (chmod) of the working file for", file_path, errno);
    }

    RecordFileReplaceCall("fsync");
    if(hooks.fsync(working.Descriptor()) != 0)
    {
        Fail("Unable to flush (sync)", file_path, errno);
    }
    if(!working.Close())
    {
        Fail("Unable to flush (close the working file for)", file_path, errno);
    }

    RecordFileReplaceCall("rename", working.Path().string());
    if(hooks.rename(working.Path().c_str(), target.c_str()) != 0)
    {
        Fail("Unable to rename the working file over (replace)", file_path, errno);
    }
    working.Commit();

    // Best effort: a failure here does not undo or fail the replacement.
    RecordFileReplaceCall("fsync_folder", folder.string());
    (void)hooks.fsync_folder(folder.c_str());

#endif
}
