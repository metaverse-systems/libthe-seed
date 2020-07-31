#pragma once

#include <string>
#include <map>
#include <libecs-cpp/ecs.hpp>

class ResourcePak
{
  public:
    ResourcePak(std::string resource_pak);
    ecs::Resource get(std::string resource_name);
    std::string name;
    uint64_t header_size = 0;
    char *raw = nullptr;
    void load_file();
    std::map<std::string, ecs::Resource> resource_map;
};
