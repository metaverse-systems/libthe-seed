#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

/**
 * Authenticode PE binary signature operations.
 * All methods are static — no instance state required.
 * Works cross-platform (no Windows API dependency).
 */
class PeSigner
{
public:
    /** Result of computing an Authenticode digest. */
    struct DigestResult
    {
        std::vector<std::uint8_t> digest;   // SHA-256 hash (32 bytes)
        bool is_pe32_plus;                  // true = PE32+, false = PE32
    };

    /**
     * Compute the Authenticode digest (SHA-256) for a PE file.
     * Excludes the CheckSum field, Certificate Table DD entry,
     * and any existing certificate data from the hash.
     * @throws std::runtime_error if file is not a valid PE
     */
    static DigestResult ComputeAuthenticodeDigest(const std::string &file_path);

    /**
     * Embed a PKCS#7/CMS SignedData blob as an Authenticode signature.
     * Appends WIN_CERTIFICATE at end of file, updates DD entry 4,
     * and recalculates the PE checksum.
     * Uses atomic write: writes to temp file, then renames.
     * @param file_path Path to PE binary (modified in-place via atomic swap)
     * @param pkcs7_der DER-encoded PKCS#7 SignedData blob
     * @throws std::runtime_error if file is not a valid PE or is read-only
     */
    static void EmbedSignature(
        const std::string &file_path,
        const std::vector<std::uint8_t> &pkcs7_der
    );

    /**
     * Extract the embedded Authenticode signature from a PE file.
     * @returns DER-encoded PKCS#7 blob, or nullopt if no signature present
     * @throws std::runtime_error if file is not a valid PE
     */
    static std::optional<std::vector<std::uint8_t>> ExtractSignature(
        const std::string &file_path
    );

    /**
     * Check if a PE file has an embedded Authenticode signature.
     * @throws std::runtime_error if file is not a valid PE
     */
    static bool HasEmbeddedSignature(const std::string &file_path);

    /**
     * Strip any existing embedded signature from a PE file.
     * Zeroes the DD entry 4 and truncates certificate data.
     * @throws std::runtime_error if file is not a valid PE
     */
    static void StripSignature(const std::string &file_path);

private:
    /**
     * Recalculate and update the PE checksum after modifications.
     * @param bytes Mutable reference to file bytes (checksum updated in-place)
     * @param checksum_offset Offset of the CheckSum field in the optional header
     */
    static void RecalcChecksum(
        std::vector<std::uint8_t> &bytes,
        std::size_t checksum_offset
    );
};
