#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <libthe-seed/LibraryLoader.hpp>
#include <fstream>

LibraryLoader::LibraryLoader(std::string library)
{
    this->name = library;
}

void LibraryLoader::PathAdd(std::string path)
{
    this->paths.push_back(path);
}

std::vector<std::string> LibraryLoader::PathsGet()
{
    std::vector<std::string> valid_paths;

    for(auto &p : this->paths)
    {
#ifdef _WIN32
        std::string full_path = p + "\\lib" + this->name + "-0.dll";
#elif __APPLE__
        std::string full_path = p + "/lib" + this->name + ".dylib";
#else
        std::string full_path = p + "/lib" + this->name + ".so";
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
        for(auto &p : this->paths)
        {
            error += p + "\n";
        }
        throw std::runtime_error(error);
    }

    return valid_paths;
}

void LibraryLoader::Load()
{
    if(this->dl_handle != nullptr) return;
    std::string error;
    std::vector<std::string> search_paths = this->PathsGet();

    for(auto &path : search_paths)
    {
#ifdef _WIN32
        this->dl_handle = LoadLibrary(path.c_str());
        if(this->dl_handle == nullptr)
        {
            DWORD result = GetLastError();
            LPSTR messageBuffer = nullptr;
            size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
            error = std::string(messageBuffer);
            LocalFree(messageBuffer);
        }
        else break;
#else
        dlerror();
        this->dl_handle = dlopen(path.c_str(), RTLD_LAZY);
        if(this->dl_handle == nullptr) 
        {
            error = std::string(dlerror());
        }
        else break;
#endif
    }

    if(this->dl_handle == nullptr)
    {
        throw std::runtime_error(error);
    }
}

LibraryLoader::~LibraryLoader()
{
#ifdef _WIN32
    FreeLibrary((HMODULE)this->dl_handle);
#else
    dlclose(this->dl_handle);
#endif
}

void *LibraryLoader::FunctionGet(std::string FuncName)
{
    this->Load();

    void *ptr = nullptr;
    std::string error;

#ifdef _WIN32
    ptr = (void *)GetProcAddress((HMODULE)this->dl_handle, FuncName.c_str());
    if(!ptr)
    {
        DWORD result = GetLastError();
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
        error = std::string(messageBuffer);
        LocalFree(messageBuffer);
    }
#else
    ptr = dlsym(this->dl_handle, FuncName.c_str());
    if(!ptr) error = std::string(dlerror());
#endif

    if(!ptr)
    {
        throw std::runtime_error(error);
    }
    return ptr;
}
