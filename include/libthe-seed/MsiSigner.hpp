#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

/**
 * MSI (OLE Compound Document) Authenticode signature operations.
 * All methods are static — no instance state required.
 * Works cross-platform (no Windows API or libgsf dependency).
 *
 * MSI files use the OLE Compound Document File Format (CFBF).
 * Authenticode signatures are stored in the \x05DigitalSignature
 * stream inside the compound document, using the same CMS/PKCS#7
 * format as PE Authenticode but with SpcSipInfo content type.
 */
class MsiSigner
{
public:
    /** Result of computing an MSI Authenticode digest. */
    struct DigestResult
    {
        std::vector<std::uint8_t> digest;   // SHA-256 hash (32 bytes)
    };

    /**
     * Check if a file is an OLE Compound Document (potential MSI).
     * Reads first 8 bytes and checks for CFBF magic: D0 CF 11 E0 A1 B1 1A E1
     * @returns true if the file has a valid CFBF signature
     */
    static bool IsMsi(const std::string &file_path);

    /**
     * Compute the Authenticode digest (SHA-256) for an MSI file.
     * The digest covers all stream data in the compound document
     * EXCEPT the \x05DigitalSignature and \x05MsiDigitalSignatureEx
     * streams, enumerated recursively and sorted alphabetically.
     * @throws std::runtime_error if file is not a valid CFBF
     */
    static DigestResult ComputeAuthenticodeDigest(const std::string &file_path);

    /**
     * Embed a PKCS#7/CMS SignedData blob as an Authenticode signature.
     * Writes the blob to the \x05DigitalSignature stream inside the
     * compound document. Creates the stream if it does not exist;
     * replaces it if it does.
     * Uses atomic write: writes to temp file, then renames.
     * @param file_path Path to MSI file (modified in-place via atomic swap)
     * @param pkcs7_der DER-encoded PKCS#7 SignedData blob
     * @throws std::runtime_error if file is not a valid CFBF or is read-only
     */
    static void EmbedSignature(
        const std::string &file_path,
        const std::vector<std::uint8_t> &pkcs7_der
    );

    /**
     * Extract the embedded Authenticode signature from an MSI file.
     * @returns DER-encoded PKCS#7 blob, or nullopt if \x05DigitalSignature
     *          stream does not exist
     * @throws std::runtime_error if file is not a valid CFBF
     */
    static std::optional<std::vector<std::uint8_t>> ExtractSignature(
        const std::string &file_path
    );

    /**
     * Check if an MSI file has an embedded Authenticode signature.
     * @returns true if \x05DigitalSignature stream exists and is non-empty
     * @throws std::runtime_error if file is not a valid CFBF
     */
    static bool HasEmbeddedSignature(const std::string &file_path);

    /**
     * Strip any existing embedded signature from an MSI file.
     * Removes the \x05DigitalSignature and \x05MsiDigitalSignatureEx
     * streams from the compound document.
     * @throws std::runtime_error if file is not a valid CFBF
     */
    static void StripSignature(const std::string &file_path);
};
