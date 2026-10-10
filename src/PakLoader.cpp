#include <libthe-seed/PakLoader.hpp>
#include <libthe-seed/ResourcePak.hpp>
#include <libthe-seed/LoadError.hpp>
#include "internal/PluginSearch.hpp"
#include "internal/PakAccess.hpp"
#include "NameParser.hpp"
#include <shared_mutex>
#include <stdexcept>
#include <algorithm>
#include <exception>
#include <future>

PakLoader::PakLoader() = default;

using namespace seed::internal;

// A validation under way: the version of the file it is for and its outcome.
struct PakLoader::InFlight
{
    PakStamp stamp;
    std::shared_future<Outcome> future;
};

std::shared_ptr<const PakIndex> PakLoader::IndexGet(const std::string &identity, PakFile &file, const std::string &pak_name,
                    const std::string &file_name, const std::vector<LoadError::Location> &locations)
{
    const PakStamp stamp = file.Stamp();
    {
        std::shared_lock lock(this->mutex);
        auto found = this->remembered.find(identity);
        if(found != this->remembered.end() && found->second->stamp == stamp)
        {
            return found->second;
        }
    }

    // One thread validates a given version of the file; the others wait for
    // its outcome. No lock is held while the description is read or awaited.
    std::shared_future<Outcome> future;
    std::promise<Outcome> promise;
    bool owner = false;
    {
        std::unique_lock lock(this->mutex);
        auto found = this->remembered.find(identity);
        if(found != this->remembered.end() && found->second->stamp == stamp)
        {
            return found->second;
        }

        auto flying = this->in_flight.find(identity);
        if(flying != this->in_flight.end() && flying->second->stamp == stamp)
        {
            future = flying->second->future;
        }
        else
        {
            future = promise.get_future().share();
            this->in_flight[identity] = std::make_shared<InFlight>(InFlight{stamp, future});
            owner = true;
        }
    }

    Outcome outcome;
    if(owner)
    {
        try
        {
            outcome.index = PakIndexRead(file);
        }
        catch(...)
        {
            // Reduced to plain values so that every waiter throws an error of
            // its own and no exception object is shared between threads.
            outcome.failed = true;
            try
            {
                PakFailureThrow(pak_name, file_name, locations, false);
            }
            catch(const LoadError &error)
            {
                outcome.reason = error.ReasonGet();
                outcome.detail = error.DetailGet();
            }
        }

        {
            std::unique_lock lock(this->mutex);
            if(outcome.index)
            {
                this->remembered[identity] = outcome.index;
            }
            auto flying = this->in_flight.find(identity);
            if(flying != this->in_flight.end() && flying->second->stamp == stamp)
            {
                this->in_flight.erase(flying);
            }
        }
        promise.set_value(outcome);
    }
    else
    {
        outcome = future.get();
    }

    if(outcome.failed)
    {
        throw LoadError(outcome.reason, pak_name, file_name, locations, outcome.detail, PakKind);
    }
    return outcome.index;
}

std::unordered_map<std::string, std::shared_ptr<ecs::Resource>>
PakLoader::Request(const std::string &pak_name, const std::vector<std::string> *resource_names)
{
    size_t configured_count = 0;
    std::vector<std::string> search_paths = this->SearchPathsGet(pak_name, configured_count);
    NameParser parsed(pak_name, PakKind);

    const std::string file_name = PakFileName(parsed.library);
    SearchResult search = SearchFirst(search_paths, file_name, configured_count);
    if (!search.found)
        NotFoundThrow(search, pak_name, file_name, PakKind);

    try
    {
        PakFile file = PakFile::Open(search.file);
        auto index = this->IndexGet(search.identity.string(), file, pak_name, search.file.string(), search.locations);

        std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> resources;
        if (resource_names == nullptr)
        {
            for (const PakEntry &entry : index->entries)
            {
                auto resource = std::make_shared<ecs::Resource>();
                PakEntryRead(file, entry, *resource);
                resources[entry.name] = std::move(resource);
            }
            return resources;
        }

        // Each requested name once, in request order. Every absent name is
        // collected before any resource is read.
        std::vector<std::string> wanted;
        std::vector<std::string> missing;
        for (const std::string &name : *resource_names)
        {
            if (std::find(wanted.begin(), wanted.end(), name) != wanted.end())
                continue;
            wanted.push_back(name);
            if (index->by_name.find(name) == index->by_name.end())
                missing.push_back(name);
        }
        if (!missing.empty())
        {
            std::string detail = "does not contain ";
            for (size_t i = 0; i < missing.size(); ++i)
                detail += (i == 0 ? "" : ", ") + ("\"" + missing[i] + "\"");
            throw LoadError(LoadError::Reason::ResourceMissing, pak_name, search.file.string(), {}, detail,
                            PakKind, missing);
        }

        for (const std::string &name : wanted)
        {
            auto resource = std::make_shared<ecs::Resource>();
            PakEntryRead(file, index->entries[index->by_name.at(name)], *resource);
            resources[name] = std::move(resource);
        }
        return resources;
    }
    catch (...)
    {
        PakFailureThrow(pak_name, search.file.string(), search.locations, false);
    }
}

std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> PakLoader::Load(const std::string &pak_name)
{
    return this->Request(pak_name, nullptr);
}

std::unordered_map<std::string, std::shared_ptr<ecs::Resource>> PakLoader::Load(const std::string &pak_name, const std::vector<std::string> &resource_names)
{
    return this->Request(pak_name, &resource_names);
}

void PakLoader::PathAdd(const std::string &path)
{
    std::unique_lock lock(this->mutex);
    this->paths.push_back(path);
}

std::vector<std::string> PakLoader::PathsGet() const
{
    std::shared_lock lock(this->mutex);
    return this->paths;
}

std::vector<std::string> PakLoader::SearchPathsGet(const std::string &pak_name) const
{
    size_t configured_count = 0;
    return this->SearchPathsGet(pak_name, configured_count);
}

// Validates the name, then builds the list from one snapshot of the paths
// taken under the lock.
std::vector<std::string> PakLoader::SearchPathsGet(const std::string &pak_name, size_t &configured_count) const
{
    NameParser parsed(pak_name, "resource pak");

    std::shared_lock lock(this->mutex);
    return seed::internal::SearchListBuild(this->paths, parsed.org, parsed.library, true, configured_count);
}
