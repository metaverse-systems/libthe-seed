#include "SystemLoader.hpp"
#include <iostream>

namespace SystemLoader
{
    Loader::Loader(std::string library)
    {
        try
        {
            this->library = new LibraryLoader(library);
            this->library->PathAdd("./");
            this->library->PathAdd("../systems/" + library + "/src/.libs/");
        }
        catch(std::string e)
        {
std::cout << "Error in SystemLoader::Loader::Loader()" << std::endl;
            throw e;
        }
    }

    SystemCreator Loader::SystemGet()
    {
        void *ptr = this->library->FunctionGet("create_system");
        SystemCreator creator = reinterpret_cast<SystemCreator>(ptr);
        return creator;
    }

    ecs::System *Loader::SystemCreate()
    {
        void *ptr = this->library->FunctionGet("create_system");
        SystemCreator creator = reinterpret_cast<SystemCreator>(ptr);
        ecs::System *s;
        try
        {
            s = creator(nullptr);
        }
        catch(std::string e)
        {
            std::cout << "Error in SystemLoader::Loader::SystemCreate()" << std::endl;
            throw e;
        }

        return s;
    }

    ecs::System *Loader::SystemCreate(void *data)
    {
        void *ptr;

        try
        {
            ptr = this->library->FunctionGet("create_system");
        }
        catch(std::string e)
        {
            std::cout << "Couldn't get create_system function. " << e << std::endl;
            throw e;
        }

        SystemCreator creator = reinterpret_cast<SystemCreator>(ptr);
        ecs::System *s;
        try
        {
            s = creator(data);
        }
        catch(std::string e)
        {
            std::cout << "Error in SystemLoader::Loader::SystemCreate(void *). " << e << std::endl;
            throw e;
        }
        return s;
    }

    std::map<std::string, SystemLoader::Loader *> system_loaders;

    ecs::System *Create(std::string system)
    {
        if(!system_loaders[system]) 
        {
            try
            {
                system_loaders[system] = new Loader(system);
            }
            catch(std::string e)
            {
                throw e;
            }
        }

        return system_loaders[system]->SystemCreate();
    }

    ecs::System *Create(std::string system, void *data)
    {
        if(!system_loaders[system])
        {
            try
            {
                system_loaders[system] = new Loader(system);
            }
            catch(std::string e)
            {
                throw e;
            }
        }

        return system_loaders[system]->SystemCreate(data);
    }

    SystemCreator Get(std::string system)
    {
        if(!system_loaders[system])
        {
            try
            {
                system_loaders[system] = new Loader(system);
            }
            catch(std::string e)
            {
                throw e;
            }
        }

        return system_loaders[system]->SystemGet();
    }
}
