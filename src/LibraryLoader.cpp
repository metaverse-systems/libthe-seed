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
        this->library_handle = LoadLibrary(path.c_str());
        if(this->library_handle == nullptr)
        {
            DWORD result = GetLastError();
            LPSTR message_buffer = nullptr;
            size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
            error = std::string(message_buffer);
            LocalFree(message_buffer);
        }
        else break;
#else
        dlerror(); // Clear dlerror value
        this->library_handle = dlopen(path.c_str(), RTLD_LAZY);
        if(this->library_handle == nullptr) 
        {
            error = std::string(dlerror());
        }
        else break;
#endif
    }

    if(this->library_handle == nullptr)
    {
        throw std::runtime_error(error);
    }
}

LibraryLoader::~LibraryLoader()
{
#ifdef _WIN32
    FreeLibrary((HMODULE)this->library_handle);
#else
    dlclose(this->library_handle);
#endif
}

void *LibraryLoader::FunctionGet(std::string FunctionName)
{
    this->Load();

    void *ptr = nullptr;
    std::string error;

#ifdef _WIN32
    ptr = (void *)GetProcAddress((HMODULE)this->library_handle, FunctionName.c_str());
    if(!ptr)
    {
        DWORD result = GetLastError();
        LPSTR message_buffer = nullptr;
        size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
        error = std::string(message_buffer);
        LocalFree(message_buffer);
    }
#else
    ptr = dlsym(this->library_handle, FunctionName.c_str());
    if(!ptr) error = std::string(dlerror());
#endif

    if(!ptr)
    {
        throw std::runtime_error(error);
    }
    return ptr;
}
