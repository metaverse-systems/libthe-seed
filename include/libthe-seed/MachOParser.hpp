#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Mach-O binary format parser.
 * Detects format, lists architecture slices in fat binaries.
 */
class MachOParser
{
public:
    enum class Format
    {
        MachO32,
        MachO64,
        Fat,
        NotMachO
    };

    struct ArchSlice
    {
        std::uint32_t cpu_type;
        std::uint32_t cpu_subtype;
        std::uint64_t offset;  // file offset of slice
        std::uint64_t size;    // size of slice
    };

    /**
     * Detect the Mach-O format by reading magic bytes.
     */
    static Format DetectFormat(const std::string &file_path);

    /**
     * Check if a file is a Mach-O binary (single or fat).
     */
    static bool IsMachO(const std::string &file_path);

    /**
     * Check if a file is a universal (fat) Mach-O binary.
     */
    static bool IsFatBinary(const std::string &file_path);

    /**
     * List architecture slices in a fat binary.
     * For single-arch binaries, returns a single entry covering the whole file.
     * @throws std::runtime_error if file is not a Mach-O binary
     */
    static std::vector<ArchSlice> GetArchSlices(const std::string &file_path);

    /**
     * List dynamic library dependencies (DT_NEEDED equivalent for Mach-O).
     * Reads LC_LOAD_DYLIB load commands.
     * @throws std::runtime_error if file is not a valid Mach-O binary
     */
    static std::vector<std::string> ListDependencies(const std::string &file_path);
};
