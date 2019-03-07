#include "ComponentLoader.hpp"
#include <iostream>

namespace ComponentLoader
{
    Loader::Loader(std::string library)
    {
        try
        {
            this->library = new LibraryLoader(library);
            this->library->PathAdd("../components/" + library + "/src/.libs/");
        }
        catch(std::string e)
        {
            throw e;
        }
    }

    ecs::Component *Loader::ComponentGet()
    {
        void *ptr = this->library->FunctionGet("create_component");
        ComponentCreator creator = reinterpret_cast<ComponentCreator>(ptr);
        return creator(nullptr);
    }

    ecs::Component *Loader::ComponentGet(void *data)
    {
        void *ptr;

        try
        {
            ptr = this->library->FunctionGet("create_component");
        }
        catch(std::string e)
        {
            throw e;
        }

        ComponentCreator creator = reinterpret_cast<ComponentCreator>(ptr);
        return creator(data);
    }

    std::map<std::string, ComponentLoader::Loader *> component_loaders;

    ecs::Component *Get(std::string component)
    {
        if(!component_loaders[component]) 
        {
            try
            {
                component_loaders[component] = new Loader(component);
            }
            catch(std::string e)
            {
                throw e;
            }
        }

        return component_loaders[component]->ComponentGet();
    }

    ecs::Component *Get(std::string component, void *data)
    {
        if(!component_loaders[component])
        {
            try
            {
                component_loaders[component] = new Loader(component);
            }
            catch(std::string e)
            {
                throw e;
            }
        }

        return component_loaders[component]->ComponentGet(data);
    }
}
