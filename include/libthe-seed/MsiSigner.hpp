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
 *
 * What the library claims. The fingerprint it computes (see
 * ComputeAuthenticodeDigest) equals the one osslsigncode 2.14 calculates for
 * the same package, and the packages it writes are read back by osslsigncode
 * and by an independent reader written for the tests. It makes no claim about
 * the CMS blob it is given: the cryptography, the certificate and its trust are never checked,
 * and nothing was checked with Microsoft's signtool, WinVerifyTrust or the
 * Windows Installer service, because no Windows machine is part of the test
 * setup.
 *
 * Every write (EmbedSignature, StripSignature) rebuilds the whole package in
 * one canonical layout, so signing and stripping repeatedly never grows the
 * file and the output depends only on the content and the signature. Sectors
 * that nothing refers to, free directory entries and the old signature streams
 * are not carried over.
 */
class MsiSigner
{
public:
    /** Result of computing an MSI Authenticode digest. */
    struct DigestResult
    {
        std::vector<std::uint8_t> digest;   // SHA-256 hash (32 bytes)
    };

    /** What CheckSignature found. */
    enum class SignatureState
    {
        None,       // no \x05DigitalSignature stream, or one of size zero
        Matches,    // readable; its SHA-256 fingerprint equals the package's
        Mismatch,   // readable; its fingerprint differs from the package's
        Unreadable  // present, but damaged, not a signature structure, or not SHA-256
    };

    /** Result of CheckSignature. */
    struct SignatureCheck
    {
        SignatureState state = SignatureState::None;
        std::vector<std::uint8_t> stored_digest;   // from the signature; empty when it cannot be read
        std::vector<std::uint8_t> computed_digest; // fingerprint of the package
        std::string detail;                        // short reason for Mismatch and Unreadable
    };

    /**
     * Check if a file is an OLE Compound Document (potential MSI).
     * Reads first 8 bytes and checks for CFBF magic: D0 CF 11 E0 A1 B1 1A E1
     * @returns true if the file has a valid CFBF signature; false for any file
     *          without it, including a very short one or one that cannot be opened
     */
    static bool IsMsi(const std::string &file_path);

    /**
     * Compute the Authenticode fingerprint (SHA-256) of an MSI file, by the
     * rule osslsigncode uses. For each storage, starting at the root: its
     * children are ordered by the raw bytes of their UTF-16LE names (the
     * shorter name first when one begins with the other, no case folding);
     * for each child in that order a stream contributes its bytes and a
     * storage contributes, recursively, the same; after the children the
     * storage's 16-byte class identifier follows. Names are not hashed. In
     * the root, the \x05DigitalSignature and \x05MsiDigitalSignatureEx
     * streams are left out, so the value is the same before signing, after
     * signing and after the signature is removed.
     * @throws std::runtime_error if file is not a valid CFBF, is malformed, or
     *         holds two entries with the same name in one storage
     */
    [[nodiscard]] static DigestResult ComputeAuthenticodeDigest(const std::string &file_path);

    /**
     * Embed a PKCS#7/CMS SignedData blob as an Authenticode signature.
     * Rebuilds the package with the blob as its \x05DigitalSignature stream:
     * in the mini stream when the blob is smaller than the header's cut-off
     * (4,096 bytes) and in ordinary sectors otherwise, and with the entry
     * placed where the format's ordering puts it, so a reader that searches
     * the directory finds it. An existing signature is replaced without being
     * read (even a damaged one, or one written by an earlier version), and
     * \x05MsiDigitalSignatureEx is dropped and never created. Everything else
     * (names, bytes, class identifiers, state bits, times) is kept. The same
     * package and blob always give the same bytes.
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
     * @param file_path Path to MSI file (replaced in place)
     * @param pkcs7_der DER-encoded PKCS#7 SignedData blob; must not be empty
     * @param require_matching_digest When true, the blob must hold a SHA-256
     *        fingerprint equal to the fingerprint of the package that is
     *        written; otherwise the call is refused (the message names the
     *        file and says "signature does not match the package contents").
     *        When false any non-empty blob is accepted. Only the fingerprint
     *        stored in the blob is compared; its cryptography is not checked.
     * @throws std::runtime_error if the blob is empty, the file is not a valid
     *         CFBF, is malformed, holds two entries with one name in a
     *         storage, needs more sectors than the format allows, or is
     *         read-only; a rejected call leaves the file unchanged
     */
    static void EmbedSignature(
        const std::string &file_path,
        const std::vector<std::uint8_t> &pkcs7_der,
        bool require_matching_digest = false
    );

    /**
     * Extract the embedded Authenticode signature from an MSI file.
     * The stream is read by its recorded size and the header's cut-off: from
     * the mini stream below the cut-off, from ordinary sectors at or above it.
     * @returns DER-encoded PKCS#7 blob, or nullopt if \x05DigitalSignature
     *          stream does not exist or is empty
     * @throws std::runtime_error if file is not a valid CFBF or is malformed,
     *         or the signature's chain does not fit its size
     */
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> ExtractSignature(
        const std::string &file_path
    );

    /**
     * Check if an MSI file has an embedded Authenticode signature.
     * Neither the signature's content nor its cryptography is read or checked:
     * use CheckSignature to see whether it belongs to the package.
     * @returns true if \x05DigitalSignature stream exists, is non-empty and its
     *          chain fits its size
     * @throws std::runtime_error if file is not a valid CFBF or is malformed,
     *         or the signature's chain does not fit its size
     */
    static bool HasEmbeddedSignature(const std::string &file_path);

    /**
     * Strip any existing embedded signature from an MSI file.
     * Removes the \x05DigitalSignature and \x05MsiDigitalSignatureEx
     * streams by rebuilding the package without them. Neither stream is read,
     * so a damaged signature can be removed.
     * @returns true when something was removed; false when neither stream
     *          existed, in which case the file is left byte for byte as it was
     * @throws std::runtime_error if file is not a valid CFBF or is malformed
     */
    static bool StripSignature(const std::string &file_path);

    /**
     * Compare the signature of a package with the package. Reads the file only.
     * Only the fingerprint stored in the signature is compared with the
     * fingerprint computed for the package: the CMS cryptography, the
     * certificate and its trust are not checked, so Matches does not say the
     * signature is valid.
     * A bad signature is a state, not an exception.
     * @throws std::runtime_error only when the package itself cannot be read
     */
    [[nodiscard]] static SignatureCheck CheckSignature(const std::string &file_path);
};
