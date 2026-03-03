#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MachOParser.hpp>

#include "ByteSwap.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../external/picosha2.h"

namespace {

// Mach-O magic constants
constexpr std::uint32_t MH_MAGIC_64 = 0xFEEDFACF;
constexpr std::uint32_t MH_CIGAM_64 = 0xCFFAEDFE;
constexpr std::uint32_t MH_MAGIC    = 0xFEEDFACE;
constexpr std::uint32_t MH_CIGAM    = 0xCEFAEDFE;

// Code Signing constants (big-endian)
constexpr std::uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0; // SuperBlob
constexpr std::uint32_t CSMAGIC_CODEDIRECTORY      = 0xFADE0C02;
constexpr std::uint32_t CSMAGIC_REQUIREMENTS       = 0xFADE0C01;
constexpr std::uint32_t CSMAGIC_BLOBWRAPPER        = 0xFADE0B01;

constexpr std::uint32_t CSSLOT_CODEDIRECTORY = 0;
constexpr std::uint32_t CSSLOT_REQUIREMENTS  = 2;
constexpr std::uint32_t CSSLOT_CMS_SIGNATURE = 0x10000;

constexpr std::uint32_t CS_HASHTYPE_SHA256   = 2;
constexpr std::uint32_t CS_HASH_SIZE_SHA256  = 32;
constexpr std::uint32_t CS_PAGE_SIZE_LOG2    = 12; // 4096 bytes
constexpr std::uint32_t CS_PAGE_SIZE         = 4096;

constexpr std::uint32_t LC_CODE_SIGNATURE    = 0x1D;
constexpr std::uint32_t LC_SEGMENT_64        = 0x19;

// CodeDirectory version supporting execSeg (for arm64 compatibility)
constexpr std::uint32_t CS_SUPPORTSEXECSEG   = 0x20400;

#pragma pack(push, 1)
struct MachHeader64
{
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
    std::uint32_t reserved;
};

struct LoadCommand
{
    std::uint32_t cmd;
    std::uint32_t cmdsize;
};

struct LinkeditDataCommand
{
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t dataoff;
    std::uint32_t datasize;
};

struct SegmentCommand64
{
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    char segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::uint32_t maxprot;
    std::uint32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct CodeDirectory
{
    std::uint32_t magic;       // CSMAGIC_CODEDIRECTORY
    std::uint32_t length;
    std::uint32_t version;
    std::uint32_t flags;
    std::uint32_t hashOffset;
    std::uint32_t identOffset;
    std::uint32_t nSpecialSlots;
    std::uint32_t nCodeSlots;
    std::uint32_t codeLimit;
    std::uint8_t  hashSize;
    std::uint8_t  hashType;
    std::uint8_t  platform;
    std::uint8_t  pageSize;     // log2
    std::uint32_t spare2;
    // Version 0x20100+
    std::uint32_t scatterOffset;
    // Version 0x20200+
    std::uint32_t teamOffset;
    // Version 0x20300+
    std::uint32_t spare3;
    std::uint64_t codeLimit64;
    // Version 0x20400+
    std::uint64_t execSegBase;
    std::uint64_t execSegLimit;
    std::uint64_t execSegFlags;
};

struct SuperBlobHeader
{
    std::uint32_t magic;
    std::uint32_t length;
    std::uint32_t count;
};

struct BlobIndex
{
    std::uint32_t type;
    std::uint32_t offset;
};
#pragma pack(pop)

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
    }
    return bytes;
}

void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes)
{
    const auto temp_path = file_path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary);
        if(!output.is_open())
        {
            throw std::runtime_error("Unable to create temp file: " + temp_path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
    }
    std::filesystem::rename(temp_path, file_path);
}

