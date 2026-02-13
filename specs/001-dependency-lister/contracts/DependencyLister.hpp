#pragma once

#include <string>
#include <vector>
#include <map>

/**
 * @brief Result of a dependency listing operation.
 *
 * Contains both the successful dependency map and any per-file errors.
 * Both fields may be populated simultaneously (partial success).
 */
struct DependencyResult
{
    /**
     * @brief Reverse dependency map.
     *
     * Key: Resolved absolute filesystem path of a discovered library,
     *      or the recorded name (as found in the binary) if the library
     *      could not be located on the filesystem.
     * Value: List of input binary paths that depend on this library
     *        (directly or transitively).
     */
    std::map<std::string, std::vector<std::string>> dependencies;

    /**
     * @brief Error map for inputs that could not be processed.
     *
     * Key: Input binary path that failed.
     * Value: Human-readable error description.
     */
    std::map<std::string, std::string> errors;
};

/**
 * @brief Analyzes binaries to build a reverse dependency map of shared libraries.
 *
 * Parses ELF and PE binary formats directly (no external library dependencies).
 * Supports cross-platform analysis: ELF binaries can be parsed on Windows and
 * PE binaries can be parsed on Linux.
 *
 * Usage:
 * @code
 *   DependencyLister lister;
 *   auto result = lister.ListDependencies(
 *       {"./build/myapp", "./build/libfoo.so"},
 *       {"/usr/lib", "/usr/local/lib", "./build/libs"}
 *   );
 *   // result.dependencies: library path -> [project files that need it]
 *   // result.errors: file path -> error description
 * @endcode
 */
class DependencyLister
{
  public:
    /**
     * @brief Build a complete reverse dependency map for the given binaries.
     *
     * Accepts a list of binary file paths and a list of search directories.
     * For each binary, extracts shared library dependencies (DT_NEEDED for ELF,
     * import table for PE) and recursively resolves transitive dependencies
     * using the provided search paths.
     *
     * The result maps each discovered library's resolved absolute path
     * (or recorded name if unresolvable) to the list of input binaries
     * that depend on it.
     *
     * Errors for individual binaries are collected in the error map without
     * aborting processing of remaining binaries.
     *
     * @param binary_paths List of file paths to compiled binaries (ELF or PE).
     * @param search_paths Ordered list of directories to search when resolving
     *        library names to filesystem paths. No platform defaults are used.
     * @return DependencyResult containing the dependency map and error map.
     */
    DependencyResult ListDependencies(
        const std::vector<std::string> &binary_paths,
        const std::vector<std::string> &search_paths
    );
};
