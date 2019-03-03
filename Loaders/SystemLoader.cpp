#include "SystemLoader.hpp"
#include <iostream>

namespace SystemLoader
{
    Loader::Loader(std::string library)
    {
        try
        {
            this->library = new LibraryLoader(library);
            this->library->PathAdd("../systems/" + library + "/src/.libs/");
        }
        catch(std::string e)
        {
            throw e;
        }
    }

    ecs::System *Loader::SystemGet()
    {
        void *ptr = this->library->FunctionGet("create_system");
        SystemCreator creator = reinterpret_cast<SystemCreator>(ptr);
        return creator(nullptr);
    }

    ecs::System *Loader::SystemGet(void *data)
    {
        void *ptr;

        try
        {
            ptr = this->library->FunctionGet("create_system");
        }
        catch(std::string e)
        {
            std::cerr << e << std::endl;
            throw e;
        }

        SystemCreator creator = reinterpret_cast<SystemCreator>(ptr);
        return creator(data);
    }

    std::map<std::string, SystemLoader::Loader *> system_loaders;

    ecs::System *Get(std::string system)
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

    ecs::System *Get(std::string system, void *data)
    {
        std::cout << "Loading system " << system << ". Pointer: 0x" << data << std::endl;
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

        return system_loaders[system]->SystemGet(data);
    }
}
