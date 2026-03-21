#include <libthe-seed/SystemLoader.hpp>
#include "NameParser.hpp"

namespace SystemLoader
{
    std::map<std::string, std::unique_ptr<SystemLoader::Loader>> system_loaders;
    std::vector<std::string> system_paths;

    Loader::Loader(const std::string &library)
    {
        auto name = NameParser(library);

        this->library = std::make_unique<LibraryLoader>(name.library);
        this->library->PathAdd("./");
        this->library->PathAdd("../../" + name.library + "/src/.libs/");
        if(!name.org.empty())
        {
            auto path = "../node_modules/" + name.org + "/" + name.library + "/src/.libs";
            this->library->PathAdd(path);
        }

        for(const auto &path : system_paths) this->library->PathAdd(path);
    }

    SystemCreator Loader::Get()
    {
        if(!this->cached_creator)
        {
            auto ptr = this->library->FunctionGet("create_system");
            this->cached_creator = reinterpret_cast<SystemCreator>(ptr);
        }
        return this->cached_creator;
    }

    ecs::System *Loader::Create(void *data)
    {
        return this->Get()(data);
    }

    std::unique_ptr<ecs::System> Create(const std::string &system)
    {
        auto &loader = system_loaders[system];
        if(!loader) 
        {
            loader = std::make_unique<Loader>(system);
        }

        return std::unique_ptr<ecs::System>(loader->Create(nullptr));
    }

    std::unique_ptr<ecs::System> Create(const std::string &system, void *data)
    {
        auto &loader = system_loaders[system];
        if(!loader)
        {
            loader = std::make_unique<Loader>(system);
        }

        return std::unique_ptr<ecs::System>(loader->Create(data));
    }

    SystemCreator Get(const std::string &system)
    {
        auto &loader = system_loaders[system];
        if(!loader)
        {
            loader = std::make_unique<Loader>(system);
        }

        return loader->Get();
    }

    std::vector<std::string> PathsGet()
    {
        return system_paths;
    }

    void PathAdd(const std::string &path)
    {
        system_paths.push_back(path);
    }
}
