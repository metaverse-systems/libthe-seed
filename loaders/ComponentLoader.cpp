#include "ComponentLoader.hpp"
#include <iostream>

namespace ComponentLoader
{
    Loader::Loader(std::string library)
    {
        try
        {
            this->library = new LibraryLoader(library);
            this->library->PathAdd(".");
            this->library->PathAdd("../components/" + library + "/src/.libs/");
            this->library->PathAdd("../../../components/" + library + "/src/.libs/");
        }
        catch(std::string e)
        {
            std::cout << "Exception in ComponentLoader::Loader::Loader()" << e << std::endl;
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
        catch(std::string e)
        {
            std::cout << "ComponentLoader::Loader(\"" + this->library->name + "\")::ComponentCreate(&data): Couldn't find create_component in library.";
            std::cout << e << std::endl;
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
            catch(std::string e)
            {
                std::cout << e << std::endl;
                exit(1);
            }
        }

        ecs::Component *com = nullptr;
        try
        {
            com = component_loaders[component]->ComponentCreate();
        }
        catch(std::string e)
        {
            std::cout << e << std::endl;
            exit(1);
        }
 
        return com;
    }

    ecs::Component *Create(std::string component, void *data)
    {
        if(!component_loaders[component])
        {
            try
            {
                component_loaders[component] = new Loader(component);
            }
            catch(std::string e)
            {
                std::cout << e << std::endl;
                exit(1);
            }
        }

        ecs::Component *com = nullptr;
        try
        {
            com = component_loaders[component]->ComponentCreate(data);
        }
        catch(std::string e)
        {
            std::cout << e << std::endl;
            exit(1);
        }

        return com;
    }

    ComponentCreator Get(std::string component)
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
}