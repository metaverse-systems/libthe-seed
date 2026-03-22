#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>

class LibraryLoader;

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
    std::vector<std::string> paths_;
    std::map<std::string, std::unique_ptr<LibraryLoader>> cache_;
    std::map<std::string, SystemCreator> creators_;
    mutable std::shared_mutex mutex_;
};
