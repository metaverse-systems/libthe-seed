#pragma once

#include <string>
#include "LibraryLoader.hpp"
#include <libecs-cpp/ecs.hpp>

namespace ComponentLoader
{
    using ComponentCreator = ecs::Component *(*)(void *);
    class Loader
    {
      public:
        Loader(const std::string &library);
        ecs::Component *Create();
        ecs::Component *Create(void *data);
        ComponentCreator Get();
      private:
        std::unique_ptr<LibraryLoader> library;
    };

    extern std::map<std::string, std::unique_ptr<ComponentLoader::Loader>> component_loaders;
    extern std::vector<std::string> component_paths;

    ecs::Component *Create(const std::string &component);
    ecs::Component *Create(const std::string &component, void *data);
    ComponentCreator Get(const std::string &component);

    std::vector<std::string> PathsGet();
    void PathAdd(const std::string &path);
}
