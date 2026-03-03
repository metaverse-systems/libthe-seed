#include <libthe-seed/PeSigner.hpp>

#include "PeParser.hpp"
#include "ByteSwap.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "../external/picosha2.h"

namespace {

std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path)
{
    std::ifstream input(file_path, std::ios::binary);
    if(!input.is_open())
    {
        throw std::runtime_error("Unable to open file: " + file_path);
    }
    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);
    if(size < 0)
    {
        throw std::runtime_error("Unable to read file size");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(size > 0)
    {
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        if(!input)
        {
            throw std::runtime_error("Unable to read file");
        }
    }
    return bytes;
}

void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes)
{
    // Atomic write: write to temp, then rename
    const auto temp_path = file_path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary);
        if(!output.is_open())
        {
            throw std::runtime_error("Unable to create temp file: " + temp_path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        if(!output)
        {
            throw std::runtime_error("Unable to write temp file");
        }
    }
    std::filesystem::rename(temp_path, file_path);
}

template <typename T>
T ReadLE(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset + sizeof(T) > bytes.size())
    {
        throw std::runtime_error("Truncated PE file: read beyond end");
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return ByteSwapIfNeeded(value, true);
}

template <typename T>
void WriteLE(std::vector<std::uint8_t> &bytes, std::size_t offset, T value)
{
    T le_value = ByteSwapIfNeeded(value, true);
    std::memcpy(bytes.data() + offset, &le_value, sizeof(T));
}

struct PeLayout
{
    std::size_t pe_header_offset;
    std::size_t optional_header_offset;
    std::size_t checksum_offset;
    std::size_t dd_security_offset;  // Data Directory entry 4 (Certificate Table)
    bool is_pe32_plus;
};

PeLayout ParsePeLayout(const std::vector<std::uint8_t> &bytes)
{
    if(bytes.size() < sizeof(IMAGE_DOS_HEADER))
    {
        throw std::runtime_error("File is too small to be a PE binary");
    }

    const auto dos_magic = ReadLE<std::uint16_t>(bytes, 0);
    if(dos_magic != IMAGE_DOS_SIGNATURE)
    {
        throw std::runtime_error("Invalid PE DOS signature");
    }

    PeLayout layout{};
    layout.pe_header_offset = static_cast<std::size_t>(ReadLE<std::int32_t>(bytes, 60)); // e_lfanew

    const auto signature = ReadLE<std::uint32_t>(bytes, layout.pe_header_offset);
    if(signature != IMAGE_NT_SIGNATURE)
    {
        throw std::runtime_error("Invalid PE signature");
    }

    // optional_header_offset = pe_header_offset + 4 (PE sig) + 20 (COFF header)
    layout.optional_header_offset = layout.pe_header_offset + 4 + 20;

    const auto optional_magic = ReadLE<std::uint16_t>(bytes, layout.optional_header_offset);
    layout.is_pe32_plus = (optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    if(optional_magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC && optional_magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        throw std::runtime_error("Unsupported PE optional header format");
    }

    // CheckSum is at offset 64 from start of optional header
    layout.checksum_offset = layout.optional_header_offset + 64;

    // Data Directory entry 4 (Certificate Table) offset:
    // PE32:  optional_header_offset + 128
    // PE32+: optional_header_offset + 144
    if(layout.is_pe32_plus)
    {
        layout.dd_security_offset = layout.optional_header_offset + 144;
    }
    else
    {
        layout.dd_security_offset = layout.optional_header_offset + 128;
    }

    // Verify we have enough data directory entries
    std::size_t num_rva_offset = layout.is_pe32_plus
        ? layout.optional_header_offset + 108
        : layout.optional_header_offset + 92;
    auto num_rva = ReadLE<std::uint32_t>(bytes, num_rva_offset);
    if(num_rva < 5)
    {
        throw std::runtime_error("PE optional header has fewer than 5 data directory entries");
    }

    return layout;
}

} // anonymous namespace

