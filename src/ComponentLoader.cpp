#include <libthe-seed/ComponentLoader.hpp>
#include "NameParser.hpp"
#include <iostream>

namespace ComponentLoader
{
    std::vector<std::string> component_paths;

    Loader::Loader(std::string library)
    {
        auto name = NameParser(library);

        try
        {
            this->library = new LibraryLoader(name.library);
            this->library->PathAdd(".");
            this->library->PathAdd("../../" + name.library + "/src/.libs/");
            if(name.org.size())
            {
                auto path = "../node_modules/" + name.org + "/" + name.library + "/src/.libs";
                this->library->PathAdd(path);
            }

            for(auto path : component_paths) this->library->PathAdd(path);
        }
        catch(std::runtime_error e)
        {
            throw e;
        }
    }

    ComponentCreator Loader::ComponentGet()
    {
        void *ptr = this->library->FunctionGet("create_component");
        ComponentCreator creator = reinterpret_cast<ComponentCreator>(ptr);
        return creator;
    }

    ecs::Component *Loader::ComponentCreate()
    {
        void *ptr = this->library->FunctionGet("create_component");
        ComponentCreator creator = reinterpret_cast<ComponentCreator>(ptr);
        return creator(nullptr);
    }

    ecs::Component *Loader::ComponentCreate(void *data)
    {
        void *ptr;

        try
        {
            ptr = this->library->FunctionGet("create_component");
        }
        catch(std::runtime_error e)
        {
            throw e;
        }

        ComponentCreator creator = reinterpret_cast<ComponentCreator>(ptr);
        return creator(data);
    }

    std::map<std::string, ComponentLoader::Loader *> component_loaders;

    ecs::Component *Create(std::string component)
    {
        if(!component_loaders[component]) 
        {
            try
            {
                component_loaders[component] = new Loader(component);
            }
            catch(std::runtime_error e)
            {
                throw e;
            }
        }

        try
        {
            return component_loaders[component]->ComponentCreate();
        }
        catch(std::runtime_error e)
        {
            throw e;
        }
    }

    ecs::Component *Create(std::string component, void *data)
    {
        if(!component_loaders[component])
        {
            try
            {
                component_loaders[component] = new Loader(component);
            }
            catch(std::runtime_error e)
            {
                throw e;
            }
        }

        try
        {
            return component_loaders[component]->ComponentCreate(data);
        }
        catch(std::runtime_error e)
        {
            throw e;
        }
    }

    ComponentCreator Get(std::string component)
    {
        if(!component_loaders[component])
        {
            try
            {
                component_loaders[component] = new Loader(component);
            }
            catch(std::runtime_error e)
            {
                throw e;
            }
        }

        return component_loaders[component]->ComponentGet();
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
