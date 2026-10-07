#include "FileIO.hpp"
#include "FileReplaceHooks.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
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

std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path)
{
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

void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes)
{
    const auto temp_path = file_path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary);
        if(!output.is_open())
        {
            throw std::runtime_error("Unable to create temp file: " + temp_path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        if(!output)
        {
            throw std::runtime_error("Unable to write temp file: " + temp_path);
        }
    }
    try
    {
        std::filesystem::rename(temp_path, file_path);
    }
    catch(const std::filesystem::filesystem_error &e)
    {
        std::filesystem::remove(temp_path);
        throw std::runtime_error("Unable to rename temp file to " + file_path + ": " + e.what());
    }
}
