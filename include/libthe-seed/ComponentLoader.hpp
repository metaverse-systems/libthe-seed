#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>

class LibraryLoader;

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

  private:
    std::vector<std::string> paths_;
    std::map<std::string, std::unique_ptr<LibraryLoader>> cache_;
    std::map<std::string, ComponentCreator> creators_;
    mutable std::shared_mutex mutex_;
};
