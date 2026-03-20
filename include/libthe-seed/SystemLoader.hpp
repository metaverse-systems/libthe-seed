#pragma once

#include <string>
#include <memory>
#include "LibraryLoader.hpp"
#include <libecs-cpp/ecs.hpp>

using SystemCreator = ecs::System *(*)(void *);

namespace SystemLoader
{
    class Loader
    {
      public:
        Loader(std::string library);
        ecs::System *SystemCreate(void *data);
        SystemCreator SystemCreatorGet();
      private:
        std::unique_ptr<LibraryLoader> library;
    };

    extern std::map<std::string, std::unique_ptr<SystemLoader::Loader>> system_loaders;
    extern std::vector<std::string> system_paths;

    std::unique_ptr<ecs::System> Create(std::string system);
    std::unique_ptr<ecs::System> Create(std::string system, void *data);
    SystemCreator CreatorGet(std::string system);

    std::vector<std::string> PathsGet();
    void PathAdd(std::string);
}
