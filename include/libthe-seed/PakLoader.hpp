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

    /**
     * Turns the development locations on or off for this loader (default
     * off): "../../<library>" and, for an "org/library" name,
     * "../node_modules/<org>/<library>", relative to the working directory.
     * They are meant for development, searched only when enabled and always
     * after the configured locations. The working directory itself is never
     * searched unless the application adds it with PathAdd.
     */
    void DevelopmentPathsEnable(bool enabled = true);
    bool DevelopmentPathsEnabled() const;

    /**
     * The ordered locations a lookup of `pak_name` searches: configured
     * first, then development when enabled. Throws LoadError (InvalidName) for
     * an invalid name. Does not touch the file system.
     */
    std::vector<std::string> SearchPathsGet(const std::string &pak_name) const;

  private:
    std::vector<std::string> SearchPathsGet(const std::string &pak_name, size_t &configured_count) const;

    std::vector<std::string> paths;
    bool development_paths = false;
    mutable std::shared_mutex mutex;
};