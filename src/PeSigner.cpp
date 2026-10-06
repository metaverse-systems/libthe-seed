#include <libthe-seed/PeSigner.hpp>

#include "PeParser.hpp"
#include "ByteSwap.hpp"
#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"
#include "internal/PeCertificate.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../external/picosha2.h"

namespace {

using seed::internal::ByteOrder;
using seed::internal::ByteSpan;
using seed::internal::GuardEntryPoint;
using seed::internal::MutableByteSpan;
using seed::internal::ThrowMalformed;

constexpr const char *kFormat = "PE";
constexpr std::uint64_t kUint32Max = 0xFFFFFFFFull;

struct PeLayout
{
    std::uint64_t pe_header_offset;
    std::uint64_t optional_header_offset;
    std::uint64_t checksum_offset;
    std::uint64_t dd_security_offset;  // Data Directory entry 4 (Certificate Table)
    bool is_pe32_plus;
};

PeLayout ParsePeLayout(const ByteSpan &file)
{
    const ByteSpan dos = file.Sub(0, sizeof(IMAGE_DOS_HEADER), "DOS header");
    if(dos.Read<std::uint16_t>(0, ByteOrder::Little, "DOS header signature") != IMAGE_DOS_SIGNATURE)
    {
        ThrowMalformed(kFormat, "DOS header signature is not MZ");
    }

    PeLayout layout{};
    const auto e_lfanew = dos.Read<std::int32_t>(60, ByteOrder::Little, "e_lfanew");
    if(e_lfanew < 0)
    {
        ThrowMalformed(kFormat, "e_lfanew " + std::to_string(e_lfanew) + " is negative");
    }
    layout.pe_header_offset = static_cast<std::uint64_t>(e_lfanew);

    // Signature (4), file header (20) and the optional header's magic (2).
    const ByteSpan pe_header = file.Sub(layout.pe_header_offset, 4 + 20 + 2, "PE header");
    if(pe_header.Read<std::uint32_t>(0, ByteOrder::Little, "PE signature") != IMAGE_NT_SIGNATURE)
    {
        ThrowMalformed(kFormat, "PE header signature is invalid");
    }

    // optional_header_offset = pe_header_offset + 4 (PE sig) + 20 (COFF header)
    layout.optional_header_offset = layout.pe_header_offset + 4 + 20;

    const auto optional_magic = pe_header.Read<std::uint16_t>(24, ByteOrder::Little, "optional header magic");
    if(optional_magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC && optional_magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        ThrowMalformed(kFormat, "PE header: optional header format " + std::to_string(optional_magic) +
                                    " is not supported");
    }
    layout.is_pe32_plus = (optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    // CheckSum is at offset 64 from start of optional header
    layout.checksum_offset = layout.optional_header_offset + 64;

    // Data Directory entry 4 (Certificate Table) offset:
    // PE32:  optional_header_offset + 128
    // PE32+: optional_header_offset + 144
    layout.dd_security_offset = layout.optional_header_offset + (layout.is_pe32_plus ? 144 : 128);

    // Verify we have enough data directory entries
    const std::uint64_t num_rva_offset =
        layout.optional_header_offset + (layout.is_pe32_plus ? 108 : 92);
    const auto num_rva = file.Read<std::uint32_t>(num_rva_offset, ByteOrder::Little,
                                                  "PE header NumberOfRvaAndSizes");
    if(num_rva < 5)
    {
        ThrowMalformed(kFormat, "PE header has " + std::to_string(num_rva) +
                                    " data directory entries, fewer than 5");
    }

    // The checksum field and the certificate directory entry lie in the file.
    file.Sub(layout.checksum_offset, 4, "PE header CheckSum field");
    file.Sub(layout.dd_security_offset, 8, "PE header certificate directory entry");

    return layout;
}

// The certificate table named by the certificate directory entry.
struct CertificateTable
{
    bool present = false;
    std::uint64_t address = 0;  // file offset
    std::uint64_t size = 0;
    std::uint64_t length = 0;   // dwLength of the first WIN_CERTIFICATE
};

// Checks the certificate table before anything relies on it. Every operation
// that reads or replaces the table calls this one function.
CertificateTable ValidateCertificateTable(const ByteSpan &file, const PeLayout &layout)
{
    CertificateTable table;
    table.address = file.Read<std::uint32_t>(layout.dd_security_offset, ByteOrder::Little,
                                             "certificate directory address");
    table.size = file.Read<std::uint32_t>(layout.dd_security_offset + 4, ByteOrder::Little,
                                          "certificate directory size");

    // C1: either field 0 means unsigned.
    if(table.address == 0 || table.size == 0)
    {
        table.present = false;
        return table;
    }
    table.present = true;

    // C2: the table lies in the file.
    const ByteSpan span = file.Sub(table.address, table.size, "certificate table");

    // C3: the table does not overlap the headers up to the end of the directory entry.
    const std::uint64_t headers_end = layout.dd_security_offset + 8;
    if(table.address < headers_end)
    {
        ThrowMalformed(kFormat, "certificate table (offset " + std::to_string(table.address) +
                                    ", size " + std::to_string(table.size) +
                                    ") overlaps the PE headers (" + std::to_string(headers_end) +
                                    " bytes)");
    }

    // C4: the first WIN_CERTIFICATE.
    if(table.size < 8)
    {
        ThrowMalformed(kFormat, "certificate table size " + std::to_string(table.size) +
                                    " is smaller than a WIN_CERTIFICATE header (8 bytes)");
    }
    table.length = span.Read<std::uint32_t>(0, ByteOrder::Little, "WIN_CERTIFICATE length");
    if(table.length < 8)
    {
        ThrowMalformed(kFormat, "WIN_CERTIFICATE length " + std::to_string(table.length) +
                                    " is smaller than its 8-byte header");
    }
    if(table.length > table.size)
    {
        ThrowMalformed(kFormat, "WIN_CERTIFICATE length " + std::to_string(table.length) +
                                    " is larger than the certificate table (" +
                                    std::to_string(table.size) + " bytes)");
    }
    const auto revision = span.Read<std::uint16_t>(4, ByteOrder::Little, "WIN_CERTIFICATE revision");
    const auto type = span.Read<std::uint16_t>(6, ByteOrder::Little, "WIN_CERTIFICATE type");
    if(revision != 0x0200 || type != 0x0002)
    {
        ThrowMalformed(kFormat, "WIN_CERTIFICATE revision " + std::to_string(revision) + " or type " +
                                    std::to_string(type) + " is not supported");
    }
    return table;
}

} // anonymous namespace

PeSigner::DigestResult PeSigner::ComputeAuthenticodeDigest(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
        const auto bytes = ReadFileBytes(file_path);
        const ByteSpan file(bytes, kFormat);
        const auto layout = ParsePeLayout(file);
        const auto table = ValidateCertificateTable(file, layout);

        // If signed: hash up to the certificate table (a raw file offset).
        // If unsigned: hash the entire file.
        const std::uint64_t hash_end = table.present ? table.address : file.Size();

        picosha2::hash256_one_by_one hasher;
        hasher.init();

        // Every hashed range lies in the file; a range that does not is rejected.
        auto hash_range = [&](std::uint64_t start, std::uint64_t end) {
            if(start < end)
            {
                const ByteSpan range = file.Sub(start, end - start, "hashed range");
                hasher.process(range.Data(), range.Data() + range.Size());
            }
        };

        // Exclusion zones, in file order:
        // 1. CheckSum field: 4 bytes at layout.checksum_offset
        // 2. DD entry 4: 8 bytes at layout.dd_security_offset
        struct Exclusion { std::uint64_t start; std::uint64_t end; };
        std::vector<Exclusion> exclusions = {
            { layout.checksum_offset, layout.checksum_offset + 4 },
            { layout.dd_security_offset, layout.dd_security_offset + 8 },
        };
        std::sort(exclusions.begin(), exclusions.end(),
                  [](const Exclusion &a, const Exclusion &b) { return a.start < b.start; });

        std::uint64_t pos = 0;
        for(const auto &excl : exclusions)
        {
            if(excl.start > pos)
            {
                hash_range(pos, excl.start);
            }
            pos = excl.end;
        }

        // Hash remaining data up to hash_end
        hash_range(pos, hash_end);

        hasher.finish();

        DigestResult result;
        result.digest.resize(picosha2::k_digest_size);
        hasher.get_hash_bytes(result.digest.begin(), result.digest.end());
        result.is_pe32_plus = layout.is_pe32_plus;

        return result;
    });
}

