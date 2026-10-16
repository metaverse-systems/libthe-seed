#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginLoaderCore.hpp"
#include <memory>

namespace the_seed
{

namespace
{
struct ComponentTraits
{
    using Product = ecs::Component;
    using Creator = ComponentLoader::ComponentCreator;
    static constexpr const char *entryPoint = "create_component";
    static constexpr const char *kind = "component plugin";
};

using Core = the_seed::internal::PluginLoaderCore<ComponentTraits>;
} // namespace

ComponentLoader::ComponentLoader() : cache(std::make_unique<the_seed::internal::PluginCache<ComponentCreator>>())
{
}

ComponentLoader::~ComponentLoader() = default;

std::unique_ptr<ecs::Component> ComponentLoader::Create(const std::string &name)
{
    return this->Create(name, nullptr);
}

std::unique_ptr<ecs::Component> ComponentLoader::Create(const std::string &name, void *data)
{
    return Core::Create(*this->cache, name, data);
}

ComponentLoader::ComponentCreator ComponentLoader::Get(const std::string &name)
{
    return Core::Get(*this->cache, name);
}

void ComponentLoader::PathAdd(const std::string &path)
{
    Core::PathAdd(*this->cache, path);
}

std::vector<std::string> ComponentLoader::PathsGet() const
{
    return Core::PathsGet(*this->cache);
}

std::vector<std::string> ComponentLoader::SearchPathsGet(const std::string &name) const
{
    return Core::SearchPathsGet(*this->cache, name);
}
} // namespace the_seed
