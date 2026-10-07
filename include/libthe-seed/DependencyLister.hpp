#pragma once

#include <map>
#include <string>
#include <vector>

/**
 * @brief Report for one library that was found but could not be read.
 *
 * The library stays in DependencyResult::dependencies; this record says why
 * what it needs could not be listed.
 */
struct LibraryError
{
    /**
     * @brief Human-readable reason, beginning with the library's path and
     *        naming the format and the structure at fault.
     */
    std::string reason;

    /**
     * @brief Sorted list of the named input binaries that need the library,
     *        directly or through other libraries.
     */
    std::vector<std::string> inputs;
};

/**
 * @brief Result of a dependency listing operation.
 *
 * Contains the successful dependency map, any per-input errors and any
 * per-library errors. The fields may be populated simultaneously (partial
 * success).
 */
struct DependencyResult
{
    /**
     * @brief Reverse dependency map.
     *
     * Key: Resolved absolute filesystem path of a discovered library,
     *      or the recorded name (as found in the binary) if the library
     *      could not be located on the filesystem.
     * Value: List of input binary paths that need this library, directly or
     *        through other libraries.
     */
    std::map<std::string, std::vector<std::string>> dependencies;

    /**
     * @brief Error map for inputs that could not be processed.
     *
     * Key: Input binary path that failed.
     * Value: Human-readable error description.
     *
     * Only inputs named in the request appear here. A library that was found
     * but could not be read is reported in libraryErrors.
     */
    std::map<std::string, std::string> errors;

    /**
     * @brief Report for libraries that were found but could not be read.
     *
     * Key: A key of `dependencies` (the library's resolved path).
     * Value: Why the library could not be read and which inputs need it.
     *
     * A program that ignores this field sees the same `dependencies` and
     * `errors` as without it.
     */
    std::map<std::string, LibraryError> libraryErrors;
};

/**
 * @brief Analyzes binaries to build a reverse dependency map of shared libraries.
 *
 * Parses ELF and PE binary formats directly (no external library dependencies).
 * Supports cross-platform analysis: ELF binaries can be parsed on Windows and
 * PE binaries can be parsed on Linux. Other formats, including Mach-O, are
 * reported as unsupported inputs.
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
 *   // result.libraryErrors: library path -> why it could not be read
 * @endcode
 */
class DependencyLister
{
  public:
    /**
     * @brief Build a complete reverse dependency map for the given binaries.
     *
     * Accepts a list of binary file paths and a list of search directories.
     * For each binary, extracts the names of the libraries it needs and
     * resolves them, and the libraries they need in turn, using the provided
     * search paths.
     *
     * The result maps each discovered library's resolved absolute path
     * (or recorded name if unresolvable) to the list of input binaries
     * that depend on it.
     *
     * Errors for individual binaries are collected in the error map without
     * aborting processing of remaining binaries. A named binary that cannot be
     * read or is malformed appears in the error map with a message naming the
     * format and the structure at fault; it is never reported as having no
     * dependencies. A library that was found but cannot be read or is
     * malformed is reported in `libraryErrors`, not in `errors`.
     *
     * @param binary_paths List of file paths to compiled binaries (ELF or PE).
     * @param search_paths Ordered list of directories to search when resolving
     *        library names to filesystem paths. No platform defaults are used.
     * @return DependencyResult containing the dependency map and error map.
     */
    [[nodiscard]] DependencyResult ListDependencies(
        const std::vector<std::string> &binary_paths,
        const std::vector<std::string> &search_paths
    );
};
