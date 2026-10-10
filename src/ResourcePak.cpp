#include <libthe-seed/ResourcePak.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PakAccess.hpp"
#include <filesystem>
#include <mutex>

using namespace seed::internal;

// Shared by every copy of a ResourcePak. The description is replaced when the
// file on disk is found to be a different version.
struct ResourcePak::State
{
    std::mutex mutex;
    std::filesystem::path path;
    std::shared_ptr<const PakIndex> index;
};

namespace
{
    // The current description of the file open as `file`: the remembered one
    // when the file is the version it was validated from, otherwise a freshly
    // validated one, which replaces it.
    std::shared_ptr<const PakIndex> IndexFor(std::mutex &mutex, std::shared_ptr<const PakIndex> &remembered,
                                             PakFile &file)
    {
        const PakStamp stamp = file.Stamp();
        std::lock_guard<std::mutex> lock(mutex);
        if(!remembered || remembered->stamp != stamp)
        {
            remembered = PakIndexRead(file);
        }
        return remembered;
    }
}

ResourcePak::ResourcePak(const std::string &filename): filename(filename), state(std::make_shared<State>())
{
    try
    {
        this->state->path = std::filesystem::absolute(this->filename);
        PakFile file = PakFile::Open(this->state->path);
        IndexFor(this->state->mutex, this->state->index, file);
    }
    catch(...)
    {
        PakFailureThrow(this->filename, this->filename, {}, true);
    }
}

void ResourcePak::Load(ecs::Container *container, const std::string &name)
{
    // Load() returns a temporary, which ResourceAdd moves into the world.
    container->ResourceAdd(name, this->Load(name));
}

ecs::Resource ResourcePak::Load(const std::string &name)
{
    try
    {
        PakFile file = PakFile::Open(this->state->path);
        auto index = IndexFor(this->state->mutex, this->state->index, file);
        auto found = index->by_name.find(name);
        if(found == index->by_name.end())
        {
            throw LoadError(LoadError::Reason::ResourceMissing, this->filename, this->filename, {},
                            "does not contain \"" + name + "\"", PakKind, {name});
        }

        ecs::Resource resource;
        PakEntryRead(file, index->entries[found->second], resource);
        return resource;
    }
    catch(...)
    {
        PakFailureThrow(this->filename, this->filename, {}, true);
    }
}

void ResourcePak::LoadAll(ecs::Container *container)
{
    std::vector<std::pair<std::string, ecs::Resource>> resources;
    try
    {
        PakFile file = PakFile::Open(this->state->path);
        auto index = IndexFor(this->state->mutex, this->state->index, file);
        resources.reserve(index->entries.size());
        for(const PakEntry &entry : index->entries)
        {
            resources.emplace_back(entry.name, ecs::Resource());
            PakEntryRead(file, entry, resources.back().second);
        }
    }
    catch(...)
    {
        PakFailureThrow(this->filename, this->filename, {}, true);
    }

    for(auto &resource : resources)
    {
        container->ResourceAdd(resource.first, std::move(resource.second));
    }
}

std::vector<std::string> ResourcePak::ResourceNames() const
{
    std::shared_ptr<const PakIndex> index;
    {
        std::lock_guard<std::mutex> lock(this->state->mutex);
        index = this->state->index;
    }

    std::vector<std::string> names;
    names.reserve(index->entries.size());
    for(const PakEntry &entry : index->entries)
    {
        names.push_back(entry.name);
    }
    return names;
}
