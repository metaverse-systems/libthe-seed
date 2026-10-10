#pragma once

#include <exception>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "PluginSearch.hpp"

namespace seed::internal
{

// The shared state and loading logic of ComponentLoader and SystemLoader.
// `Creator` is the type of the plugin's entry point.
//
// One load per plugin name per cache: the first caller for a name does the
// work, every other caller for that name waits for the same result. A failure
// is delivered to every waiter and is not remembered, so the next request
// tries again. A plugin file is opened once per cache however many names
// resolve to it. No lock is held while the file system is searched, while a
// plugin is opened or while a creator runs; only the short bookkeeping steps
// take the mutex.
template <typename Creator> class PluginCache
{
  public:
    struct Entry
    {
        Creator creator = nullptr;
        std::string file;
    };

    Entry Get(const std::string &name, const std::string &org, const std::string &library,
              const std::string &symbol, const std::string &kind)
    {
        for(;;)
        {
            std::shared_future<Outcome> pending;
            std::promise<Outcome> promise;
            std::vector<std::string> locations;
            size_t configured_count = 0;
            bool owner = false;

            {
                std::shared_lock lock(this->mutex);
                auto it = this->entries.find(name);
                if(it != this->entries.end())
                {
                    return it->second;
                }
            }

            {
                std::unique_lock lock(this->mutex);
                auto it = this->entries.find(name);
                if(it != this->entries.end())
                {
                    return it->second;
                }

                auto flight = this->in_flight.find(name);
                if(flight != this->in_flight.end())
                {
                    pending = flight->second.future;
                    if(flight->second.version == this->version)
                    {
                        // Same settings: share the result of the load already running.
                        lock.unlock();
                        return Deliver(pending.get());
                    }
                }
                else
                {
                    // Snapshot the settings here; the search below runs unlocked.
                    owner = true;
                    pending = promise.get_future().share();
                    this->in_flight.emplace(name, InFlight{pending, this->version});
                    locations = SearchListBuild(this->paths, org, library, false, configured_count);
                }
            }

            if(!owner)
            {
                // The running load started before a location was added. Let it
                // finish, then look again so this request sees the current
                // locations.
                try
                {
                    pending.wait();
                }
                catch(...)
                {
                }
                continue;
            }

            try
            {
                Entry entry = this->Load(name, library, symbol, kind, locations, configured_count);
                {
                    std::unique_lock lock(this->mutex);
                    this->entries[name] = entry;
                    this->in_flight.erase(name);
                }
                Outcome outcome;
                outcome.entry = entry;
                promise.set_value(std::move(outcome));
                return entry;
            }
            catch(const LoadError &failure)
            {
                this->Fail(name, promise, failure, nullptr);
                throw LoadError(failure);
            }
            catch(...)
            {
                std::exception_ptr error = std::current_exception();
                this->Fail(name, promise, std::nullopt, error);
                std::rethrow_exception(error);
            }
        }
    }

    void PathAdd(const std::string &path)
    {
        std::unique_lock lock(this->mutex);
        this->paths.push_back(path);
        ++this->version;
    }

    std::vector<std::string> PathsGet() const
    {
        std::shared_lock lock(this->mutex);
        return this->paths;
    }

    std::vector<std::string> SearchPathsGet(const std::string &org, const std::string &library) const
    {
        std::shared_lock lock(this->mutex);
        size_t configured_count = 0;
        return SearchListBuild(this->paths, org, library, false, configured_count);
    }

  private:
    // What a finished load hands to everyone waiting for it. A failure is held
    // as a value and each waiter throws its own copy, so no thread shares an
    // exception object with another.
    struct Outcome
    {
        Entry entry;
        std::optional<LoadError> error;
        std::exception_ptr other;
    };

    static Entry Deliver(const Outcome &outcome)
    {
        if(outcome.error)
        {
            throw LoadError(*outcome.error);
        }
        if(outcome.other)
        {
            std::rethrow_exception(outcome.other);
        }
        return outcome.entry;
    }

    // Removes the in-flight record and tells the waiters the load failed. The
    // failure is not remembered.
    void Fail(const std::string &name, std::promise<Outcome> &promise, std::optional<LoadError> error,
              std::exception_ptr other)
    {
        {
            std::unique_lock lock(this->mutex);
            this->in_flight.erase(name);
        }
        Outcome outcome;
        outcome.error = std::move(error);
        outcome.other = other;
        promise.set_value(std::move(outcome));
    }

    struct InFlight
    {
        std::shared_future<Outcome> future;
        unsigned long long version = 0;
    };

    Entry Load(const std::string &name, const std::string &library, const std::string &symbol,
               const std::string &kind, const std::vector<std::string> &locations, size_t configured_count)
    {
        const std::string file_name = PluginFileName(library);
        SearchResult search = SearchFirst(locations, file_name, configured_count);
        if(!search.found)
        {
            NotFoundThrow(search, name, file_name, kind);
        }

        void *handle = nullptr;
        {
            // Opens are serialised so two names reaching one file open it once.
            std::lock_guard open_lock(this->open_mutex);
            auto known = this->libraries.find(search.identity.string());
            if(known != this->libraries.end())
            {
                handle = known->second;
            }
            else
            {
                std::string error;
                handle = OpenPinned(search.file, error);
                if(handle == nullptr)
                {
                    NotLoadableThrow(search, name, error, kind);
                }
                this->libraries.emplace(search.identity.string(), handle);
            }
        }

        std::string error;
        void *address = SymbolFind(handle, symbol, error);
        if(address == nullptr)
        {
            EntryPointMissingThrow(search.file, name, symbol, error, kind);
        }

        Entry entry;
        entry.creator = reinterpret_cast<Creator>(address);
        entry.file = search.file.string();
        return entry;
    }

    mutable std::shared_mutex mutex;
    std::vector<std::string> paths;
    unsigned long long version = 0;
    std::map<std::string, Entry> entries;
    std::map<std::string, InFlight> in_flight;

    std::mutex open_mutex;
    std::map<std::string, void *> libraries;
};

} // namespace seed::internal
