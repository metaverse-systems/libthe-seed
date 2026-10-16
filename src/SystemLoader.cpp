#include <libthe-seed/SystemLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginLoaderCore.hpp"
#include <memory>

namespace the_seed
{

namespace
{
struct SystemTraits
{
    using Product = ecs::System;
    using Creator = SystemLoader::SystemCreator;
    static constexpr const char *entryPoint = "create_system";
    static constexpr const char *kind = "system plugin";
};

using Core = the_seed::internal::PluginLoaderCore<SystemTraits>;
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
    return Core::Create(*this->cache, name, data);
}

SystemLoader::SystemCreator SystemLoader::Get(const std::string &name)
{
    return Core::Get(*this->cache, name);
}

void SystemLoader::PathAdd(const std::string &path)
{
    Core::PathAdd(*this->cache, path);
}

std::vector<std::string> SystemLoader::PathsGet() const
{
    return Core::PathsGet(*this->cache);
}

std::vector<std::string> SystemLoader::SearchPathsGet(const std::string &name) const
{
    return Core::SearchPathsGet(*this->cache, name);
}
} // namespace the_seed
