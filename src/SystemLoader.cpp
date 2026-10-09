#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginSearch.hpp"
#include "NameParser.hpp"
#include <shared_mutex>
#include <stdexcept>

SystemLoader::SystemLoader() = default;

SystemLoader::~SystemLoader() = default;

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name)
{
    return this->Create(name, nullptr);
}

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name, void *data)
{
    auto creator = this->Get(name);
    return std::unique_ptr<ecs::System>(creator(data));
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

    // Only the configured locations are searched, in the order they were
    // added; the first one holding the file decides.
    seed::internal::SearchResult search;
    void *handle = seed::internal::PluginOpen(this->paths, parsed.library, name, "system plugin", search);

    std::string error;
    void *ptr = seed::internal::SymbolFind(handle, "create_system", error);
    if (ptr == nullptr)
        throw std::runtime_error(error);

    auto creator = reinterpret_cast<SystemCreator>(ptr);
    this->entries[name] = creator;

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
