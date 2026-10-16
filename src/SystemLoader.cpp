#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginCache.hpp"
#include "internal/PluginSearch.hpp"
#include "internal/NameParser.hpp"
#include <memory>

namespace the_seed
{

namespace
{
the_seed::internal::PluginCache<SystemLoader::SystemCreator>::Entry Resolve(the_seed::internal::PluginCache<SystemLoader::SystemCreator> &cache,
                                                         const std::string &name)
{
    // Reject names that could address anything but a plain file first.
    the_seed::internal::NameParser parsed(name, "system plugin");
    return cache.Get(name, parsed.org, parsed.library, "create_system", "system plugin");
}
} // namespace

SystemLoader::SystemLoader() : cache(std::make_unique<the_seed::internal::PluginCache<SystemCreator>>())
{
}

SystemLoader::~SystemLoader() = default;

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name)
{
    return this->Create(name, nullptr);
}

std::unique_ptr<ecs::System> SystemLoader::Create(const std::string &name, void *data)
{
    auto entry = Resolve(*this->cache, name);
    ecs::System *object = entry.creator(data);
    if (object == nullptr)
        the_seed::internal::NoObjectThrow(entry.file, name, "create_system", "system plugin");
    return std::unique_ptr<ecs::System>(object);
}

SystemLoader::SystemCreator SystemLoader::Get(const std::string &name)
{
    return Resolve(*this->cache, name).creator;
}

void SystemLoader::PathAdd(const std::string &path)
{
    this->cache->PathAdd(path);
}

std::vector<std::string> SystemLoader::PathsGet() const
{
    return this->cache->PathsGet();
}

std::vector<std::string> SystemLoader::SearchPathsGet(const std::string &name) const
{
    the_seed::internal::NameParser parsed(name, "system plugin");
    return this->cache->SearchPathsGet(parsed.org, parsed.library);
}
} // namespace the_seed
