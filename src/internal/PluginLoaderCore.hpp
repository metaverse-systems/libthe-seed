#pragma once

#include <memory>
#include <string>
#include <vector>

#include "NameParser.hpp"
#include "PluginCache.hpp"
#include "PluginSearch.hpp"

namespace the_seed::internal
{

// The logic ComponentLoader and SystemLoader share. `Traits` names the
// product type, the type of the plugin's entry point, the entry point's symbol
// and the kind text used in error messages:
//
//   struct Traits
//   {
//       using Product = ecs::Component;
//       using Creator = ecs::Component *(*)(void *);
//       static constexpr const char *entryPoint = "create_component";
//       static constexpr const char *kind = "component plugin";
//   };
//
// The loaders keep their own non-template public classes; this template is
// only used inside their source files.
template <typename Traits> class PluginLoaderCore
{
  public:
    using Cache = PluginCache<typename Traits::Creator>;

    static std::unique_ptr<typename Traits::Product> Create(Cache &cache, const std::string &name, void *data)
    {
        auto entry = Resolve(cache, name);
        typename Traits::Product *object = entry.creator(data);
        if (object == nullptr)
            NoObjectThrow(entry.file, name, Traits::entryPoint, Traits::kind);
        return std::unique_ptr<typename Traits::Product>(object);
    }

    static typename Traits::Creator Get(Cache &cache, const std::string &name)
    {
        return Resolve(cache, name).creator;
    }

    static void PathAdd(Cache &cache, const std::string &path)
    {
        cache.PathAdd(path);
    }

    static std::vector<std::string> PathsGet(const Cache &cache)
    {
        return cache.PathsGet();
    }

    static std::vector<std::string> SearchPathsGet(const Cache &cache, const std::string &name)
    {
        NameParser parsed(name, Traits::kind);
        return cache.SearchPathsGet(parsed.org, parsed.library);
    }

  private:
    static typename Cache::Entry Resolve(Cache &cache, const std::string &name)
    {
        // Reject names that could address anything but a plain file first.
        NameParser parsed(name, Traits::kind);
        return cache.Get(name, parsed.org, parsed.library, Traits::entryPoint, Traits::kind);
    }
};

} // namespace the_seed::internal
