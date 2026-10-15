#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include "PluginSearch.hpp"

#include <atomic>
#include <cwctype>
#include <string>
#include <system_error>
#include <utility>

namespace seed::internal
{

namespace
{
std::atomic<std::uint64_t> open_calls{0};

#ifdef _WIN32
std::wstring PathKey(const std::wstring &path)
{
    std::wstring key = path;
    for(wchar_t &c : key)
    {
        if(c == L'/') c = L'\\';
        c = static_cast<wchar_t>(::towlower(c));
    }
    return key;
}

#endif
} // namespace

#ifdef _WIN32
std::string PlatformMessage(DWORD id)
{
    if(id == 0) return {};

    LPSTR buffer = nullptr;
    DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message;
    if(buffer != nullptr)
    {
        message.assign(buffer, size);
        LocalFree(buffer);
    }
    while(!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
    {
        message.pop_back();
    }
    return message;
}
#endif

void *OpenPinned(const std::filesystem::path &absolute_path, std::string &error)
{
    open_calls.fetch_add(1, std::memory_order_relaxed);
    error.clear();

#ifdef _WIN32
    DWORD previous_mode = 0;
    bool mode_set = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous_mode) != 0;

    HMODULE module = LoadLibraryExW(absolute_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    DWORD load_error = module == nullptr ? GetLastError() : 0;
    if(module == nullptr)
    {
        error = PlatformMessage(load_error);
    }
    else
    {
        // The platform returns a module of the same name that is already
        // loaded, wherever it came from; the file that was asked for must be
        // the one that is mapped.
        std::wstring loaded(32768, L'\0');
        DWORD length = GetModuleFileNameW(module, loaded.data(), static_cast<DWORD>(loaded.size()));
        loaded.resize(length);
        if(length != 0 && PathKey(loaded) != PathKey(absolute_path.wstring()))
        {
            error = "a different module with the same name is already loaded from " + std::filesystem::path(loaded).string();
            FreeLibrary(module);
            module = nullptr;
        }
    }

    if(module != nullptr)
    {
        HMODULE pinned = nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               reinterpret_cast<LPCWSTR>(module), &pinned))
        {
            error = "could not pin the module: " + PlatformMessage(GetLastError());
            FreeLibrary(module);
            module = nullptr;
        }
    }

    if(mode_set)
    {
        SetThreadErrorMode(previous_mode, nullptr);
    }
    return module;
#else
    dlerror();
    void *handle = dlopen(absolute_path.c_str(), RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    if(handle == nullptr)
    {
        const char *message = dlerror();
        error = message != nullptr ? message : "";
    }
    return handle;
#endif
}

std::uint64_t PluginOpenCallCount()
{
    return open_calls.load(std::memory_order_relaxed);
}

void ResetPluginOpenCallCount()
{
    open_calls.store(0, std::memory_order_relaxed);
}

} // namespace seed::internal

