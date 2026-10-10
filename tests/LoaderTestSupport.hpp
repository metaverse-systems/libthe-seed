#pragma once

#include "TestPaths.hpp"

#include <libecs-cpp/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace seedtest {

// Changes the working directory for the life of the object and restores the
// previous one afterwards, even when the test fails.
class WorkingDirectoryGuard
{
public:
    explicit WorkingDirectoryGuard(const std::filesystem::path &target)
        : previous(std::filesystem::current_path())
    {
        std::filesystem::current_path(target);
    }

    WorkingDirectoryGuard(const WorkingDirectoryGuard &) = delete;
    WorkingDirectoryGuard &operator=(const WorkingDirectoryGuard &) = delete;

    ~WorkingDirectoryGuard()
    {
        std::error_code ec;
        std::filesystem::current_path(this->previous, ec);
    }

private:
    std::filesystem::path previous;
};

// Counts destructions. Its address goes to a plugin (the "counter" key of a
// component configuration, or the argument of create_system).
using DestructionCounter = std::atomic<int>;

// The address of a counter as the integer a component configuration carries.
inline std::uint64_t CounterAddress(DestructionCounter &counter)
{
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&counter));
}

// Copies the built plugin `source` (for example "testmodulealt") into
// `destDir` under the file name of the plugin `destName` (for example
// "testmodule"). Returns the path of the copy.
inline std::filesystem::path CopyModule(const ScratchDir &scratch, const std::string &source,
                                        const std::filesystem::path &destDir,
                                        const std::string &destName)
{
    std::filesystem::path dir = destDir.is_absolute() ? destDir : scratch.Path() / destDir;
    std::filesystem::create_directories(dir);
    std::filesystem::path dest = dir / PluginFileName(destName);
    std::filesystem::copy_file(PluginPath(source), dest,
                               std::filesystem::copy_options::overwrite_existing);
    return dest;
}

// Writes a file that has the plugin file name `name` but holds bytes the
// platform cannot load. Returns its path.
inline std::filesystem::path WriteDamagedModule(const std::filesystem::path &dir,
                                                const std::string &name)
{
    std::filesystem::create_directories(dir);
    std::filesystem::path path = dir / PluginFileName(name);
    std::ofstream out(path, std::ios::binary);
    out << "this is not a shared library, it is garbage bytes\n";
    return path;
}

// Writes `dir/<name>.pak` holding the given resources (name and bytes).
// Returns its path.
inline std::filesystem::path WritePak(
    const std::filesystem::path &dir, const std::string &name,
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> &resources)
{
    nlohmann::json header;
    header["resources"] = nlohmann::json::array();
    for(const auto &resource : resources)
    {
        nlohmann::json entry;
        entry["name"] = resource.first;
        entry["size"] = resource.second.size();
        header["resources"].push_back(entry);
    }

    // The header size is part of the header and its digit count can change the
    // length, so iterate until it is stable.
    header["headerSize"] = 0;
    std::string raw = header.dump();
    for(int i = 0; i < 5; ++i)
    {
        header["headerSize"] = std::to_string(raw.size() + 1);
        raw = header.dump();
    }

    std::filesystem::create_directories(dir);
    std::filesystem::path path = dir / (name + ".pak");
    std::ofstream out(path, std::ios::binary);
    out << raw << '\n';
    for(const auto &resource : resources)
    {
        out.write(reinterpret_cast<const char *>(resource.second.data()),
                  static_cast<std::streamsize>(resource.second.size()));
    }
    return path;
}

}
