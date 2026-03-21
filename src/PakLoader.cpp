#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>
#include "NameParser.hpp"

namespace PakLoader
{
    std::vector<std::string> paths;

    std::vector<std::string> PathsGet()
    {
        return paths;
    }

    void PathAdd(const std::string &path)
    {
        paths.push_back(path);
    }

    std::shared_ptr<ResourcePak> LoadPak(const std::vector<std::string> &paths, const std::string &pak_name)
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

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(const std::string &pak_name)
    {
        auto name = NameParser(pak_name);

        std::vector<std::string> search_paths;
        search_paths.push_back(".");
        search_paths.push_back("../../" + name.library);
        if (!name.org.empty())
        {
            auto path = "../node_modules/" + name.org + "/" + name.library;
            search_paths.push_back(path);
        }

        auto pak = LoadPak(search_paths, name.library);
        std::vector<std::string> resource_names = pak->ResourceNames();
        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
        for(auto &resource_name : resource_names)
        {
            resources[resource_name] = std::make_shared<ecs::Resource>(pak->Load(resource_name));
        }

        return resources;
    }

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(const std::string &pak_name, const std::vector<std::string> &resource_names)
    {
        auto name = NameParser(pak_name);

        std::vector<std::string> search_paths;
        search_paths.push_back(".");
        search_paths.push_back("../../" + name.library);
        if (!name.org.empty())
        {
            auto path = "../node_modules/" + name.org + "/" + name.library;
            search_paths.push_back(path);
        }

        auto pak = LoadPak(search_paths, name.library);
        std::vector<std::string> available_resource_names = pak->ResourceNames();
        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
        for(auto &available_name : available_resource_names)
        {
            if(std::find(resource_names.begin(), resource_names.end(), available_name) != resource_names.end())
            {
                resources[available_name] = std::make_shared<ecs::Resource>(pak->Load(available_name));
            }
        }

        return resources;
    }
}