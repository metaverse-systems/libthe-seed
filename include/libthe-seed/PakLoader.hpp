#pragma once

#include <map>
#include <future>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <libecs-cpp/ecs.hpp>
#include <libthe-seed/LoadError.hpp>

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
 * source tree are searched after the configured ones. A location that cannot
 * be examined (permission denied) is listed with the system's text and the
 * search goes on.
 *
 * Errors: only LoadError leaves this class. InvalidName; NotFound (no
 * location holds the pak); NotLoadable (the pak is present but damaged, in
 * which case DetailGet() starts with "damaged:", or could not be read, in
 * which case it is the system's text); ResourceMissing, when a requested name
 * is not in the pak: nothing is returned and MissingGet() lists every missing
 * name in request order. Names listed twice are returned once.
 *
 * Cost: a request opens the pak, reads each requested resource straight into
 * its own ecs::Resource and closes the file. The pak's description (the list
 * of resource names and sizes) is read and checked once per version of the
 * file for the life of the loader and remembered; a later request finds the
 * file again, compares its identity, size and modification time, and reads the
 * description again only if they differ. No resource bytes are kept. A pak
 * that fails is not remembered; the next request examines the file again.
 * Unlike a plugin, which stays loaded until the process ends, a rebuilt pak is
 * picked up by the next request.
 *
 * Files: the pak is opened for each request and closed before the request
 * returns, so another program may replace or delete it at any time, on Linux
 * and on Windows. A request sees the one version of the file it opened.
 *
 * Threads: every method is safe to call from several threads at once.
 * Concurrent requests for one unchanged file check its description once and
 * share the result. No lock is held while searching, opening, checking or
 * reading. A request made after PathAdd returned uses the new location.
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
                                                              seed::internal::PakFile &file, const std::string &pak_name,
                                                              const std::string &file_name,
                                                              const std::vector<LoadError::Location> &locations);

    // The result of validating one version of a pak file, shared with waiters.
    struct Outcome
    {
        std::shared_ptr<const seed::internal::PakIndex> index;
        bool failed = false;
        LoadError::Reason reason = LoadError::Reason::NotLoadable;
        std::string detail;
    };
    struct InFlight;

    std::vector<std::string> paths;
    // Validated descriptions by canonical path of the pak file.
    std::map<std::string, std::shared_ptr<const seed::internal::PakIndex>> remembered;
    // Validations under way by canonical path of the pak file.
    std::map<std::string, std::shared_ptr<InFlight>> in_flight;
    mutable std::shared_mutex mutex;
};