PeSigner::DigestResult PeSigner::ComputeAuthenticodeDigest(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    const auto layout = ParsePeLayout(bytes);

    // Read existing certificate table info
    const auto cert_table_va = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset);
    const auto cert_table_size = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset + 4);

    // Determine end of hashed content
    // If signed: hash up to cert_table_va (which is a raw file offset)
    // If unsigned: hash entire file
    const std::size_t hash_end = (cert_table_va != 0 && cert_table_size != 0)
        ? static_cast<std::size_t>(cert_table_va)
        : bytes.size();

    // Exclusion zones:
    // 1. CheckSum field: 4 bytes at layout.checksum_offset
    // 2. DD entry 4: 8 bytes at layout.dd_security_offset
    picosha2::hash256_one_by_one hasher;
    hasher.init();

    std::size_t pos = 0;

    // Helper to hash a range
    auto hash_range = [&](std::size_t start, std::size_t end) {
        if(start < end && end <= bytes.size())
        {
            hasher.process(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                           bytes.begin() + static_cast<std::ptrdiff_t>(end));
        }
    };

    // Sort exclusion zones
    struct Exclusion { std::size_t start; std::size_t end; };
    std::vector<Exclusion> exclusions = {
        { layout.checksum_offset, layout.checksum_offset + 4 },
        { layout.dd_security_offset, layout.dd_security_offset + 8 },
    };
    std::sort(exclusions.begin(), exclusions.end(),
              [](const Exclusion &a, const Exclusion &b) { return a.start < b.start; });

    pos = 0;
    for(const auto &excl : exclusions)
    {
        if(excl.start > pos && excl.start <= hash_end)
        {
            hash_range(pos, std::min(excl.start, hash_end));
        }
        pos = excl.end;
    }

    // Hash remaining data up to hash_end
    if(pos < hash_end)
    {
        hash_range(pos, hash_end);
    }

    hasher.finish();

    DigestResult result;
    result.digest.resize(picosha2::k_digest_size);
    hasher.get_hash_bytes(result.digest.begin(), result.digest.end());
    result.is_pe32_plus = layout.is_pe32_plus;

    return result;
}

void PeSigner::RecalcChecksum(std::vector<std::uint8_t> &bytes, std::size_t checksum_offset)
{
    // Zero the existing checksum field first
    WriteLE<std::uint32_t>(bytes, checksum_offset, 0);

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
        std::uint16_t word;
        std::memcpy(&word, bytes.data() + offset, sizeof(std::uint16_t));
        sum += word;
        // Fold carry
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Handle trailing byte if odd file size
    if(bytes.size() % 2 != 0)
    {
        sum += bytes.back();
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Final fold
    sum = (sum & 0xFFFF) + (sum >> 16);

    // Add file size
    const auto checksum = static_cast<std::uint32_t>(sum + bytes.size());
    WriteLE<std::uint32_t>(bytes, checksum_offset, checksum);
}

void PeSigner::EmbedSignature(
    const std::string &file_path,
    const std::vector<std::uint8_t> &pkcs7_der)
{
    auto bytes = ReadFileBytes(file_path);
    const auto layout = ParsePeLayout(bytes);

    // Strip any existing signature first
    const auto existing_va = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset);
    const auto existing_size = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset + 4);
    if(existing_va != 0 && existing_size != 0 && existing_va < bytes.size())
    {
        bytes.resize(static_cast<std::size_t>(existing_va));
    }

    // Build WIN_CERTIFICATE structure
    // dwLength (4 bytes) = 8 (header) + pkcs7_der.size()
    // wRevision (2 bytes) = 0x0200
    // wCertificateType (2 bytes) = 0x0002
    // bCertificate (variable) = pkcs7_der
    const std::uint32_t cert_length = static_cast<std::uint32_t>(8 + pkcs7_der.size());

    // Align certificate table to 8-byte boundary
    const std::size_t cert_table_offset = bytes.size();
    // Pad to 8-byte alignment if needed
    const std::size_t padding_before = (8 - (cert_table_offset % 8)) % 8;
    bytes.resize(bytes.size() + padding_before, 0);

    const std::size_t actual_cert_offset = bytes.size();

    // Append WIN_CERTIFICATE header
    const std::size_t win_cert_start = bytes.size();
    bytes.resize(bytes.size() + 8 + pkcs7_der.size());

    WriteLE<std::uint32_t>(bytes, win_cert_start, cert_length);
    WriteLE<std::uint16_t>(bytes, win_cert_start + 4, std::uint16_t{0x0200});
    WriteLE<std::uint16_t>(bytes, win_cert_start + 6, std::uint16_t{0x0002});
    std::memcpy(bytes.data() + win_cert_start + 8, pkcs7_der.data(), pkcs7_der.size());

    // Pad to 8-byte alignment after certificate
    const std::size_t total_cert_size = bytes.size() - actual_cert_offset;
    const std::size_t padding_after = (8 - (total_cert_size % 8)) % 8;
    bytes.resize(bytes.size() + padding_after, 0);

    // Update DD entry 4 (Certificate Table)
    const auto dd_va = static_cast<std::uint32_t>(actual_cert_offset);
    const auto dd_size = static_cast<std::uint32_t>(bytes.size() - actual_cert_offset);
    WriteLE<std::uint32_t>(bytes, layout.dd_security_offset, dd_va);
    WriteLE<std::uint32_t>(bytes, layout.dd_security_offset + 4, dd_size);

    // Recalculate PE checksum
    RecalcChecksum(bytes, layout.checksum_offset);

    // Atomic write
    WriteFileBytes(file_path, bytes);
}