void PeSigner::RecalcChecksum(std::vector<std::uint8_t> &bytes, std::size_t checksum_offset)
{
    GuardEntryPoint(kFormat, [&] {
        const MutableByteSpan out(bytes, kFormat);

        // Zero the existing checksum field first
        out.Write<std::uint32_t>(checksum_offset, 0, ByteOrder::Little, "CheckSum field");

        const ByteSpan file(bytes, kFormat);

        // Carry-folding 16-bit checksum (TCP/IP style)
        std::uint32_t sum = 0;
        const std::size_t word_count = bytes.size() / 2;

        for(std::size_t i = 0; i < word_count; ++i)
        {
            const std::size_t offset = i * 2;
            // Skip the two words that make up the checksum field
            if(offset == checksum_offset || offset == checksum_offset + 2)
            {
                continue;
            }
            sum += file.Read<std::uint16_t>(offset, ByteOrder::Little, "checksum word");
            // Fold carry
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        // Handle trailing byte if odd file size
        if(bytes.size() % 2 != 0)
        {
            sum += file.Read<std::uint8_t>(bytes.size() - 1, ByteOrder::Little, "checksum byte");
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        // Final fold
        sum = (sum & 0xFFFF) + (sum >> 16);

        // Add file size
        const auto checksum = static_cast<std::uint32_t>(sum + bytes.size());
        out.Write<std::uint32_t>(checksum_offset, checksum, ByteOrder::Little, "CheckSum field");
    });
}

void PeSigner::EmbedSignature(
    const std::string &file_path,
    const std::vector<std::uint8_t> &pkcs7_der)
{
    GuardEntryPoint(kFormat, [&] {
        auto bytes = ReadFileBytes(file_path);

        // Everything is checked before the buffer changes.
        PeLayout layout;
        CertificateTable existing;
        {
            const ByteSpan file(bytes, kFormat);
            layout = ParsePeLayout(file);
            existing = ValidateCertificateTable(file, layout);
        }
        seed::internal::CheckPeSignatureSize(pkcs7_der.size());

        // Strip any existing signature first
        const std::uint64_t kept_size = existing.present ? existing.address : bytes.size();

        // Align certificate table to 8-byte boundary
        const std::uint64_t padding_before = (8 - (kept_size % 8)) % 8;
        const std::uint64_t cert_offset = kept_size + padding_before;

        // Build WIN_CERTIFICATE structure
        // dwLength (4 bytes) = 8 (header) + pkcs7_der.size()
        // wRevision (2 bytes) = 0x0200
        // wCertificateType (2 bytes) = 0x0002
        // bCertificate (variable) = pkcs7_der
        const std::uint64_t cert_length = 8 + static_cast<std::uint64_t>(pkcs7_der.size());
        const std::uint64_t padding_after = (8 - (cert_length % 8)) % 8;
        const std::uint64_t table_size = cert_length + padding_after;

        if(cert_offset > kUint32Max)
        {
            ThrowMalformed(kFormat, "certificate table offset " + std::to_string(cert_offset) +
                                        " does not fit in 32 bits");
        }
        if(table_size > kUint32Max)
        {
            ThrowMalformed(kFormat, "certificate table size " + std::to_string(table_size) +
                                        " does not fit in 32 bits");
        }

        bytes.resize(static_cast<std::size_t>(kept_size));
        bytes.resize(static_cast<std::size_t>(cert_offset + table_size), 0);

        const MutableByteSpan out(bytes, kFormat);
        out.Write<std::uint32_t>(cert_offset, static_cast<std::uint32_t>(cert_length), ByteOrder::Little,
                                 "WIN_CERTIFICATE length");
        out.Write<std::uint16_t>(cert_offset + 4, std::uint16_t{0x0200}, ByteOrder::Little,
                                 "WIN_CERTIFICATE revision");
        out.Write<std::uint16_t>(cert_offset + 6, std::uint16_t{0x0002}, ByteOrder::Little,
                                 "WIN_CERTIFICATE type");
        out.Copy(cert_offset + 8, pkcs7_der, "WIN_CERTIFICATE data");

        // Update DD entry 4 (Certificate Table)
        out.Write<std::uint32_t>(layout.dd_security_offset, static_cast<std::uint32_t>(cert_offset),
                                 ByteOrder::Little, "certificate directory address");
        out.Write<std::uint32_t>(layout.dd_security_offset + 4, static_cast<std::uint32_t>(table_size),
                                 ByteOrder::Little, "certificate directory size");

        // Recalculate PE checksum
        RecalcChecksum(bytes, static_cast<std::size_t>(layout.checksum_offset));

        // Atomic write
        WriteFileBytes(file_path, bytes);
    });
}

std::optional<std::vector<std::uint8_t>> PeSigner::ExtractSignature(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&]() -> std::optional<std::vector<std::uint8_t>> {
        const auto bytes = ReadFileBytes(file_path);
        const ByteSpan file(bytes, kFormat);
        const auto layout = ParsePeLayout(file);
        const auto table = ValidateCertificateTable(file, layout);

        if(!table.present)
        {
            return std::nullopt;
        }

        // Extract PKCS#7 blob (after the 8-byte header)
        const ByteSpan certificate = file.Sub(table.address, table.length, "WIN_CERTIFICATE");
        const ByteSpan blob = certificate.Sub(8, table.length - 8, "PKCS#7 blob");
        return std::vector<std::uint8_t>(blob.Data(), blob.Data() + blob.Size());
    });
}

