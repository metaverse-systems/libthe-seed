#pragma once

#include <string>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

/**
 * @brief Loads Entities, Components, and Systems from JSON data.
 * 
 */
class JSONLoader
{
  public:
    /**
     * @brief Construct a new JSONLoader object.
     * 
     * @param container A pointer to the ECS container that will 
     *                  house the Entities, Components, and Systems
     *                  imported from JSON data.
     */
    JSONLoader(ecs::Container *container);
    ~JSONLoader();
    /**
     * @brief Parses ECS data from string.
     * 
     * @param data JSON formatted ECS data.
     */
    void StringParse(std::string data);
    /**
     * @brief Parses ECS data from a file.
     * 
     * @param filename JSON formatted file containing ECS data.
     */
    void FileParse(std::string filename);
  private:
    nlohmann::json scene;
    ecs::Container *container = nullptr;
};
