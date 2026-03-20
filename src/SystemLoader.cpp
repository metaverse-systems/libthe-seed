#include <libthe-seed/SystemLoader.hpp>
#include "NameParser.hpp"

namespace SystemLoader
{
    std::map<std::string, std::unique_ptr<SystemLoader::Loader>> system_loaders;
    std::vector<std::string> system_paths;

    Loader::Loader(std::string library)
    {
        auto name = NameParser(library);

        try
        {
            this->library = std::make_unique<LibraryLoader>(name.library);
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

    std::unique_ptr<ecs::System> Create(std::string system)
    {
        auto &loader = system_loaders[system];
        if(!loader) 
        {
            loader = std::make_unique<Loader>(system);
        }

        return std::unique_ptr<ecs::System>(loader->SystemCreate(nullptr));
    }

    std::unique_ptr<ecs::System> Create(std::string system, void *data)
    {
        auto &loader = system_loaders[system];
        if(!loader)
        {
            loader = std::make_unique<Loader>(system);
        }

        return std::unique_ptr<ecs::System>(loader->SystemCreate(data));
    }

    SystemCreator CreatorGet(std::string system)
    {
        auto &loader = system_loaders[system];
        if(!loader)
        {
            try
            {
                loader = std::make_unique<Loader>(system);
            }
            catch(std::runtime_error e)
            {
                throw e;
            }
        }

        return loader->SystemCreatorGet();
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
