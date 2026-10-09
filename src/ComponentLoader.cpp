#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginCache.hpp"
#include "internal/PluginSearch.hpp"
#include "NameParser.hpp"
#include <memory>

namespace
{
seed::internal::PluginCache<ComponentLoader::ComponentCreator>::Entry Resolve(seed::internal::PluginCache<ComponentLoader::ComponentCreator> &cache,
                                                         const std::string &name)
{
    // Reject names that could address anything but a plain file first.
    NameParser parsed(name, "component plugin");
    return cache.Get(name, parsed.org, parsed.library, "create_component", "component plugin");
}
} // namespace

ComponentLoader::ComponentLoader() : cache(std::make_unique<seed::internal::PluginCache<ComponentCreator>>())
{
}

ComponentLoader::~ComponentLoader() = default;

std::unique_ptr<ecs::Component> ComponentLoader::Create(const std::string &name)
{
    return this->Create(name, nullptr);
}

std::unique_ptr<ecs::Component> ComponentLoader::Create(const std::string &name, void *data)
{
    auto entry = Resolve(*this->cache, name);
    ecs::Component *object = entry.creator(data);
    if (object == nullptr)
        seed::internal::NoObjectThrow(entry.file, name, "create_component", "component plugin");
    return std::unique_ptr<ecs::Component>(object);
}

ComponentLoader::ComponentCreator ComponentLoader::Get(const std::string &name)
{
    return Resolve(*this->cache, name).creator;
}

void ComponentLoader::PathAdd(const std::string &path)
{
    this->cache->PathAdd(path);
}

std::vector<std::string> ComponentLoader::PathsGet() const
{
    return this->cache->PathsGet();
}

void ComponentLoader::DevelopmentPathsEnable(bool enabled)
{
    this->cache->DevelopmentPathsEnable(enabled);
}

bool ComponentLoader::DevelopmentPathsEnabled() const
{
    return this->cache->DevelopmentPathsEnabled();
}

std::vector<std::string> ComponentLoader::SearchPathsGet(const std::string &name) const
{
    NameParser parsed(name, "component plugin");
    return this->cache->SearchPathsGet(parsed.org, parsed.library);
}