namespace seed::internal
{

std::string PluginFileName(const std::string &library)
{
#ifdef _WIN32
    return "lib" + library + "-0.dll";
#elif defined(__APPLE__)
    return "lib" + library + ".dylib";
#else
    return "lib" + library + ".so";
#endif
}

std::string PakFileName(const std::string &library)
{
    return library + ".pak";
}

std::vector<std::string> DevelopmentLocations(const std::string &org, const std::string &library, bool pak)
{
    std::vector<std::string> result;
    const std::string suffix = pak ? "" : "/src/.libs";
    result.push_back("../../" + library + suffix);
    if(!org.empty())
    {
        result.push_back("../node_modules/" + org + "/" + library + suffix);
    }
    return result;
}

std::vector<std::string> SearchListBuild(const std::vector<std::string> &configured, const std::string &org,
                                         const std::string &library, bool pak, size_t &configured_count)
{
    std::vector<std::string> list = configured;
    configured_count = list.size();
    for(std::string &location : DevelopmentLocations(org, library, pak))
    {
        list.push_back(std::move(location));
    }
    return list;
}

SearchResult SearchFirst(const std::vector<std::string> &locations, const std::string &file_name, size_t configured_count)
{
    namespace fs = std::filesystem;

    SearchResult result;
    for(size_t i = 0; i < locations.size(); ++i)
    {
        result.locations.push_back({locations[i], i >= configured_count, LoadError::LocationState::NotReached});
    }

    // A path that is absent, or has a file where a folder should be, is
    // simply not there. Any other failure to examine it keeps the system's
    // text and the search goes on with the next location.
    auto absent = [](const std::error_code &code) {
        return code == std::errc::no_such_file_or_directory || code == std::errc::not_a_directory;
    };

    for(size_t i = 0; i < locations.size(); ++i)
    {
        LoadError::Location &entry = result.locations[i];
        std::error_code ec;
        fs::path directory = fs::absolute(fs::path(locations[i]), ec);
        fs::file_status directory_status;
        if(!ec)
        {
            directory_status = fs::status(directory, ec);
        }
        if(ec && !absent(ec))
        {
            entry.state = LoadError::LocationState::Unreadable;
            entry.reason = ec.message();
            continue;
        }
        if(ec || !fs::is_directory(directory_status))
        {
            entry.state = LoadError::LocationState::Missing;
            continue;
        }

        fs::path candidate = directory / file_name;
        std::error_code file_ec;
        fs::file_status file_status = fs::status(candidate, file_ec);
        if(file_ec && !absent(file_ec))
        {
            entry.state = LoadError::LocationState::Unreadable;
            entry.reason = file_ec.message();
            continue;
        }
        if(file_ec || !fs::is_regular_file(file_status))
        {
            entry.state = LoadError::LocationState::Searched;
            continue;
        }

        entry.state = LoadError::LocationState::Found;
        result.found = true;
        result.file = candidate.lexically_normal();
        std::error_code canonical_ec;
        result.identity = fs::canonical(candidate, canonical_ec);
        if(canonical_ec)
        {
            result.identity = result.file;
        }
        break;
    }
    return result;
}

void NotFoundThrow(const SearchResult &result, const std::string &name,
                   const std::string &file_name, const std::string &kind)
{
    throw LoadError(LoadError::Reason::NotFound, name, file_name, result.locations, "", kind);
}

void NotLoadableThrow(const SearchResult &result, const std::string &name,
                      const std::string &reason, const std::string &kind)
{
    throw LoadError(LoadError::Reason::NotLoadable, name, result.file.string(), result.locations, reason, kind);
}

void *PluginOpen(const std::vector<std::string> &locations, size_t configured_count, const std::string &library,
                 const std::string &name, const std::string &kind, SearchResult &result)
{
    const std::string file_name = PluginFileName(library);
    result = SearchFirst(locations, file_name, configured_count);
    if(!result.found)
    {
        NotFoundThrow(result, name, file_name, kind);
    }

    std::string error;
    void *handle = OpenPinned(result.file, error);
    if(handle == nullptr)
    {
        NotLoadableThrow(result, name, error, kind);
    }
    return handle;
}

void EntryPointMissingThrow(const std::filesystem::path &file, const std::string &name,
                            const std::string &symbol, const std::string &reason, const std::string &kind)
{
    throw LoadError(LoadError::Reason::EntryPointMissing, name, file.string(), {},
                    reason.empty() ? symbol : symbol + ": " + reason, kind);
}

void NoObjectThrow(const std::filesystem::path &file, const std::string &name,
                   const std::string &symbol, const std::string &kind)
{
    throw LoadError(LoadError::Reason::NoObject, name, file.string(), {}, symbol, kind);
}

void *SymbolFind(void *handle, const std::string &symbol, std::string &error)
{
    error.clear();
#ifdef _WIN32
    void *address = reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), symbol.c_str()));
    if(address == nullptr)
    {
        error = PlatformMessage(GetLastError());
    }
#else
    dlerror();
    void *address = dlsym(handle, symbol.c_str());
    if(address == nullptr)
    {
        const char *message = dlerror();
        error = message != nullptr ? message : "";
    }
#endif
    return address;
}

} // namespace seed::internal
