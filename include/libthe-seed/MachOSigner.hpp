#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

/**
 * Mach-O binary code signature operations.
 * All methods are static: no instance state required.
 * Works cross-platform (no macOS API dependency).
 *
 * Signing is two calls, PrepareSignature and then CompleteSignature, and no
 * other order exists. The CodeDirectory fingerprints every 4096-byte page of
 * the file up to the start of the signature data, so it can only be computed
 * after the file has its final layout: the new load command, the new header
 * counts, the extended __LINKEDIT segment and the region reserved for the
 * signature. PrepareSignature builds that layout in memory, never writes, and
 * returns the CodeDirectory of each slice; the caller signs it (the CMS is
 * produced outside this library); CompleteSignature builds the layout again,
 * requires the CodeDirectories to be the prepared ones, and replaces the file.
 * The reserved region is part of the file: the SuperBlob follows the data
 * offset and zero bytes fill the rest up to the end of the reserved size.
 *
 * Signing a program that already carries a signature replaces it in place:
 * the one signature command is updated, never duplicated or left with size
 * zero, and the old data is cut off. A signature followed by other data is
 * refused. StripSignature removes a signature.
 *
 * Only 64-bit little-endian arm64 and x86-64 programs, alone or in a universal
 * file, are supported. Error text begins with the path or with "Mach-O".
 *
 * A program without room between its load commands and its first section for
 * the 16-byte signature command is refused and left unchanged; relink with
 * extra header space (for example -headerpad 0x20).
 *
 * What was checked: page fingerprints in a signature produced by this library
 * match the finished file (an independent checker and values recorded from
 * Python's hashlib); the checker accepts the ad-hoc signature written by
 * ld64.lld, a different producer; the structure of signed files parses with
 * llvm-otool, llvm-objdump and llvm-lipo.
 *
 * Nobody has checked a signature produced by this library with the platform's
 * own verifier (`codesign --verify`) or by running a signed program on a Mac.
 * What was checked is described above. The CMS part of the signature is the
 * minimal one the-seed has always produced and is not known to be accepted by
 * Apple's tools. Programs signed by earlier versions of the-seed are malformed
 * after re-signing and may need to be rebuilt from their unsigned originals.
 */
class MachOSigner
{
public:
    /** The CodeDirectory of one program (one slice of a universal file). */
    struct PreparedSlice
    {
        std::uint32_t cpu_type;                     // CPU type of the slice
        std::uint32_t cpu_subtype;                  // CPU subtype of the slice
        std::vector<std::uint8_t> code_directory;   // computed over the finished layout
        std::vector<std::uint8_t> cd_hash;          // SHA-256 of code_directory
    };

    /**
     * What PrepareSignature returns and CompleteSignature checks. It holds no
     * reference to the file: a copy with the same bytes can be completed
     * elsewhere, in another process.
     */
    struct PreparedSignature
    {
        std::string identity;                       // identifier written into every CodeDirectory
        std::uint32_t cms_capacity;                 // bytes reserved per slice for the DER CMS
        std::vector<PreparedSlice> slices;          // table order; one for a thin file
    };

    /**
     * Compute the CodeDirectory of every slice for the file as it will be
     * after signing. The file is not changed, whether the call succeeds or
     * throws.
     *
     * Each CodeDirectory has version 0x20400, SHA-256 fingerprints over
     * 4096-byte pages, two special slots (requirements, Info.plist absent) and
     * covers exactly the bytes before the signature data (codeLimit equals the
     * data offset). It contains no time and no random value: equal inputs
     * (file bytes, identity, capacity) give an identical CodeDirectory. Each
     * slice of a universal file is its own program and gets its own.
     *
     * An existing signature is not an obstacle: the layout is computed for
     * replacing it in place.
     *
     * @param file_path Path to the Mach-O program (thin or universal)
     * @param identity Code signing identifier written into the CodeDirectory
     * @param cms_capacity Upper bound in bytes of the DER CMS the caller will
     *        supply for any one slice; 0 is allowed; above 2^31 is refused
     * @throws std::runtime_error if the file is not a supported Mach-O program,
     *         has no room for the signature command (when it has no signature
     *         yet) or no link-edit segment at its end, has data after its
     *         signature, has non-zero bytes between the slices of a universal
     *         file, or a slice is refused (the message names the slice of a
     *         universal file)
     */
    [[nodiscard]] static PreparedSignature PrepareSignature(
        const std::string &file_path,
        const std::string &identity,
        std::uint32_t cms_capacity
    );

