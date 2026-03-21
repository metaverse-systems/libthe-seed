#pragma once

#include <libecs-cpp/ecs.hpp>

class ResourcePak;

namespace PakLoader
{
    extern std::vector<std::string> paths;
    std::vector<std::string> PathsGet();
    void PathAdd(const std::string &path);

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(const std::string &pak_name);
    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(const std::string &pak_name, const std::vector<std::string> &resource_names);
}