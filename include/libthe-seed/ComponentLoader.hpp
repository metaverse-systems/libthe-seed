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
        ecs::Component *ComponentCreate();
        ecs::Component *ComponentCreate(void *data);
        ComponentCreator ComponentGet();
      private:
        std::unique_ptr<LibraryLoader> library;
    };

    extern std::map<std::string, std::unique_ptr<ComponentLoader::Loader>> component_loaders;
    extern std::vector<std::string> component_paths;

    ecs::Component *Create(std::string component);
    ecs::Component *Create(std::string component, void *data);
    ComponentCreator Get(std::string);

    std::vector<std::string> PathsGet();
    void PathAdd(std::string);
}
