#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <libecs-cpp/ecs.hpp>

namespace seed::internal
{
class PakFile;
struct PakIndex;
} // namespace seed::internal

/**
 * @brief Loads resource paks by name.
 *
 * Search: locations added with PathAdd are searched first, in order, and the
 * first location holding <library>.pak decides. A pak that is present but
 * cannot be read fails the load with LoadError (NotLoadable) and later
 * locations are not tried. The working directory is never searched unless the
 * application adds it with PathAdd. The development locations of a the-seed
 * source tree are searched after the configured ones. Failures are LoadError
 * (InvalidName, NotFound, NotLoadable).
 *
 * Cost: a request opens the pak, reads each requested resource straight into
 * its own ecs::Resource and closes the file. The pak's description (the list
 * of resource names and sizes) is read and checked once per version of the
 * file for the life of the loader and remembered; a later request finds the
 * file again, compares its identity, size and modification time, and reads the
 * description again only if they differ. No resource bytes are kept.
 *
 * Threads: every method is safe to call from several threads at once. A
 * request made after PathAdd returned uses the new location.
 */
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
     * The ordered locations a lookup of `pak_name` searches: the configured
     * locations in the order they were added, then the development locations
     * of a the-seed source tree: "../../<library>" and, for an "org/library"
     * name, "../node_modules/<org>/<library>", relative to the working
     * directory. The working directory itself is never searched unless the
     * application adds it with PathAdd. Throws LoadError (InvalidName) for an
     * invalid name. Does not touch the file system.
     */
    std::vector<std::string> SearchPathsGet(const std::string &pak_name) const;

  private:
    std::vector<std::string> SearchPathsGet(const std::string &pak_name, size_t &configured_count) const;

    // One request: every resource, or those named when `resource_names` is set.
    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>>
    Request(const std::string &pak_name, const std::vector<std::string> *resource_names);

    // The remembered description of the version of `file` that was just opened:
    // the one kept for `identity` when it matches, otherwise one validated from
    // `file` and kept in its place. Failures are not kept.
    std::shared_ptr<const seed::internal::PakIndex> IndexGet(const std::string &identity,
                                                              seed::internal::PakFile &file);

    std::vector<std::string> paths;
    // Validated descriptions by canonical path of the pak file.
    std::map<std::string, std::shared_ptr<const seed::internal::PakIndex>> remembered;
    mutable std::shared_mutex mutex;
};