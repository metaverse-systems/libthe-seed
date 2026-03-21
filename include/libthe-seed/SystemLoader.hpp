#pragma once

#include <string>
#include <memory>
#include "LibraryLoader.hpp"
#include <libecs-cpp/ecs.hpp>

namespace SystemLoader
{
    using SystemCreator = ecs::System *(*)(void *);
    class Loader
    {
      public:
        Loader(const std::string &library);
        ecs::System *Create(void *data);
        SystemCreator Get();
      private:
        std::unique_ptr<LibraryLoader> library;
        SystemCreator cached_creator = nullptr;
    };

    extern std::map<std::string, std::unique_ptr<SystemLoader::Loader>> system_loaders;
    extern std::vector<std::string> system_paths;

    std::unique_ptr<ecs::System> Create(const std::string &system);
    std::unique_ptr<ecs::System> Create(const std::string &system, void *data);
    SystemCreator Get(const std::string &system);

    std::vector<std::string> PathsGet();
    void PathAdd(const std::string &path);
}
