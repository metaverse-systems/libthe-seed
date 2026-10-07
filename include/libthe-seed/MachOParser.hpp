#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Mach-O binary format parser.
 * Detects format, lists architecture slices in fat binaries.
 *
 * Format as written in the file: a file is classified by its first four bytes,
 * not by a host integer, so the answers do not depend on the host byte order.
 *   CF FA ED FE   64-bit little-endian program
 *   CE FA ED FE   32-bit little-endian program
 *   FE ED FA CF   64-bit big-endian program
 *   FE ED FA CE   32-bit big-endian program
 *   CA FE BA BE   universal file, 32-bit offsets
 *   CA FE BA BF   universal file, 64-bit offsets
 * The table of a universal file is big-endian in both forms; the fields of a
 * little-endian program are little-endian. Only 64-bit little-endian programs
 * are interpreted; other programs are recognised and reported as unsupported.
 * Every rejection is a std::runtime_error whose text starts with "Mach-O: ",
 * except the refusal of an unsupported shape by ListDependencies and by the
 * MachOSigner operations: it is "<path>: <kind> Mac programs are not supported
 * (supported: 64-bit little-endian arm64 and x86-64, alone or in a universal
 * file)", with "slice <i> (<arch>): " before the kind for a slice of a universal
 * file. <kind> is "big-endian", "32-bit" or "32-bit big-endian".
 *
 * Format::MachO32 is a 32-bit program of either byte order and Format::MachO64
 * a 64-bit one of either byte order. Of these only 64-bit little-endian is
 * supported; MachO32, and MachO64 in big-endian, are the declined shapes. The
 * first bytes alone decide, so Format::Fat can also hold declined slices. Signing
 * a universal file with any declined slice is refused whole, leaving the file
 * unchanged.
 */
class MachOParser
{
public:
    enum class Format
    {
        MachO32, // 32-bit program, either byte order
        MachO64, // 64-bit program, either byte order
        Fat,     // universal file, either offset width
        NotMachO
    };

    struct ArchSlice
    {
        std::uint32_t cpu_type;
        std::uint32_t cpu_subtype;
        std::uint64_t offset;  // file offset of slice
        std::uint64_t size;    // size of slice
        bool is_signed = false;                  // the slice has an LC_CODE_SIGNATURE command
        std::uint64_t signature_offset = 0;      // offset of the signature data, relative to the slice start
        std::uint64_t signature_size = 0;        // size of the signature data in bytes
        bool supported = true;                   // false for a 32-bit or big-endian program
        std::string unsupported_reason;          // empty when supported
    };

    /**
     * Detect the Mach-O format by reading magic bytes.
     * Answers NotMachO for a file without a Mach-O magic, including a very short one.
     * @throws std::runtime_error if the file cannot be read
     */
    static Format DetectFormat(const std::string &file_path);

    /**
     * Check if a file is a Mach-O binary (single or fat).
     * @throws std::runtime_error if the file cannot be read
     */
    static bool IsMachO(const std::string &file_path);

    /**
     * Check if a file is a universal (fat) Mach-O binary.
     * @throws std::runtime_error if the file cannot be read
     */
    static bool IsFatBinary(const std::string &file_path);

    /**
     * List architecture slices in a fat binary.
     * For single-arch binaries, returns a single entry covering the whole file.
     * A slice that is not supported is listed with supported false and the reason;
     * its signature fields are not read.
     * @throws std::runtime_error if file is not a Mach-O binary or the slice table or
     *         a supported slice's load commands are malformed
     */
    static std::vector<ArchSlice> GetArchSlices(const std::string &file_path);

    /**
     * List dynamic library dependencies (DT_NEEDED equivalent for Mach-O).
     * Reads LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB and
     * LC_LOAD_UPWARD_DYLIB commands of every slice, in file order, each name once.
     * A slice that is not supported is an error, not an empty answer.
     * @throws std::runtime_error if file is not a valid Mach-O binary or its load commands are malformed
     */
    static std::vector<std::string> ListDependencies(const std::string &file_path);
};
