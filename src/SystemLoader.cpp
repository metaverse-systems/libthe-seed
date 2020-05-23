#include <libthe-seed/SystemLoader.hpp>
#include <iostream>

namespace SystemLoader
{
    std::map<std::string, SystemLoader::Loader *> system_loaders;
    std::vector<std::string> system_paths;

    Loader::Loader(std::string library)
    {
        try
        {
            this->library = new LibraryLoader(library);
            this->library->PathAdd("./");
            this->library->PathAdd("../../" + library + "/src/.libs/");

            for(auto path : system_paths) this->library->PathAdd(path);
        }
        catch(std::string e)
        {
            std::cout << "Error in SystemLoader::Loader::Loader(\"" + library + "\")" << std::endl;
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
            e = "SystemLoader::Loader(\"" + this->library->name + "\")::SystemCreate(): Couldn't get create_system function. " + e;
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
            e = "SystemLoader::Loader(\"" + this->library->name + "\")::SystemCreate(void *): Couldn't get create_system function. " + e;
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
                std::cout << "SystemLoader::Create()" << std::endl;
                std::cout << e << std::endl;
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
                std::cout << "SystemLoader::Create(,)" << std::endl;
                std::cout << e << std::endl;
                exit(1);
            }
        }

        ecs::System *sys = nullptr;
        
        try
        {
            sys = system_loaders[system]->SystemCreate(data);
        }
        catch(std::string e)
        {
            std::cout << e << std::endl;
            exit(1);
        }

        return sys;
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

    std::vector<std::string> PathsGet()
    {
        return system_paths;
    }

    void PathAdd(std::string path)
    {
        system_paths.push_back(path);
    }
}
