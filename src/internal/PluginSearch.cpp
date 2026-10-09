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

namespace seed::internal
{

namespace
{
std::atomic<std::uint64_t> open_calls{0};

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
} // namespace

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
