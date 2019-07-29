#pragma once

#include <string>
#include "LibraryLoader.hpp"
#include <libecs-cpp/ecs.hpp>

using ComponentCreator = std::shared_ptr<ecs::Component> (*)(void *);

namespace ComponentLoader
{
    class Loader
    {
      public:
        Loader(std::string library);
        ~Loader();
        std::shared_ptr<ecs::Component> ComponentCreate();
        std::shared_ptr<ecs::Component> ComponentCreate(void *data);
        ComponentCreator ComponentGet();
      private:
        LibraryLoader *library;
    };

    extern std::map<std::string, ComponentLoader::Loader *> component_loaders;

    std::shared_ptr<ecs::Component> Create(std::string component);
    std::shared_ptr<ecs::Component> Create(std::string component, void *data);
    ComponentCreator Get(std::string);
}
