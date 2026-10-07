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
     * The fingerprint of an unsigned program includes the zero padding that
     * embedding adds (up to seven bytes, to the next 8-byte boundary), so it
     * equals the fingerprint of the same program after embedding.
     * @throws std::runtime_error if file is not a valid PE or is malformed
     */
    [[nodiscard]] static DigestResult ComputeAuthenticodeDigest(const std::string &file_path);

    /**
     * Embed a PKCS#7/CMS SignedData blob as an Authenticode signature.
     * Appends WIN_CERTIFICATE at end of file, updates DD entry 4,
     * and recalculates the PE checksum.
     * The file is replaced as one step: after a failure or a crash of the
     * process it holds the complete old or the complete new content, and no
     * temporary file remains when the call returns. Permissions are preserved
     * (Linux mode bits, and owner and group where the account may; Windows file
     * attributes), extended attributes and ACLs are not. Data is flushed to
     * storage before the replacement and the folder after it where the platform
     * supports it; this protects against loss of power only on storage that
     * honours those requests and is not a guarantee. A process killed during
     * the replacement can leave a working file named
     * `.<name>.seedtmp.<pid>.<hex>` in the same folder; it is never reused and
     * can be deleted. A link is followed: the file it points to is replaced and
     * the link is kept. Read-only files are refused.
     * @param file_path Path to PE binary (replaced in place)
     * @param pkcs7_der DER-encoded PKCS#7 SignedData blob
     * @throws std::runtime_error if file is not a valid PE, is malformed or read-only;
     *         a rejected call leaves the file unchanged
     */
    static void EmbedSignature(
        const std::string &file_path,
        const std::vector<std::uint8_t> &pkcs7_der
    );

    /**
     * Extract the embedded Authenticode signature from a PE file.
     * @returns DER-encoded PKCS#7 blob, or nullopt if no signature present
     * @throws std::runtime_error if file is not a valid PE or is malformed
     */
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> ExtractSignature(
        const std::string &file_path
    );

    /**
     * Check if a PE file has an embedded Authenticode signature.
     * @throws std::runtime_error if file is not a valid PE or is malformed
     */
    static bool HasEmbeddedSignature(const std::string &file_path);

    /**
     * Strip any existing embedded signature from a PE file.
     * Zeroes the DD entry 4 and truncates certificate data.
     * Removes up to seven zero bytes of padding immediately before the
     * certificate, never below the end of the last section's data, so a program
     * whose last byte is not zero is restored to its original length and
     * content, except that the CheckSum field holds the calculated checksum;
     * one that ends in zeros may lose up to seven of them when its length was
     * a multiple of eight.
     * The file is replaced as described for EmbedSignature.
     * @throws std::runtime_error if file is not a valid PE or is malformed
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
