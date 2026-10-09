#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <libthe-seed/LoadError.hpp>

namespace seed::internal
{

// The platform file name of the plugin library `library`: lib<library>.so,
// lib<library>-0.dll or lib<library>.dylib.
std::string PluginFileName(const std::string &library);

// The file name of the resource pak `library`: <library>.pak.
std::string PakFileName(const std::string &library);

// What a search through a list of locations found. `locations` holds every
// location in search order with its state: Missing when it is not a
// directory, Searched when it is one without the file, Found for the one that
// holds the file and NotReached for those after it. When `found` is true,
// `file` is the absolute path of the deciding file and `identity` its
// canonical path, which names the file regardless of how it was reached.
struct SearchResult
{
    bool found = false;
    std::filesystem::path file;
    std::filesystem::path identity;
    std::vector<LoadError::Location> locations;
};

// Looks for `file_name` in `locations` in order. The first location holding a
// regular file of that name decides; later locations are not looked at.
// Relative locations are resolved against the working directory at the time
// of the call. Nothing is searched but the given locations. Never throws for
// a missing location or file.
SearchResult SearchFirst(const std::vector<std::string> &locations, const std::string &file_name);

// Throws LoadError (NotFound) for a search that found nothing.
[[noreturn]] void NotFoundThrow(const SearchResult &result, const std::string &name,
                                const std::string &file_name, const std::string &kind);

// Throws LoadError (NotLoadable) for the deciding file of `result`, with the
// platform's or parser's `reason`. The search stops at that file.
[[noreturn]] void NotLoadableThrow(const SearchResult &result, const std::string &name,
                                   const std::string &reason, const std::string &kind);

// Searches `locations` for the plugin library `library` and opens the file
// that decides, pinned, by its absolute path. `name` and `kind` only shape
// error messages. Throws LoadError: NotFound when no location holds the file,
// NotLoadable when the deciding file cannot be opened (no later location is
// tried). On success `result` describes the search.
void *PluginOpen(const std::vector<std::string> &locations, const std::string &library,
                 const std::string &name, const std::string &kind, SearchResult &result);

// Address of `symbol` in an opened plugin, or nullptr with the platform's
// message in `error`.
void *SymbolFind(void *handle, const std::string &symbol, std::string &error);

// Opens the plugin at `absolute_path` and pins it, so the platform never
// unmaps its code while the process runs. Closing the returned handle only
// drops a reference.
//
// Returns the platform handle (a HMODULE on Windows), or nullptr on failure,
// in which case `error` holds the platform's message. The message is captured
// on the calling thread immediately after the failing call. An opened plugin
// that cannot be pinned is released and reported as a failure.
void *OpenPinned(const std::filesystem::path &absolute_path, std::string &error);

// Number of OpenPinned calls since the process started or since the last
// reset, failed calls included. Tests use it to check how often a plugin is
// opened; it has no effect on behaviour.
std::uint64_t PluginOpenCallCount();
void ResetPluginOpenCallCount();

} // namespace seed::internal
