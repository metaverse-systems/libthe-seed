#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>
#include "NameParser.hpp"

namespace PakLoader
{
    std::shared_ptr<ResourcePak> LoadPak(std::vector<std::string> paths, std::string pak_name)
    {
        for(auto &path : paths)
        {
            auto full_path = path + "/" + pak_name + ".pak";
            try
            {
                return std::make_shared<ResourcePak>(full_path);
            }
            catch(const std::exception &e)
            {
                continue;
            }
        }

        throw std::runtime_error("Couldn't find resource pak: " + pak_name);
    }

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(std::string pak_name)
    {
        auto name = NameParser(pak_name);

        std::vector<std::string> paths;
        paths.push_back(".");
        paths.push_back("../../" + name.library);
        if (!name.org.empty())
        {
            auto path = "../node_modules/" + name.org + "/" + name.library;
            paths.push_back(path);
        }

        auto pak = LoadPak(paths, name.library);
        std::vector<std::string> resource_names = pak->ResourceNames();
        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
        for(auto &name : resource_names)
        {
            resources[name] = std::make_shared<ecs::Resource>(pak->Load(name));
        }

        loaded_paks.push_back(pak);
        return resources;
    }

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(std::string pak_name, std::vector<std::string> resource_names)
    {
        auto pak = LoadPak(paths, pak_name);
        std::vector<std::string> available_resource_names = pak->ResourceNames();
        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
        for(auto &name : available_resource_names)
        {
            if(std::find(resource_names.begin(), resource_names.end(), name) != resource_names.end())
            {
                resources[name] = std::make_shared<ecs::Resource>(pak->Load(name));
            }
        }

        loaded_paks.push_back(pak);
        return resources;
    }
}