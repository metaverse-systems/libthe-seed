#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace seed::internal
{

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