bool PeSigner::HasEmbeddedSignature(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
        const auto bytes = ReadFileBytes(file_path);
        const ByteSpan file(bytes, kFormat);
        const auto layout = ParsePeLayout(file);
        return ValidateCertificateTable(file, layout).present;
    });
}

void PeSigner::StripSignature(const std::string &file_path)
{
    GuardEntryPoint(kFormat, [&] {
        auto bytes = ReadFileBytes(file_path);

        PeLayout layout;
        CertificateTable table;
        {
            const ByteSpan file(bytes, kFormat);
            layout = ParsePeLayout(file);
            table = ValidateCertificateTable(file, layout);
        }

        if(!table.present)
        {
            return; // No signature to strip
        }

        // Truncate the certificate data
        bytes.resize(static_cast<std::size_t>(table.address));

        // Zero the DD entry 4
        const MutableByteSpan out(bytes, kFormat);
        out.Write<std::uint32_t>(layout.dd_security_offset, std::uint32_t{0}, ByteOrder::Little,
                                 "certificate directory address");
        out.Write<std::uint32_t>(layout.dd_security_offset + 4, std::uint32_t{0}, ByteOrder::Little,
                                 "certificate directory size");

        // Recalculate checksum
        RecalcChecksum(bytes, static_cast<std::size_t>(layout.checksum_offset));

        WriteFileBytes(file_path, bytes);
    });
}
