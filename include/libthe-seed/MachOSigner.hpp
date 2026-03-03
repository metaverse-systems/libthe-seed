#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

/**
 * Mach-O binary code signature operations.
 * All methods are static — no instance state required.
 * Works cross-platform (no macOS API dependency).
 */
class MachOSigner
{
public:
    /** Result of computing a CodeDirectory. */
    struct CodeDirectoryResult
    {
        std::vector<std::uint8_t> code_directory;  // Serialized CodeDirectory blob
        std::vector<std::uint8_t> cd_hash;         // SHA-256 hash of the CodeDirectory
    };

    /**
     * Compute the CodeDirectory blob for a Mach-O binary.
     * Performs page-based SHA-256 hashing (4096-byte pages).
     * Includes special slot for empty requirements hash.
     * @param file_path Path to the Mach-O binary
     * @param identity Code signing identity string (e.g., CN from cert)
     * @throws std::runtime_error if file is not a valid Mach-O
     */
    static CodeDirectoryResult ComputeCodeDirectory(
        const std::string &file_path,
        const std::string &identity
    );

    /**
     * Build a complete CS_SuperBlob containing CodeDirectory,
     * empty Requirements, and a CMS signature wrapper.
     * @param code_directory The CodeDirectory blob
     * @param cms_signature DER-encoded CMS SignedData blob
     * @returns Serialized SuperBlob ready for embedding
     */
    static std::vector<std::uint8_t> BuildSuperBlob(
        const std::vector<std::uint8_t> &code_directory,
        const std::vector<std::uint8_t> &cms_signature
    );

    /**
     * Embed a code signature SuperBlob into a Mach-O binary.
     * Adds/updates LC_CODE_SIGNATURE load command and appends
     * signature data to __LINKEDIT segment.
     * Uses atomic write for crash safety.
     * @throws std::runtime_error if file is not a valid Mach-O or read-only
     */
    static void EmbedSignature(
        const std::string &file_path,
        const std::vector<std::uint8_t> &super_blob
    );

    /**
     * Extract the embedded code signature (SuperBlob) from a Mach-O binary.
     * @returns SuperBlob bytes, or nullopt if no signature present
     */
    static std::optional<std::vector<std::uint8_t>> ExtractSignature(
        const std::string &file_path
    );

    /**
     * Check if a Mach-O binary has an embedded code signature.
     */
    static bool HasEmbeddedSignature(const std::string &file_path);

    /**
     * Extract the CMS signature blob from a SuperBlob.
     * @returns DER-encoded CMS blob, or nullopt if not present
     */
    static std::optional<std::vector<std::uint8_t>> ExtractCmsFromSuperBlob(
        const std::vector<std::uint8_t> &super_blob
    );

    /**
     * Extract the CodeDirectory blob from a SuperBlob.
     * @returns CodeDirectory bytes, or nullopt if not present
     */
    static std::optional<std::vector<std::uint8_t>> ExtractCodeDirectoryFromSuperBlob(
        const std::vector<std::uint8_t> &super_blob
    );
};