// Write a big-endian uint32
void WriteBE32(std::vector<std::uint8_t> &buf, std::size_t offset, std::uint32_t value)
{
    buf[offset]     = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    buf[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    buf[offset + 2] = static_cast<std::uint8_t>((value >> 8)  & 0xFF);
    buf[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

std::uint32_t ReadBE32(const std::vector<std::uint8_t> &buf, std::size_t offset)
{
    return (static_cast<std::uint32_t>(buf[offset]) << 24) |
           (static_cast<std::uint32_t>(buf[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buf[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buf[offset + 3]);
}

void WriteBE64(std::vector<std::uint8_t> &buf, std::size_t offset, std::uint64_t value)
{
    for(int i = 7; i >= 0; --i)
    {
        buf[offset + static_cast<std::size_t>(7 - i)] =
            static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

std::vector<std::uint8_t> HashSHA256(const std::uint8_t *data, std::size_t size)
{
    std::vector<std::uint8_t> hash(picosha2::k_digest_size);
    picosha2::hash256(data, data + size, hash.begin(), hash.end());
    return hash;
}

bool IsBigEndianMachO(std::uint32_t magic)
{
    return magic == MH_MAGIC || magic == MH_MAGIC_64;
}

template <typename T>
T ReadField(const std::vector<std::uint8_t> &bytes, std::size_t offset, bool big_endian)
{
    if(offset + sizeof(T) > bytes.size())
    {
        throw std::runtime_error("Truncated Mach-O file");
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return ByteSwapIfNeeded(value, !big_endian);
}

template <typename T>
void WriteField(std::vector<std::uint8_t> &bytes, std::size_t offset, T value, bool big_endian)
{
    T stored = ByteSwapIfNeeded(value, !big_endian);
    std::memcpy(bytes.data() + offset, &stored, sizeof(T));
}

// Build the empty requirements blob: FADE0C01 0000000C 00000000
std::vector<std::uint8_t> BuildEmptyRequirements()
{
    std::vector<std::uint8_t> req(12, 0);
    WriteBE32(req, 0, CSMAGIC_REQUIREMENTS);
    WriteBE32(req, 4, 12);
    WriteBE32(req, 8, 0);
    return req;
}

struct MachOLayout
{
    std::uint32_t magic;
    bool big_endian;
    bool is_64bit;
    std::size_t header_size;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::size_t linkedit_cmd_offset;  // offset of __LINKEDIT segment command (0 if not found)
    std::size_t codesig_cmd_offset;   // offset of LC_CODE_SIGNATURE (0 if not found)
    std::uint64_t linkedit_fileoff;
    std::uint64_t linkedit_filesize;
    std::uint64_t linkedit_vmaddr;
    std::uint64_t linkedit_vmsize;
    std::uint64_t text_fileoff;
    std::uint64_t text_filesize;
    std::size_t code_limit;           // end of code pages (= file size excluding sig)
};

MachOLayout ParseMachOLayout(const std::vector<std::uint8_t> &bytes)
{
    if(bytes.size() < 4)
    {
        throw std::runtime_error("File too small to be a Mach-O binary");
    }

    MachOLayout layout{};
    std::memcpy(&layout.magic, bytes.data(), 4);
    layout.big_endian = IsBigEndianMachO(layout.magic);
    layout.is_64bit = (layout.magic == MH_MAGIC_64 || layout.magic == MH_CIGAM_64);

    if(layout.magic != MH_MAGIC && layout.magic != MH_CIGAM &&
       layout.magic != MH_MAGIC_64 && layout.magic != MH_CIGAM_64)
    {
        throw std::runtime_error("Not a single-arch Mach-O binary");
    }

    layout.header_size = layout.is_64bit ? sizeof(MachHeader64) : 28; // 32-bit header is 28 bytes
    layout.ncmds = ReadField<std::uint32_t>(bytes, 16, layout.big_endian);
    layout.sizeofcmds = ReadField<std::uint32_t>(bytes, 20, layout.big_endian);

    // Scan load commands
    std::size_t cmd_offset = layout.header_size;
    for(std::uint32_t i = 0; i < layout.ncmds; ++i)
    {
        if(cmd_offset + sizeof(LoadCommand) > bytes.size())
        {
            break;
        }

        const auto cmd = ReadField<std::uint32_t>(bytes, cmd_offset, layout.big_endian);
        const auto cmdsize = ReadField<std::uint32_t>(bytes, cmd_offset + 4, layout.big_endian);

        if(cmd == LC_SEGMENT_64)
        {
            char segname[17] = {};
            std::memcpy(segname, bytes.data() + cmd_offset + 8, 16);

            if(std::string(segname) == "__LINKEDIT")
            {
                layout.linkedit_cmd_offset = cmd_offset;
                layout.linkedit_vmaddr = ReadField<std::uint64_t>(bytes, cmd_offset + 24, layout.big_endian);
                layout.linkedit_vmsize = ReadField<std::uint64_t>(bytes, cmd_offset + 32, layout.big_endian);
                layout.linkedit_fileoff = ReadField<std::uint64_t>(bytes, cmd_offset + 40, layout.big_endian);
                layout.linkedit_filesize = ReadField<std::uint64_t>(bytes, cmd_offset + 48, layout.big_endian);
            }
            else if(std::string(segname) == "__TEXT")
            {
                layout.text_fileoff = ReadField<std::uint64_t>(bytes, cmd_offset + 40, layout.big_endian);
                layout.text_filesize = ReadField<std::uint64_t>(bytes, cmd_offset + 48, layout.big_endian);
            }
        }
        else if(cmd == LC_CODE_SIGNATURE)
        {
            layout.codesig_cmd_offset = cmd_offset;
        }

        cmd_offset += cmdsize;
    }

    // Determine code limit
    if(layout.codesig_cmd_offset != 0)
    {
        // code limit = offset of signature data
        layout.code_limit = ReadField<std::uint32_t>(bytes, layout.codesig_cmd_offset + 8, layout.big_endian);
    }
    else
    {
        layout.code_limit = bytes.size();
    }

    return layout;
}

} // anonymous namespace

MachOSigner::CodeDirectoryResult MachOSigner::ComputeCodeDirectory(
    const std::string &file_path,
    const std::string &identity)
{
    const auto bytes = ReadFileBytes(file_path);
    const auto layout = ParseMachOLayout(bytes);

    const std::size_t code_limit = layout.code_limit;

    // Compute page hashes
    const std::size_t n_code_slots = (code_limit + CS_PAGE_SIZE - 1) / CS_PAGE_SIZE;
    const std::uint32_t n_special_slots = 2; // -1 = info.plist (zeroed), -2 = requirements

    // Compute requirements hash
    const auto empty_req = BuildEmptyRequirements();
    const auto req_hash = HashSHA256(empty_req.data(), empty_req.size());

    // CodeDirectory layout:
    // Fixed header through execSegFlags = 88 bytes for version 0x20400
    const std::size_t cd_fixed_size = 88;
    const std::size_t ident_offset = cd_fixed_size;
    const std::size_t ident_size = identity.size() + 1; // null-terminated
    const std::size_t hash_offset = ident_offset + ident_size;
    // Align hash offset for cleanliness (not strictly required)
    const std::size_t special_hashes_start = hash_offset; // special slots stored before code slots
    // Special slots are stored at negative indices: -2, -1 (in order)
    // Total hash slots = nSpecialSlots + nCodeSlots
    const std::size_t total_hashes = n_special_slots + n_code_slots;
    const std::size_t cd_total_size = hash_offset + total_hashes * CS_HASH_SIZE_SHA256;

    std::vector<std::uint8_t> cd(cd_total_size, 0);

    // Write CodeDirectory header (big-endian)
    WriteBE32(cd, 0, CSMAGIC_CODEDIRECTORY);
    WriteBE32(cd, 4, static_cast<std::uint32_t>(cd_total_size));
    WriteBE32(cd, 8, CS_SUPPORTSEXECSEG); // version
    WriteBE32(cd, 12, 0); // flags
    WriteBE32(cd, 16, static_cast<std::uint32_t>(hash_offset + n_special_slots * CS_HASH_SIZE_SHA256)); // hashOffset (points to code slot 0)
    WriteBE32(cd, 20, static_cast<std::uint32_t>(ident_offset)); // identOffset
    WriteBE32(cd, 24, n_special_slots);
    WriteBE32(cd, 28, static_cast<std::uint32_t>(n_code_slots));
    WriteBE32(cd, 32, static_cast<std::uint32_t>(code_limit)); // codeLimit
    cd[36] = CS_HASH_SIZE_SHA256; // hashSize
    cd[37] = CS_HASHTYPE_SHA256;  // hashType
    cd[38] = 0;                   // platform
    cd[39] = CS_PAGE_SIZE_LOG2;   // pageSize
    WriteBE32(cd, 40, 0); // spare2
    WriteBE32(cd, 44, 0); // scatterOffset
    WriteBE32(cd, 48, 0); // teamOffset
    WriteBE32(cd, 52, 0); // spare3
    WriteBE64(cd, 56, 0); // codeLimit64
    WriteBE64(cd, 64, layout.text_fileoff);  // execSegBase
    WriteBE64(cd, 72, layout.text_filesize); // execSegLimit
    WriteBE64(cd, 80, 0); // execSegFlags

    // Write identity string
    std::memcpy(cd.data() + ident_offset, identity.c_str(), identity.size() + 1);

    // Write special slot hashes (stored before code slots, at negative indices)
    // Slot -2 = requirements hash (index 0 in special area)
    // Slot -1 = info.plist hash (index 1, zeroed = no info.plist)
    std::memcpy(cd.data() + special_hashes_start, req_hash.data(), CS_HASH_SIZE_SHA256);
    // Slot -1 stays zero (no Info.plist)

    // Write code slot hashes
    const std::size_t code_hashes_start = hash_offset + n_special_slots * CS_HASH_SIZE_SHA256;
    for(std::size_t i = 0; i < n_code_slots; ++i)
    {
        const std::size_t page_start = i * CS_PAGE_SIZE;
        const std::size_t page_end = std::min(page_start + CS_PAGE_SIZE, code_limit);
        const auto hash = HashSHA256(bytes.data() + page_start, page_end - page_start);
        std::memcpy(cd.data() + code_hashes_start + i * CS_HASH_SIZE_SHA256,
                     hash.data(), CS_HASH_SIZE_SHA256);
    }

    // Compute cdHash (SHA-256 of entire CodeDirectory)
    CodeDirectoryResult result;
    result.code_directory = cd;
    result.cd_hash = HashSHA256(cd.data(), cd.size());

    return result;
}

std::vector<std::uint8_t> MachOSigner::BuildSuperBlob(
    const std::vector<std::uint8_t> &code_directory,
    const std::vector<std::uint8_t> &cms_signature)
{
    const auto requirements = BuildEmptyRequirements();

    // Build CMS BlobWrapper
    std::vector<std::uint8_t> cms_blob(8 + cms_signature.size());
    WriteBE32(cms_blob, 0, CSMAGIC_BLOBWRAPPER);
    WriteBE32(cms_blob, 4, static_cast<std::uint32_t>(cms_blob.size()));
    std::memcpy(cms_blob.data() + 8, cms_signature.data(), cms_signature.size());

    // SuperBlob: header (12 bytes) + 3 blob index entries (8 bytes each) + blobs
    const std::uint32_t n_blobs = 3;
    const std::size_t index_size = n_blobs * sizeof(BlobIndex);
    const std::size_t header_size = 12 + index_size;

    const std::size_t cd_offset = header_size;
    const std::size_t req_offset = cd_offset + code_directory.size();
    const std::size_t cms_offset = req_offset + requirements.size();
    const std::size_t total_size = cms_offset + cms_blob.size();

    std::vector<std::uint8_t> blob(total_size, 0);

    // SuperBlob header
    WriteBE32(blob, 0, CSMAGIC_EMBEDDED_SIGNATURE);
    WriteBE32(blob, 4, static_cast<std::uint32_t>(total_size));
    WriteBE32(blob, 8, n_blobs);

    // Blob index entries
    // Entry 0: CodeDirectory
    WriteBE32(blob, 12, CSSLOT_CODEDIRECTORY);
    WriteBE32(blob, 16, static_cast<std::uint32_t>(cd_offset));
    // Entry 1: Requirements
    WriteBE32(blob, 20, CSSLOT_REQUIREMENTS);
    WriteBE32(blob, 24, static_cast<std::uint32_t>(req_offset));
    // Entry 2: CMS Signature
    WriteBE32(blob, 28, CSSLOT_CMS_SIGNATURE);
    WriteBE32(blob, 32, static_cast<std::uint32_t>(cms_offset));

    // Copy blobs
    std::memcpy(blob.data() + cd_offset, code_directory.data(), code_directory.size());
    std::memcpy(blob.data() + req_offset, requirements.data(), requirements.size());
    std::memcpy(blob.data() + cms_offset, cms_blob.data(), cms_blob.size());

    return blob;
}

void MachOSigner::EmbedSignature(
    const std::string &file_path,
    const std::vector<std::uint8_t> &super_blob)
{
    auto bytes = ReadFileBytes(file_path);
    auto layout = ParseMachOLayout(bytes);

    if(!layout.is_64bit)
    {
        throw std::runtime_error("Only 64-bit Mach-O binaries are currently supported for signing");
    }

    // If existing signature, strip it
    if(layout.codesig_cmd_offset != 0)
    {
        // Truncate to code limit (remove old signature data)
        const auto old_dataoff = ReadField<std::uint32_t>(bytes, layout.codesig_cmd_offset + 8, layout.big_endian);
        bytes.resize(old_dataoff);

        // Zero out the old LC_CODE_SIGNATURE load command
        std::memset(bytes.data() + layout.codesig_cmd_offset, 0,
                     sizeof(LinkeditDataCommand));

        // Re-parse layout
        layout = ParseMachOLayout(bytes);
    }

    // Pad file to 16-byte alignment
    const std::size_t pad_to_16 = (16 - (bytes.size() % 16)) % 16;
    bytes.resize(bytes.size() + pad_to_16, 0);

    const std::size_t sig_offset = bytes.size();
    const std::size_t sig_size = super_blob.size();

    // Append SuperBlob
    bytes.insert(bytes.end(), super_blob.begin(), super_blob.end());

    // Now we need to either add or update LC_CODE_SIGNATURE
    if(layout.codesig_cmd_offset != 0)
    {
        // Update existing LC_CODE_SIGNATURE
        WriteField<std::uint32_t>(bytes, layout.codesig_cmd_offset + 8,
                                   static_cast<std::uint32_t>(sig_offset), layout.big_endian);
        WriteField<std::uint32_t>(bytes, layout.codesig_cmd_offset + 12,
                                   static_cast<std::uint32_t>(sig_size), layout.big_endian);
    }
    else
    {
        // Add new LC_CODE_SIGNATURE load command
        // Insert at end of existing load commands
        const std::size_t new_cmd_offset = layout.header_size + layout.sizeofcmds;

        // Verify we have space (there should be padding between load commands and first section)
        if(new_cmd_offset + sizeof(LinkeditDataCommand) > bytes.size())
        {
            throw std::runtime_error("No space for new load command");
        }

        // Write LC_CODE_SIGNATURE
        WriteField<std::uint32_t>(bytes, new_cmd_offset, LC_CODE_SIGNATURE, layout.big_endian);
        WriteField<std::uint32_t>(bytes, new_cmd_offset + 4, std::uint32_t{16}, layout.big_endian);
        WriteField<std::uint32_t>(bytes, new_cmd_offset + 8,
                                   static_cast<std::uint32_t>(sig_offset), layout.big_endian);
        WriteField<std::uint32_t>(bytes, new_cmd_offset + 12,
                                   static_cast<std::uint32_t>(sig_size), layout.big_endian);

        // Update header: ncmds += 1, sizeofcmds += 16
        WriteField<std::uint32_t>(bytes, 16, layout.ncmds + 1, layout.big_endian);
        WriteField<std::uint32_t>(bytes, 20, layout.sizeofcmds + 16, layout.big_endian);
    }

    // Update __LINKEDIT segment to cover the signature data
    if(layout.linkedit_cmd_offset != 0)
    {
        const std::uint64_t new_filesize = bytes.size() - layout.linkedit_fileoff;
        WriteField<std::uint64_t>(bytes, layout.linkedit_cmd_offset + 48,
                                   new_filesize, layout.big_endian);

        // Update vmsize (page-aligned)
        const std::uint64_t page_size = 16384; // arm64 page size
        const std::uint64_t new_vmsize = ((new_filesize + page_size - 1) / page_size) * page_size;
        WriteField<std::uint64_t>(bytes, layout.linkedit_cmd_offset + 32,
                                   new_vmsize, layout.big_endian);
    }

    WriteFileBytes(file_path, bytes);
}

std::optional<std::vector<std::uint8_t>> MachOSigner::ExtractSignature(
    const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    const auto layout = ParseMachOLayout(bytes);

    if(layout.codesig_cmd_offset == 0)
    {
        return std::nullopt;
    }

    const auto dataoff = ReadField<std::uint32_t>(bytes, layout.codesig_cmd_offset + 8, layout.big_endian);
    const auto datasize = ReadField<std::uint32_t>(bytes, layout.codesig_cmd_offset + 12, layout.big_endian);

    if(dataoff == 0 || datasize == 0)
    {
        return std::nullopt;
    }

    if(static_cast<std::size_t>(dataoff) + datasize > bytes.size())
    {
        throw std::runtime_error("Code signature extends beyond file");
    }

    return std::vector<std::uint8_t>(
        bytes.begin() + dataoff,
        bytes.begin() + dataoff + datasize
    );
}

bool MachOSigner::HasEmbeddedSignature(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    const auto layout = ParseMachOLayout(bytes);
    return layout.codesig_cmd_offset != 0;
}

std::optional<std::vector<std::uint8_t>> MachOSigner::ExtractCmsFromSuperBlob(
    const std::vector<std::uint8_t> &super_blob)
{
    if(super_blob.size() < 12)
    {
        return std::nullopt;
    }

    const auto magic = ReadBE32(super_blob, 0);
    if(magic != CSMAGIC_EMBEDDED_SIGNATURE)
    {
        return std::nullopt;
    }

    const auto count = ReadBE32(super_blob, 8);

    for(std::uint32_t i = 0; i < count; ++i)
    {
        const std::size_t idx_offset = 12 + static_cast<std::size_t>(i) * 8;
        if(idx_offset + 8 > super_blob.size())
        {
            break;
        }

        const auto slot_type = ReadBE32(super_blob, idx_offset);
        const auto blob_offset = ReadBE32(super_blob, idx_offset + 4);

        if(slot_type == CSSLOT_CMS_SIGNATURE)
        {
            if(blob_offset + 8 > super_blob.size())
            {
                return std::nullopt;
            }

            const auto blob_magic = ReadBE32(super_blob, blob_offset);
            const auto blob_length = ReadBE32(super_blob, blob_offset + 4);

            if(blob_magic != CSMAGIC_BLOBWRAPPER)
            {
                return std::nullopt;
            }

            const std::size_t cms_start = blob_offset + 8;
            const std::size_t cms_size = blob_length - 8;

            if(cms_start + cms_size > super_blob.size())
            {
                return std::nullopt;
            }

            return std::vector<std::uint8_t>(
                super_blob.begin() + static_cast<std::ptrdiff_t>(cms_start),
                super_blob.begin() + static_cast<std::ptrdiff_t>(cms_start + cms_size)
            );
        }
    }

    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> MachOSigner::ExtractCodeDirectoryFromSuperBlob(
    const std::vector<std::uint8_t> &super_blob)
{
    if(super_blob.size() < 12)
    {
        return std::nullopt;
    }

    const auto magic = ReadBE32(super_blob, 0);
    if(magic != CSMAGIC_EMBEDDED_SIGNATURE)
    {
        return std::nullopt;
    }

    const auto count = ReadBE32(super_blob, 8);

    for(std::uint32_t i = 0; i < count; ++i)
    {
        const std::size_t idx_offset = 12 + static_cast<std::size_t>(i) * 8;
        if(idx_offset + 8 > super_blob.size())
        {
            break;
        }

        const auto slot_type = ReadBE32(super_blob, idx_offset);
        const auto blob_offset = ReadBE32(super_blob, idx_offset + 4);

        if(slot_type == CSSLOT_CODEDIRECTORY)
        {
            if(blob_offset + 8 > super_blob.size())
            {
                return std::nullopt;
            }

            const auto cd_magic = ReadBE32(super_blob, blob_offset);
            const auto cd_length = ReadBE32(super_blob, blob_offset + 4);

            if(cd_magic != CSMAGIC_CODEDIRECTORY)
            {
                return std::nullopt;
            }

            if(blob_offset + cd_length > super_blob.size())
            {
                return std::nullopt;
            }

            return std::vector<std::uint8_t>(
                super_blob.begin() + static_cast<std::ptrdiff_t>(blob_offset),
                super_blob.begin() + static_cast<std::ptrdiff_t>(blob_offset + cd_length)
            );
        }
    }

    return std::nullopt;
}
