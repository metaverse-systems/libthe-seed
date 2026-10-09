#pragma once

#include <string>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

class ComponentLoader;

/**
 * @brief Builds a container from a JSON scene, creating components through a
 *        ComponentLoader.
 *
 * Lifetime: no teardown order is required. The components of a scene come from
 * pinned plugins, so they stay valid and destructible after this loader and the
 * ComponentLoader are destroyed, whether the container is destroyed before or
 * after them.
 */
class JSONLoader
{
  public:
    JSONLoader(ecs::Container *container, ComponentLoader &loader);
    ~JSONLoader() = default;

    void StringParse(const std::string &data);
    void FileParse(const std::string &filename);

  private:
    nlohmann::json scene;
    ecs::Container *container = nullptr;
    ComponentLoader &loader_;
};
