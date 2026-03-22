#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <libecs-cpp/ecs.hpp>

class PakLoader
{
  public:
    PakLoader();
    ~PakLoader() = default;

    PakLoader(const PakLoader &) = delete;
    PakLoader &operator=(const PakLoader &) = delete;
    PakLoader(PakLoader &&) = delete;
    PakLoader &operator=(PakLoader &&) = delete;

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>>
    Load(const std::string &pak_name);

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>>
    Load(const std::string &pak_name,
         const std::vector<std::string> &resource_names);

    void PathAdd(const std::string &path);
    std::vector<std::string> PathsGet() const;

  private:
    std::vector<std::string> paths_;
    mutable std::shared_mutex mutex_;
};