#pragma once

#include <libthe-seed/Export.hpp>

#include <string>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

namespace the_seed
{
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
 *
 * Data format: the configuration pointer a plugin receives (the `data` argument
 * of `create_component`) is a `nlohmann::json *` from the JSON library that
 * libecs-cpp provides. A plugin that reads it must be built against the same
 * libecs-cpp, and so the same JSON library version, as libthe-seed. Nothing
 * checks this; a mismatch is undefined behaviour, typically a crash.
 */
class THE_SEED_API JSONLoader
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
} // namespace the_seed