    /**
     * Write the signature into the file: the layout is built again, the
     * CodeDirectories recomputed from the file must equal `prepared` byte for
     * byte, and the finished file replaces the old one in a single step.
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
     *
     * The command is added after the last load command (the header counts grow
     * by one and 16), the signature data starts at the 16-byte aligned end of
     * the program, and __LINKEDIT ends where the signature region ends (its
     * memory size is rounded to 16384 bytes for arm64 and 4096 for x86-64).
     * A universal file keeps its table form (32 or 64 bit), the
     * order and alignment of its slices, with offsets and lengths rewritten.
     * Signing is all slices or none.
     *
     * @param cms_signatures One DER CMS per slice, in the order of `prepared.slices`
     * @throws std::runtime_error if the program, the identity or the capacity
     *         changed since PrepareSignature; if the number of signatures is
     *         not the number of slices; if a signature is larger than the
     *         reserved capacity ("prepare again with a larger capacity"); or
     *         if the file is not a supported program or is read-only. A
     *         refused call leaves the whole file unchanged.
     */
    static void CompleteSignature(
        const std::string &file_path,
        const PreparedSignature &prepared,
        const std::vector<std::vector<std::uint8_t>> &cms_signatures
    );

    /**
     * Remove the code signature of a program (every slice of a universal
     * file). The signature command leaves the load command table (later
     * commands move up, the freed bytes are zero), ncmds and sizeofcmds are
     * lowered, the file is cut where the signature data began and __LINKEDIT
     * ends at the cut. A program without a signature is left as it is and the
     * file is not written.
     * @throws std::runtime_error if the file is not a supported program, or if
     *         data follows the signature (as for signing); the file is then
     *         unchanged
     */
    static void StripSignature(const std::string &file_path);

    /**
     * Build a complete CS_SuperBlob containing CodeDirectory,
     * empty Requirements, and a CMS signature wrapper.
     * This only formats bytes: it reads no file and is not a signing step.
     * @param code_directory The CodeDirectory blob
     * @param cms_signature DER-encoded CMS SignedData blob
     * @returns Serialized SuperBlob
     */
    [[nodiscard]] static std::vector<std::uint8_t> BuildSuperBlob(
        const std::vector<std::uint8_t> &code_directory,
        const std::vector<std::uint8_t> &cms_signature
    );

    /**
     * Extract the embedded code signature from a Mach-O binary.
     * Returns the SuperBlob (its own length) when the signature region starts
     * with the embedded-signature magic and the length fits the region, so the
     * zero bytes that follow it are not part of the answer; otherwise the whole
     * region. For a universal file the answer is that of the first slice that is
     * a supported program.
     * @returns signature bytes, or nullopt if no signature present
     * @throws std::runtime_error if file is not a valid Mach-O or the signature
     *         data lies outside the file
     */
    static std::optional<std::vector<std::uint8_t>> ExtractSignature(
        const std::string &file_path
    );

    /**
     * Extract the signature of every slice, in table order (one entry for a
     * thin file); a slice without a signature gives nullopt.
     * @throws std::runtime_error as ExtractSignature does
     */
    static std::vector<std::optional<std::vector<std::uint8_t>>> ExtractSignatures(
        const std::string &file_path
    );

    /**
     * Check if a Mach-O binary has an embedded code signature. For a universal
     * file this is true only when every slice is signed.
     * @throws std::runtime_error if file is not a valid Mach-O or the signature
     *         area is malformed
     */
    static bool HasEmbeddedSignature(const std::string &file_path);

    /**
     * Extract the CMS signature blob from a SuperBlob.
     * @returns DER-encoded CMS blob, or nullopt if the SuperBlob is well formed
     *          and has no CMS slot
     * @throws std::runtime_error if the SuperBlob data is malformed
     */
    static std::optional<std::vector<std::uint8_t>> ExtractCmsFromSuperBlob(
        const std::vector<std::uint8_t> &super_blob
    );

    /**
     * Extract the CodeDirectory blob from a SuperBlob.
     * @returns CodeDirectory bytes, or nullopt if the SuperBlob is well formed
     *          and has no CodeDirectory slot
     * @throws std::runtime_error if the SuperBlob data is malformed
     */
    static std::optional<std::vector<std::uint8_t>> ExtractCodeDirectoryFromSuperBlob(
        const std::vector<std::uint8_t> &super_blob
    );
};
