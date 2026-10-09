#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>

/**
 * @brief Loads system plugins by name.
 *
 * Lifetime: no teardown order is required. Every plugin the loader opens is
 * pinned, so systems, creators returned by Get and anything else made from the
 * plugin stay valid and destructible after this loader is destroyed. The
 * plugin's code stays loaded until the process ends.
 */
class SystemLoader
{
  public:
    using SystemCreator = ecs::System *(*)(void *);

    SystemLoader();
    ~SystemLoader();

    SystemLoader(const SystemLoader &) = delete;
    SystemLoader &operator=(const SystemLoader &) = delete;
    SystemLoader(SystemLoader &&) = delete;
    SystemLoader &operator=(SystemLoader &&) = delete;

    std::unique_ptr<ecs::System> Create(const std::string &name);
    std::unique_ptr<ecs::System> Create(const std::string &name, void *data);

    SystemCreator Get(const std::string &name);

    void PathAdd(const std::string &path);
    std::vector<std::string> PathsGet() const;

  private:
    std::vector<std::string> paths;
    std::map<std::string, SystemCreator> entries;
    mutable std::shared_mutex mutex;
};
