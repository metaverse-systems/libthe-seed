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
        ecs::System *SystemGet();
        ecs::System *SystemGet(void *data);
      private:
        LibraryLoader *library;
    };

    extern std::map<std::string, SystemLoader::Loader *> system_loaders;

    ecs::System *Get(std::string system);
    ecs::System *Get(std::string system, void *data);
}
