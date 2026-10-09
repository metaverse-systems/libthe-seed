#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>

/**
 * @brief Loads component plugins by name.
 *
 * Lifetime: no teardown order is required. Every plugin the loader opens is
 * pinned, so components, creators returned by Get and anything else made from
 * the plugin stay valid and destructible after this loader is destroyed. The
 * plugin's code stays loaded until the process ends.
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
     * Turns the development locations on or off for this loader (default
     * off). They are meant for development trees: the library's
     * "../../<library>/src/.libs" and, for an "org/library" name,
     * "../node_modules/<org>/<library>/src/.libs", relative to the working
     * directory. They are searched only when enabled and always after the
     * configured locations. The working directory itself is never searched
     * unless the application adds it with PathAdd. A load already in progress
     * keeps the setting it started with.
     */
    void DevelopmentPathsEnable(bool enabled = true);
    bool DevelopmentPathsEnabled() const;

    /**
     * The ordered locations a lookup of `name` searches: the configured
     * locations, then the development locations when enabled. Throws LoadError
     * (InvalidName) for an invalid name. Does not touch the file system.
     */
    std::vector<std::string> SearchPathsGet(const std::string &name) const;

  private:
    std::vector<std::string> paths;
    bool development_paths = false;
    std::map<std::string, ComponentCreator> entries;
    std::map<std::string, std::string> files;
    mutable std::shared_mutex mutex;
};
