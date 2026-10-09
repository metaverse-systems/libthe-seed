#pragma once

#include <string>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

class ComponentLoader;

/**
 * @brief Builds a container from a JSON scene, creating components through a
 *        ComponentLoader.
 *
 * All or nothing: the scene is read, parsed and checked as a whole, and every
 * component is made before the container is touched. Any failure leaves the
 * container exactly as it was. Failures are LoadError:
 *  - SceneUnopenable: FileParse could not open or read the file (missing,
 *    a directory, not a regular file, permission denied, read error).
 *  - SceneNotUnderstood: a syntax error (with line and column) or a structure
 *    error (missing "entities", an entity without a "Handle" or "Components").
 *  - SceneComponentFailed: a component could not be made; the message names the
 *    entity, the component type and the plugin's own message.
 * An explicit empty "entities" array is valid and adds nothing.
 *
 * Lifetime: neither this loader nor the ComponentLoader needs to outlive the
 * container, and there is no teardown order. The components of a scene come
 * from pinned plugins, so they stay valid and destructible after both are
 * destroyed, whether the container is destroyed before or after them.
 */
class JSONLoader
{
  public:
    JSONLoader(ecs::Container *container, ComponentLoader &loader);
    ~JSONLoader() = default;

    void StringParse(const std::string &data);
    void FileParse(const std::string &filename);

  private:
    void Load(const std::string &data, const std::string &source);

    nlohmann::json scene;
    ecs::Container *container = nullptr;
    ComponentLoader &loader;
};
