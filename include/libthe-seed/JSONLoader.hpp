#pragma once

#include <string>
#include <libecs-cpp/ecs.hpp>
#include <libecs-cpp/json.hpp>

class ComponentLoader;

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
