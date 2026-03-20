#include <libecs-cpp/ecs.hpp>

class ResourcePak;

namespace PakLoader
{
    std::vector<std::string> paths;
    std::vector<std::string> PathsGet()
    {
        return paths;
    }

    void PathAdd(std::string path)
    {
        paths.push_back(path);
    }

    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(std::string pak_name);
    std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> Load(std::string pak_name, std::vector<std::string> resource_names);
}