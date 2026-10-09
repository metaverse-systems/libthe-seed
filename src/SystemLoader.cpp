#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginSearch.hpp"
#include "NameParser.hpp"
#include <shared_mutex>

SystemLoader::SystemLoader() = default;

SystemLoader::~SystemLoader() = default;

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name)
{
    return this->Create(name, nullptr);
}

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name, void *data)
{
    auto creator = this->Get(name);
    ecs::System *object = creator(data);
    if (object == nullptr)
    {
        std::string file;
        {
            std::shared_lock lock(this->mutex);
            auto it = this->files.find(name);
            if (it != this->files.end())
                file = it->second;
        }
        seed::internal::NoObjectThrow(file, name, "create_system", "system plugin");
    }
    return std::unique_ptr<ecs::System>(object);
}

SystemLoader::SystemCreator SystemLoader::Get(const std::string &name)
{
    // Reject names that could address anything but a plain file first.
    NameParser parsed(name, "system plugin");

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
    void *handle = seed::internal::PluginOpen(locations, configured_count, parsed.library, name, "system plugin", search);

    std::string error;
    void *ptr = seed::internal::SymbolFind(handle, "create_system", error);
    if (ptr == nullptr)
        seed::internal::EntryPointMissingThrow(search.file, name, "create_system", error, "system plugin");

    auto creator = reinterpret_cast<SystemCreator>(ptr);
    this->entries[name] = creator;
    this->files[name] = search.file.string();

    return creator;
}

void SystemLoader::PathAdd(const std::string &path)
{
    std::unique_lock lock(this->mutex);
    this->paths.push_back(path);
}

std::vector<std::string> SystemLoader::PathsGet() const
{
    std::shared_lock lock(this->mutex);
    return this->paths;
}

void SystemLoader::DevelopmentPathsEnable(bool enabled)
{
    std::unique_lock lock(this->mutex);
    this->development_paths = enabled;
}

bool SystemLoader::DevelopmentPathsEnabled() const
{
    std::shared_lock lock(this->mutex);
    return this->development_paths;
}

std::vector<std::string> SystemLoader::SearchPathsGet(const std::string &name) const
{
    NameParser parsed(name, "system plugin");

    std::shared_lock lock(this->mutex);
    size_t configured_count = 0;
    return seed::internal::SearchListBuild(this->paths, this->development_paths, parsed.org, parsed.library,
                                           false, configured_count);
}
