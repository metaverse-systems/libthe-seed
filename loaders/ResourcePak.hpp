#pragma once

#include <string>
#include <map>
#include <libecs-cpp/ecs.hpp>

typedef struct Resource
{
    char *ptr;
    uint64_t size = 0;
} Resource;

class ResourcePak
{
  public:
    ResourcePak(std::string resource_pak);
    Resource get(std::string resource_name);
    std::string name;
    uint64_t header_size = 0;
    char *raw = nullptr;
    void load_file();
    std::map<std::string, Resource> resource_map;
};
