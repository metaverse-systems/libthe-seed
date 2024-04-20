#include <libthe-seed/ComponentLoader.hpp>
#include "NameParser.hpp"
#include <iostream>

namespace ComponentLoader
{
    std::vector<std::string> component_paths;

    Loader::Loader(std::string library)
    {
        auto name = NameParser(library);
        this->library = std::make_unique<LibraryLoader>(name.library);

          this->library->PathAdd(".");
          this->library->PathAdd("../../" + name.library + "/src/.libs/");
          if(!name.org.empty())
          {
              auto path = "../node_modules/" + name.org + "/" + name.library + "/src/.libs";
              this->library->PathAdd(path);
          }

          for(auto path : component_paths) this->library->PathAdd(path);
    }

    ComponentCreator Loader::ComponentGet()
    {
        void *ptr = this->library->FunctionGet("create_component");
        return reinterpret_cast<ComponentCreator>(ptr);
    }

    ecs::Component *Loader::ComponentCreate()
    {
        return this->ComponentCreate(nullptr);
    }

    ecs::Component *Loader::ComponentCreate(void *data)
    {
        auto creator = this->ComponentGet();
        return creator(data);
    }

    std::map<std::string, std::unique_ptr<ComponentLoader::Loader>> component_loaders;

    ecs::Component *Create(const std::string &component)
    {
        auto &loader = component_loaders[component];
        if(!loader) 
        {
            loader = std::make_unique<Loader>(component);
        }

        return loader->ComponentCreate();
    }

    ecs::Component *Create(std::string component, void *data)
    {
        auto &loader = component_loaders[component];
        if(!loader)
        {   
            loader = std::make_unique<Loader>(component);
        }   

        return loader->ComponentCreate(data);
    }

    ComponentCreator Get(std::string component)
    {
        auto &loader = component_loaders[component];
        if(!loader)
        {
            loader = std::make_unique<Loader>(component);
        }

        return loader->ComponentGet();
    }

    std::vector<std::string> PathsGet()
    {
        return component_paths;
    }

    void PathAdd(std::string path)
    {
        component_paths.push_back(path);
    }
}
