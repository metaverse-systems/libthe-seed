#pragma once

#include <string>
#include <vector>

class LibraryLoader
{
  public:
    LibraryLoader(std::string library);
    ~LibraryLoader();
    void *FunctionGet(std::string FuncName);
    void PathAdd(std::string path);
  private:
    void *dl_handle = nullptr;
    void Load();
    std::vector<std::string> PathsGet();
    std::string name;
    std::vector<std::string> paths;
};
