#include <libthe-seed/SystemLoader.hpp>
#include "NameParser.hpp"

namespace SystemLoader
{
    std::map<std::string, SystemLoader::Loader *> system_loaders;
    std::vector<std::string> system_paths;

    Loader::Loader(std::string library)
    {
        auto name = NameParser(library);

        try
        {
            this->library = new LibraryLoader(name.library);
            this->library->PathAdd("./");
            this->library->PathAdd("../../" + name.library + "/src/.libs/");
            if(name.org.size())
            {
                auto path = "../node_modules/" + name.org + "/" + name.library + "/src/.libs";
                this->library->PathAdd(path);
            }

            for(auto path : system_paths) this->library->PathAdd(path);
        }
        catch(std::runtime_error e)
        {
            throw e;
        }
    }

    SystemCreator Loader::SystemCreatorGet()
    {
        auto ptr = this->library->FunctionGet("create_system");
        auto creator = reinterpret_cast<SystemCreator>(ptr);
        return creator;
    }

    ecs::System *Loader::SystemCreate(void *data)
    {
        auto ptr = this->library->FunctionGet("create_system");
        auto creator = reinterpret_cast<SystemCreator>(ptr);
        return creator(data);
    }

    ecs::System *Create(std::string system)
    {
        if(!system_loaders[system]) 
        {
            system_loaders[system] = new Loader(system);
        }

        return system_loaders[system]->SystemCreate(nullptr);
    }

    ecs::System *Create(std::string system, void *data)
    {
        if(!system_loaders[system])
        {
            system_loaders[system] = new Loader(system);
        }

        return system_loaders[system]->SystemCreate(data);
    }

    SystemCreator CreatorGet(std::string system)
    {
        if(!system_loaders[system])
        {
            try
            {
                system_loaders[system] = new Loader(system);
            }
            catch(std::runtime_error e)
            {
                throw e;
            }
        }

        return system_loaders[system]->SystemCreatorGet();
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
