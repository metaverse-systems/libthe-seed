#pragma once

#include <memory>
#include <string>
#include <vector>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

/**
 * @brief Loads resources (images, sounds, etc.) from .pak files.
 * 
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
