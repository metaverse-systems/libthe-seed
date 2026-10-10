#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginSearch.hpp"
#include "NameParser.hpp"
#include <shared_mutex>
#include <stdexcept>
#include <algorithm>

PakLoader::PakLoader() = default;

// Opens the pak named `pak_name` from the first location in `locations` that holds
// <library>.pak. A pak that is present but refused by ResourcePak ends the
// search; later locations are not tried.
static std::shared_ptr<ResourcePak> LoadPak(const std::vector<std::string> &locations, size_t configured_count, const std::string &pak_name)
{
    constexpr const char *kind = "resource pak";
    NameParser parsed(pak_name, kind);

    const std::string file_name = seed::internal::PakFileName(parsed.library);
    seed::internal::SearchResult search = seed::internal::SearchFirst(locations, file_name, configured_count);
    if (!search.found)
        seed::internal::NotFoundThrow(search, pak_name, file_name, kind);

    try
    {
        return std::make_shared<ResourcePak>(search.file.string());
    }
    catch (const std::exception &e)
    {
        seed::internal::NotLoadableThrow(search, pak_name, e.what(), kind);
    }
}

std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> PakLoader::Load(const std::string &pak_name)
{
    size_t configured_count = 0;
    std::vector<std::string> search_paths = this->SearchPathsGet(pak_name, configured_count);

    auto pak = LoadPak(search_paths, configured_count, pak_name);
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
    size_t configured_count = 0;
    std::vector<std::string> search_paths = this->SearchPathsGet(pak_name, configured_count);

    auto pak = LoadPak(search_paths, configured_count, pak_name);
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
    std::unique_lock lock(this->mutex);
    this->paths.push_back(path);
}

std::vector<std::string> PakLoader::PathsGet() const
{
    std::shared_lock lock(this->mutex);
    return this->paths;
}

std::vector<std::string> PakLoader::SearchPathsGet(const std::string &pak_name) const
{
    size_t configured_count = 0;
    return this->SearchPathsGet(pak_name, configured_count);
}

// Validates the name, then builds the list from one snapshot of the paths
// taken under the lock.
std::vector<std::string> PakLoader::SearchPathsGet(const std::string &pak_name, size_t &configured_count) const
{
    NameParser parsed(pak_name, "resource pak");

    std::shared_lock lock(this->mutex);
    return seed::internal::SearchListBuild(this->paths, parsed.org, parsed.library, true, configured_count);
}
