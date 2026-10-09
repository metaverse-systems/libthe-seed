#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <libthe-seed/LibraryLoader.hpp>

#include "internal/PluginSearch.hpp"

#include <libthe-seed/LoadError.hpp>


void LibraryLoader::PathAdd(const std::string &path)
{
    this->paths.push_back(path);
}

std::vector<std::string> LibraryLoader::PathsGet()
{
    return this->paths;
}

void LibraryLoader::Load()
{
    if(this->library_handle != nullptr) return;

    // Names that could address anything but a plain file below a location.
    if(this->name.find_first_of("/\\:") != std::string::npos)
    {
        throw LoadError(LoadError::Reason::InvalidName, this->name, "", {},
                        "contains \"/\", a backslash or \":\"", "library");
    }

    // Plugins are pinned: the platform never unmaps their code, so objects
    // and function pointers stay valid after this loader is gone. Only the
    // added locations are searched and the first one holding the file decides.
    seed::internal::SearchResult search;
    void *lib = seed::internal::PluginOpen(this->paths, this->paths.size(), this->name, this->name, "library", search);
    this->library_handle.reset(lib);
    this->loaded_file = search.file.string();
}

void *LibraryLoader::FunctionGet(const std::string &FunctionName)
{
    this->Load();

    std::string error;
    void *ptr = seed::internal::SymbolFind(this->library_handle.get(), FunctionName, error);
    if(!ptr)
    {
        seed::internal::EntryPointMissingThrow(this->loaded_file, this->name, FunctionName, error, "library");
    }
    return ptr;
}

const std::string LibraryLoader::GetLastErrorAsString()
{
#ifdef _WIN32
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0) {
        return {}; // No error message has been recorded
    }
    
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
#else
    const char *errorMessage = dlerror();
    if (errorMessage == nullptr) {
        return {}; // No error message has been recorded
    }
    return std::string(errorMessage);
#endif
}
