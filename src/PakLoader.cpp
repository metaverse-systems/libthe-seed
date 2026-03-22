#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>
#include "NameParser.hpp"
#include <shared_mutex>
#include <stdexcept>
#include <algorithm>

PakLoader::PakLoader() = default;

static std::shared_ptr<ResourcePak> LoadPak(const std::vector<std::string> &search_paths, const std::string &pak_name)
{
    for (const auto &path : search_paths)
    {
        auto full_path = path + "/" + pak_name + ".pak";
        try
        {
            return std::make_shared<ResourcePak>(full_path);
        }
        catch (const std::exception &e)
        {
            continue;
        }
    }

    throw std::runtime_error("Couldn't find resource pak: " + pak_name);
}

std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> PakLoader::Load(const std::string &pak_name)
{
    auto parsed = NameParser(pak_name);

    std::vector<std::string> search_paths;
    search_paths.push_back(".");
    search_paths.push_back("../../" + parsed.library);
    if (!parsed.org.empty())
    {
        auto path = "../node_modules/" + parsed.org + "/" + parsed.library;
        search_paths.push_back(path);
    }

    {
        std::shared_lock lock(mutex_);
        for (const auto &p : paths_)
            search_paths.push_back(p);
    }

    auto pak = LoadPak(search_paths, parsed.library);
    std::vector<std::string> resource_names = pak->ResourceNames();
    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
    for (const auto &resource_name : resource_names)
    {
        resources[resource_name] = std::make_shared<ecs::Resource>(pak->Load(resource_name));
    }

    return resources;
}

std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> PakLoader::Load(const std::string &pak_name, const std::vector<std::string> &resource_names)
{
    auto parsed = NameParser(pak_name);

    std::vector<std::string> search_paths;
    search_paths.push_back(".");
    search_paths.push_back("../../" + parsed.library);
    if (!parsed.org.empty())
    {
        auto path = "../node_modules/" + parsed.org + "/" + parsed.library;
        search_paths.push_back(path);
    }

    {
        std::shared_lock lock(mutex_);
        for (const auto &p : paths_)
            search_paths.push_back(p);
    }

    auto pak = LoadPak(search_paths, parsed.library);
    std::vector<std::string> available_resource_names = pak->ResourceNames();
    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
    for (const auto &available_name : available_resource_names)
    {
        if (std::find(resource_names.begin(), resource_names.end(), available_name) != resource_names.end())
        {
            resources[available_name] = std::make_shared<ecs::Resource>(pak->Load(available_name));
        }
    }

    return resources;
}

void PakLoader::PathAdd(const std::string &path)
{
    std::unique_lock lock(mutex_);
    paths_.push_back(path);
}

std::vector<std::string> PakLoader::PathsGet() const
{
    std::shared_lock lock(mutex_);
    return paths_;
}