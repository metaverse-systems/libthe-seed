#pragma once

#include <string>
#include <vector>
#include <memory>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

struct LibraryDeleter
{
    void operator()(void *ptr) const
    {
        if(ptr)
        {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(ptr));
#else
            dlclose(ptr);
#endif
        }
    }
};

/**
 * @brief Loads functions from dynamic libraries.
 *        (.so, .dll, .dylib)
 * 
 */
class LibraryLoader
{
  public:
    /**
     * @brief Construct a new Library Loader object.
     * 
     * @param library The name of the library to load.
     */
    LibraryLoader(std::string library): name(library) {};
    /**
     * @brief Get pointer to function in library.
     * 
     * @param FuncName The name of the function to load.
     * @return void* A pointer to the function.
     */
    void *FunctionGet(std::string FunctionName);
    /**
     * @brief Adds a path to the library path search list.
     * 
     * @param path Path to add.
     */
    void PathAdd(std::string path);
    const std::vector<std::string> PathsGet();
    const std::string name;
  private:
    std::unique_ptr<void, LibraryDeleter> library_handle;
    void Load();
    std::vector<std::string> paths;
};
