#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <libthe-seed/LibraryLoader.hpp>
#include <fstream>

void LibraryLoader::PathAdd(std::string path)
{
    this->paths.push_back(path);
}

const std::vector<std::string> LibraryLoader::PathsGet()
{
    std::vector<std::string> valid_paths;

    for(const auto &path : this->paths)
    {
#ifdef _WIN32
        std::string full_path = path + "\\lib" + this->name + "-0.dll";
#elif __APPLE__
        std::string full_path = path + "/lib" + this->name + ".dylib";
#else
        std::string full_path = path + "/lib" + this->name + ".so";
#endif

        std::ifstream test_path(full_path);
        if(test_path.is_open()) 
        {
            valid_paths.push_back(full_path);
            test_path.close();
        }
    }

    if(valid_paths.size() == 0)
    {
        std::string error = "Could not find " + this->name + " shared object in the following paths:\n";
        for(const auto &path : this->paths)
        {
            error += path + "\n";
        }
        throw std::runtime_error(error);
    }

    return valid_paths;
}

void LibraryLoader::Load()
{
    if(this->library_handle != nullptr) return;
    std::string error;
    std::vector<std::string> search_paths = this->PathsGet();

    for(const auto &path : search_paths)
    {
#ifdef _WIN32
        HMODULE lib = LoadLibrary(path.c_str());
#else
        void *lib = dlopen(path.c_str(), RTLD_LAZY);
#endif
        if(lib == nullptr)
        {
            error = LibraryLoader::GetLastErrorAsString();
        }
        else
        {
            this->library_handle.reset(lib);
            break;
        }
    }

    if(this->library_handle == nullptr)
    {
        throw std::runtime_error(error);
    }
}

void *LibraryLoader::FunctionGet(std::string FunctionName)
{
    this->Load();

    void *ptr = nullptr;
    std::string error;

#ifdef _WIN32
    ptr = (void *)GetProcAddress((HMODULE)this->library_handle.get(), FunctionName.c_str());
#else
    ptr = dlsym(this->library_handle.get(), FunctionName.c_str());
#endif

    if(!ptr)
    {
        throw std::runtime_error(LibraryLoader::GetLastErrorAsString());
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
