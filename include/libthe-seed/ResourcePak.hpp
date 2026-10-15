#pragma once

#include <memory>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

/**
 * @brief Loads resources (images, sounds, etc.) from .pak files.
 *
 * The constructor checks the pak's description (the list of resource names and
 * sizes) and remembers it. Each Load opens the file, reads only the resource
 * asked for and closes the file; if the file changed since the description was
 * checked (identity, size or modification time), the new version is checked
 * first, so the pak may be replaced or deleted by another program at any time.
 *
 * Errors: only LoadError leaves this class. NotFound for a missing file;
 * NotLoadable for a damaged pak (DetailGet() starts with "damaged:") or one
 * that could not be read (the system's text); ResourceMissing for a name the
 * pak does not hold (MissingGet() lists it).
 *
 * Threads: one object may be used from several threads, and copies share the
 * remembered description. Adding to an ecs::Container is limited to that
 * container's thread, as libecs-cpp requires.
 */
class ResourcePak
{
  public:
    /**
     * @brief Construct a new ResourcePak object.
     *
     * Opens the pak, checks its description and remembers it; no resource is
     * read. Throws LoadError if the file is missing or damaged.
     *
     * @param filename Path to resource pak to load.
     */
    ResourcePak(const std::string &filename);
    /**
     * @brief Load resource by name.
     * 
     * @param container Container to load resource into.
     * @param name Name of resource to load.
     */
    void Load(ecs::Container *container, const std::string &name);

    /**
     * @brief Load resource by name.
     *
     * @param name Name of resource to load.
     * @return Handle to Resource
     */
    ecs::Resource Load(const std::string &name);
    /**
     * @brief Load all resources in Resource Pak.
     * 
     * @param container Container to load resource into.
     */
    void LoadAll(ecs::Container *container);
    /**
     * @brief Names of the resources in the pak, in the order the pak lists them.
     *
     * Answered from the description remembered by the constructor; the file
     * is not touched.
     */
    std::vector<std::string> ResourceNames() const;
  private:
    struct State;

    const std::string filename;
    std::shared_ptr<State> state;
};
