#pragma once

#include <string>
#include <vector>

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
    ~LibraryLoader();
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
    const std::string name;
  private:
    void *library_handle = nullptr;
    void Load();
    std::vector<std::string> PathsGet();
    std::vector<std::string> paths;
};
