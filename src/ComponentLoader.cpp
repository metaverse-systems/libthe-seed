#include <libthe-seed/ComponentLoader.hpp>
#include <iostream>

namespace ComponentLoader
{
    std::vector<std::string> component_paths;

    Loader::Loader(std::string library)
    {
        std::string org;

        if(library.find("/") != std::string::npos)
        {
            std::vector<std::string> name;
            std::string temp;
            for(size_t counter = 0; counter < library.size(); counter++)
            {
                if(library[counter] == '/')
                {
                    name.push_back(temp);
                    temp = "";
                    continue;
                }

                temp += library[counter];
            }

            name.push_back(temp);
            org = name[0];
            library = name[1];
        }

        try
        {
            this->library = new LibraryLoader(library);
            this->library->PathAdd(".");
            this->library->PathAdd("../../" + library + "/src/.libs/");
            if(org.size())
            {
                auto path = "../node_modules/" + org + "/" + library + "/src/.libs";
                this->library->PathAdd(path);
            }

            for(auto path : component_paths) this->library->PathAdd(path);
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

        try
        {
            return component_loaders[component]->ComponentCreate();
        }
        catch(std::string e)
        {
            std::cout << e << std::endl;
            exit(1);
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
            catch(std::string e)
            {
                std::cout << e << std::endl;
                exit(1);
            }
        }

        try
        {
            return component_loaders[component]->ComponentCreate(data);
        }
        catch(std::string e)
        {
            std::cout << e << std::endl;
            exit(1);
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
            catch(std::string e)
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
