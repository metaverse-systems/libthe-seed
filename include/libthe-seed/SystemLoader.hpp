#pragma once

#include <string>
#include "LibraryLoader.hpp"
#include <libecs-cpp/ecs.hpp>

using SystemCreator = ecs::System *(*)(void *);

namespace SystemLoader
{
    class Loader
    {
      public:
        Loader(std::string library);
        ~Loader();
        ecs::System *SystemCreate(void *data);
        SystemCreator SystemCreatorGet();
      private:
        LibraryLoader *library;
    };

    extern std::map<std::string, SystemLoader::Loader *> system_loaders;
    extern std::vector<std::string> system_paths;

    ecs::System *Create(std::string system);
    ecs::System *Create(std::string system, void *data);
    SystemCreator CreatorGet(std::string system);

    std::vector<std::string> PathsGet();
    void PathAdd(std::string);
}
