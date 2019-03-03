#pragma once

#include <string>
#include "LibraryLoader.hpp"
#include <libecs-cpp/ecs.hpp>

using ComponentCreator = ecs::Component *(*)(void *);

namespace ComponentLoader
{
    class Loader
    {
      public:
        Loader(std::string library);
        ~Loader();
        ecs::Component *ComponentGet();
        ecs::Component *ComponentGet(void *data);
      private:
        LibraryLoader *library;
    };

    extern std::map<std::string, ComponentLoader::Loader *> component_loaders;

    ecs::Component *Get(std::string component);
    ecs::Component *Get(std::string component, void *data);
}