std::optional<std::vector<std::uint8_t>> PeSigner::ExtractSignature(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    const auto layout = ParsePeLayout(bytes);

    const auto cert_va = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset);
    const auto cert_size = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset + 4);

    if(cert_va == 0 || cert_size == 0)
    {
        return std::nullopt;
    }

    const auto offset = static_cast<std::size_t>(cert_va);
    if(offset + cert_size > bytes.size())
    {
        throw std::runtime_error("Certificate table extends beyond file");
    }

    // Read WIN_CERTIFICATE header
    // dwLength at offset+0, wRevision at offset+4, wCertificateType at offset+6
    const auto dw_length = ReadLE<std::uint32_t>(bytes, offset);
    const auto w_revision = ReadLE<std::uint16_t>(bytes, offset + 4);
    const auto w_cert_type = ReadLE<std::uint16_t>(bytes, offset + 6);

    if(w_revision != 0x0200 || w_cert_type != 0x0002)
    {
        throw std::runtime_error("Unsupported WIN_CERTIFICATE revision/type");
    }

    // Extract PKCS#7 blob (after 8-byte header)
    const std::size_t blob_size = static_cast<std::size_t>(dw_length) - 8;
    const std::size_t blob_offset = offset + 8;
    if(blob_offset + blob_size > bytes.size())
    {
        throw std::runtime_error("PKCS#7 blob extends beyond file");
    }

    return std::vector<std::uint8_t>(
        bytes.begin() + static_cast<std::ptrdiff_t>(blob_offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(blob_offset + blob_size)
    );
}

bool PeSigner::HasEmbeddedSignature(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    const auto layout = ParsePeLayout(bytes);

    const auto cert_va = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset);
    const auto cert_size = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset + 4);

    return cert_va != 0 && cert_size != 0;
}

void PeSigner::StripSignature(const std::string &file_path)
{
    auto bytes = ReadFileBytes(file_path);
    const auto layout = ParsePeLayout(bytes);

    const auto cert_va = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset);
    const auto cert_size = ReadLE<std::uint32_t>(bytes, layout.dd_security_offset + 4);

    if(cert_va == 0 && cert_size == 0)
    {
        return; // No signature to strip
    }

    // Zero the DD entry 4
    WriteLE<std::uint32_t>(bytes, layout.dd_security_offset, std::uint32_t{0});
    WriteLE<std::uint32_t>(bytes, layout.dd_security_offset + 4, std::uint32_t{0});

    // Truncate the certificate data
    if(cert_va > 0 && cert_va < bytes.size())
    {
        bytes.resize(static_cast<std::size_t>(cert_va));
    }

    // Recalculate checksum
    RecalcChecksum(bytes, layout.checksum_offset);

    WriteFileBytes(file_path, bytes);
}
