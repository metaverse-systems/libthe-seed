#include <libthe-seed/ComponentLoader.hpp>
#include "NameParser.hpp"
#include <iostream>

namespace ComponentLoader
{
    std::vector<std::string> component_paths;

    Loader::Loader(const std::string &library)
    {
        auto name = NameParser(library);
        this->library = std::make_unique<LibraryLoader>(name.library);

        this->library->PathAdd(".");
        this->library->PathAdd("../../" + name.library + "/src/.libs/");
        if (!name.org.empty())
        {
            auto path = "../node_modules/" + name.org + "/" + name.library + "/src/.libs";
            this->library->PathAdd(path);
        }

        for (const auto &path : component_paths)
            this->library->PathAdd(path);
    }

    ComponentCreator Loader::Get()
    {
        void *ptr = this->library->FunctionGet("create_component");
        return reinterpret_cast<ComponentCreator>(ptr);
    }

    ecs::Component *Loader::Create()
    {
        return this->Create(nullptr);
    }

    ecs::Component *Loader::Create(void *data)
    {
        auto creator = this->Get();
        return creator(data);
    }

    std::map<std::string, std::unique_ptr<ComponentLoader::Loader>> component_loaders;

    ecs::Component *Create(const std::string &component)
    {
        auto &loader = component_loaders[component];
        if (!loader)
        {
            loader = std::make_unique<Loader>(component);
        }

        return loader->Create();
    }

    ecs::Component *Create(const std::string &component, void *data)
    {
        auto &loader = component_loaders[component];
        if (!loader)
        {
            loader = std::make_unique<Loader>(component);
        }

        return loader->Create(data);
    }

    ComponentCreator Get(const std::string &component)
    {
        auto &loader = component_loaders[component];
        if (!loader)
        {
            loader = std::make_unique<Loader>(component);
        }

        return loader->Get();
    }

    std::vector<std::string> PathsGet()
    {
        return component_paths;
    }

    void PathAdd(const std::string &path)
    {
        component_paths.push_back(path);
    }
}
