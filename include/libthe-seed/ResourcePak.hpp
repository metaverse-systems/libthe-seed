#pragma once

#include <string>
#include <map>
#include <libecs-cpp/ecs.hpp>

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
     * @param filename Path to resource pak to load.
     */
    ResourcePak(std::string filename);
    /**
     * @brief Load resource by name.
     * 
     * @param container Container to load resource into.
     * @param name Name of resource to load.
     */
    void Load(ecs::Container *container, std::string name);

    /**
     * @brief Load resource by name.
     *
     * @param name Name of resource to load.
     * @return Handle to Resource
     */
    ecs::Resource Load(std::string name);
    /**
     * @brief Load all resources in Resource Pak.
     * 
     * @param container Container to load resource into.
     */
    void LoadAll(ecs::Container *container);
  private:
    const std::string filename;
    uint64_t header_size = 0;
    char *raw = nullptr;
    Json::Value header;
};
