#pragma once

#include <string>
#include <map>
#include <unordered_map>
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
    std::vector<std::string> ResourceNames()
    {
        std::vector<std::string> names;
        for(auto &resource : this->header["resources"])
        {
            names.push_back(resource["name"].get<std::string>());
        }
        return names;
    }
  private:
    const std::string filename;
    uint64_t header_size = 0;
    std::vector<char> raw;
    nlohmann::json header;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> offset_map;
};
