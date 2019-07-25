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
    std::string name;
  private:
    void *dl_handle = nullptr;
    void Load();
    std::vector<std::string> PathsGet();
    std::vector<std::string> paths;
};
