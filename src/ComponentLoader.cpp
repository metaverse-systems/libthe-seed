#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginSearch.hpp"
#include "NameParser.hpp"
#include <shared_mutex>
#include <stdexcept>

ComponentLoader::ComponentLoader() = default;

ComponentLoader::~ComponentLoader() = default;

std::unique_ptr<ecs::Component> ComponentLoader::Create(const std::string &name)
{
    return this->Create(name, nullptr);
}

std::unique_ptr<ecs::Component> ComponentLoader::Create(const std::string &name, void *data)
{
    auto creator = this->Get(name);
    return std::unique_ptr<ecs::Component>(creator(data));
}

ComponentLoader::ComponentCreator ComponentLoader::Get(const std::string &name)
{
    // Reject names that could address anything but a plain file first.
    NameParser parsed(name, "component plugin");

    {
        std::shared_lock lock(this->mutex);
        auto it = this->entries.find(name);
        if (it != this->entries.end())
            return it->second;
    }

    std::unique_lock lock(this->mutex);
    auto it = this->entries.find(name);
    if (it != this->entries.end())
        return it->second;

    // The configured locations in the order they were added, then the
    // development locations if enabled; the first one holding the file
    // decides. The setting is snapshotted here, under the lock.
    size_t configured_count = 0;
    std::vector<std::string> locations = seed::internal::SearchListBuild(
        this->paths, this->development_paths, parsed.org, parsed.library, false, configured_count);
    seed::internal::SearchResult search;
    void *handle = seed::internal::PluginOpen(locations, configured_count, parsed.library, name, "component plugin", search);

    std::string error;
    void *ptr = seed::internal::SymbolFind(handle, "create_component", error);
    if (ptr == nullptr)
        throw std::runtime_error(error);

    auto creator = reinterpret_cast<ComponentCreator>(ptr);
    this->entries[name] = creator;

    return creator;
}

void ComponentLoader::PathAdd(const std::string &path)
{
    std::unique_lock lock(this->mutex);
    this->paths.push_back(path);
}

std::vector<std::string> ComponentLoader::PathsGet() const
{
    std::shared_lock lock(this->mutex);
    return this->paths;
}

void ComponentLoader::DevelopmentPathsEnable(bool enabled)
{
    std::unique_lock lock(this->mutex);
    this->development_paths = enabled;
}

bool ComponentLoader::DevelopmentPathsEnabled() const
{
    std::shared_lock lock(this->mutex);
    return this->development_paths;
}

std::vector<std::string> ComponentLoader::SearchPathsGet(const std::string &name) const
{
    NameParser parsed(name, "component plugin");

    std::shared_lock lock(this->mutex);
    size_t configured_count = 0;
    return seed::internal::SearchListBuild(this->paths, this->development_paths, parsed.org, parsed.library,
                                           false, configured_count);
}
