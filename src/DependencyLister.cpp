#include <libthe-seed/DependencyLister.hpp>

#include "ElfParser.hpp"
#include "PeParser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
enum class BinaryFormat
{
    Elf,
    Pe
};

BinaryFormat DetectFormat(const std::string &binary_path)
{
    std::ifstream input(binary_path, std::ios::binary);
    if(!input.is_open())
    {
        throw std::runtime_error("Unable to open file");
    }

    std::uint8_t header[4] = {};
    input.read(reinterpret_cast<char *>(header), sizeof(header));
    if(input.gcount() < 2)
    {
        throw std::runtime_error("File is too small to determine binary format");
    }

    if(input.gcount() >= 4 && header[0] == ELFMAG0 && header[1] == ELFMAG1 && header[2] == ELFMAG2 &&
       header[3] == ELFMAG3)
    {
        return BinaryFormat::Elf;
    }

    if(header[0] == 'M' && header[1] == 'Z')
    {
        return BinaryFormat::Pe;
    }

    throw std::runtime_error("Unsupported binary format");
}

std::vector<std::string> ParseBinaryDependencies(const std::string &binary_path)
{
    switch(DetectFormat(binary_path))
    {
        case BinaryFormat::Elf:
            return ElfParser::ListDependencies(binary_path);
        case BinaryFormat::Pe:
            return PeParser::ListDependencies(binary_path);
    }

    throw std::runtime_error("Unsupported binary format");
}

std::optional<std::string> ResolveDependency(
    const std::string &library_name,
    const std::vector<std::string> &search_paths
)
{
    for(const auto &search_path : search_paths)
    {
        const auto candidate = std::filesystem::path(search_path) / library_name;
        if(std::filesystem::exists(candidate))
        {
            return std::filesystem::canonical(candidate).string();
        }
    }

    return std::nullopt;
}

void AddDependency(
    std::map<std::string, std::vector<std::string>> &dependencies,
    const std::string &library_key,
    const std::string &originating_binary
)
{
    auto &dependents = dependencies[library_key];
    if(std::find(dependents.begin(), dependents.end(), originating_binary) == dependents.end())
    {
        dependents.push_back(originating_binary);
    }
}

void ResolveRecursive(
    const std::string &resolved_library,
    const std::string &originating_binary,
    const std::vector<std::string> &search_paths,
    std::unordered_set<std::string> &visited,
    DependencyResult &result
)
{
    if(visited.find(resolved_library) != visited.end())
    {
        return;
    }

    visited.insert(resolved_library);

    std::vector<std::string> dependencies;
    try
    {
        dependencies = ParseBinaryDependencies(resolved_library);
    }
    catch(...)
    {
        return;
    }

    for(const auto &dependency_name : dependencies)
    {
        const auto resolved_dependency = ResolveDependency(dependency_name, search_paths);
        const std::string dependency_key = resolved_dependency.value_or(dependency_name);

        AddDependency(result.dependencies, dependency_key, originating_binary);

        if(resolved_dependency.has_value())
        {
            ResolveRecursive(*resolved_dependency, originating_binary, search_paths, visited, result);
        }
    }
}
} // namespace

DependencyResult DependencyLister::ListDependencies(
    const std::vector<std::string> &binary_paths,
    const std::vector<std::string> &search_paths
)
{
    DependencyResult result;
    std::unordered_set<std::string> visited;

    for(const auto &binary_path : binary_paths)
    {
        try
        {
            const auto dependencies = ParseBinaryDependencies(binary_path);
            for(const auto &dependency_name : dependencies)
            {
                const auto resolved_dependency = ResolveDependency(dependency_name, search_paths);
                const std::string dependency_key = resolved_dependency.value_or(dependency_name);

                AddDependency(result.dependencies, dependency_key, binary_path);

                if(resolved_dependency.has_value())
                {
                    ResolveRecursive(*resolved_dependency, binary_path, search_paths, visited, result);
                }
            }
        }
        catch(const std::exception &error)
        {
            result.errors[binary_path] = error.what();
        }
    }

    return result;
}
