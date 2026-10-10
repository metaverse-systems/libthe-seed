#pragma once

#include <memory>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>

namespace seed::internal
{
template <typename Creator> class PluginCache;
}

/**
 * @brief Loads component plugins by name.
 *
 * Lifetime: no teardown order is required. Every plugin the loader opens is
 * pinned, so components, creators returned by Get and anything else made from
 * the plugin stay valid and destructible after this loader is destroyed. The
 * plugin's code stays loaded until the process ends.
 *
 * Threads: every method is safe to call from several threads at once. A plugin
 * is loaded once per loader: concurrent requests for one name wait for the same
 * load and get the same result, and names that resolve to one file share one
 * open. A failed load is reported to every waiting request and is not
 * remembered; the next request tries again. No loader lock is held while the
 * file system is searched or a plugin is opened. A plugin's static initialiser
 * must not load through the same loader. A request made after PathAdd
 * returned uses the new location.
 */
class ComponentLoader
{
  public:
    using ComponentCreator = ecs::Component *(*)(void *);

    ComponentLoader();
    ~ComponentLoader();

    ComponentLoader(const ComponentLoader &) = delete;
    ComponentLoader &operator=(const ComponentLoader &) = delete;
    ComponentLoader(ComponentLoader &&) = delete;
    ComponentLoader &operator=(ComponentLoader &&) = delete;

    std::unique_ptr<ecs::Component> Create(const std::string &name);
    std::unique_ptr<ecs::Component> Create(const std::string &name, void *data);

    ComponentCreator Get(const std::string &name);

    void PathAdd(const std::string &path);
    std::vector<std::string> PathsGet() const;

    /**
     * The ordered locations a lookup of `name` searches: the configured
     * locations in the order they were added, then the development locations
     * of a the-seed source tree: "../../<library>/src/.libs" and, for an
     * "org/library" name, "../node_modules/<org>/<library>/src/.libs",
     * relative to the working directory. The working directory itself is never
     * searched unless the application adds it with PathAdd. Throws LoadError
     * (InvalidName) for an invalid name. Does not touch the file system.
     */
    std::vector<std::string> SearchPathsGet(const std::string &name) const;

  private:
    std::unique_ptr<seed::internal::PluginCache<ComponentCreator>> cache;
};
