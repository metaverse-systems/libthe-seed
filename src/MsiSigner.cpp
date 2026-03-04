#include <libthe-seed/MsiSigner.hpp>

#include "ByteSwap.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "../external/picosha2.h"

namespace {

// ── CFBF Constants ──────────────────────────────────────────

constexpr std::uint8_t CFBF_MAGIC[8] = {
    0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1
};

constexpr std::uint32_t ENDOFCHAIN  = 0xFFFFFFFE;
constexpr std::uint32_t FREESECT    = 0xFFFFFFFF;
constexpr std::uint32_t FATSECT     = 0xFFFFFFFD;
constexpr std::uint32_t DIFSECT     = 0xFFFFFFFC;
constexpr std::uint32_t NOSTREAM    = 0xFFFFFFFF;

// Directory entry object types
constexpr std::uint8_t DIR_TYPE_UNKNOWN  = 0;
constexpr std::uint8_t DIR_TYPE_STORAGE  = 1;
constexpr std::uint8_t DIR_TYPE_STREAM   = 2;
constexpr std::uint8_t DIR_TYPE_ROOT     = 5;

// ── File I/O helpers ────────────────────────────────────────

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
        throw std::runtime_error("Truncated CFBF file: read beyond end at offset " +
                                  std::to_string(offset));
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return ByteSwapIfNeeded(value, true); // CFBF is always little-endian
}

template <typename T>
void WriteLE(std::vector<std::uint8_t> &bytes, std::size_t offset, T value)
{
    if(offset + sizeof(T) > bytes.size())
    {
        throw std::runtime_error("Write beyond end of CFBF data");
    }
    T le_value = ByteSwapIfNeeded(value, true);
    std::memcpy(bytes.data() + offset, &le_value, sizeof(T));
}

// ── CFBF Data Structures ───────────────────────────────────

struct CfbHeader
{
    std::uint16_t major_version;
    std::uint16_t minor_version;
    std::uint32_t sector_size;           // 512 (v3) or 4096 (v4)
    std::uint16_t sector_size_power;     // 9 or 12
    std::uint32_t mini_sector_size;      // 64
    std::uint32_t total_dir_sectors;     // v4 only (0 for v3)
    std::uint32_t total_fat_sectors;
    std::uint32_t first_dir_sector;
    std::uint32_t mini_stream_cutoff;    // 4096
    std::uint32_t first_mini_fat_sector;
    std::uint32_t total_mini_fat_sectors;
    std::uint32_t first_difat_sector;
    std::uint32_t total_difat_sectors;
};

struct CfbDirEntry
{
    std::u16string name;           // UTF-16LE name (without null terminator)
    std::uint16_t name_size;       // in bytes, including null terminator
    std::uint8_t type;             // 0=unknown, 1=storage, 2=stream, 5=root
    std::uint32_t left_sibling;
    std::uint32_t right_sibling;
    std::uint32_t child;
    std::uint32_t start_sector;
    std::uint64_t stream_size;
    std::size_t dir_index;         // position in the directory array
};

// ── CFBF Parser ────────────────────────────────────────────

struct CfbDocument
{
    CfbHeader header;
    std::vector<std::uint32_t> fat;       // Full FAT table
    std::vector<std::uint32_t> mini_fat;  // Mini-FAT table
    std::vector<CfbDirEntry> directory;   // All directory entries
    std::vector<std::uint8_t> bytes;      // Raw file bytes

    // Parse CFBF header
    void ParseHeader()
    {
        if(bytes.size() < 512)
        {
            throw std::runtime_error("File too small for CFBF header");
        }

        // Verify magic
        if(std::memcmp(bytes.data(), CFBF_MAGIC, 8) != 0)
        {
            throw std::runtime_error("Invalid CFBF magic signature");
        }

        header.minor_version = ReadLE<std::uint16_t>(bytes, 24);
        header.major_version = ReadLE<std::uint16_t>(bytes, 26);

        // Verify byte order (0xFFFE = little-endian)
        const auto byte_order = ReadLE<std::uint16_t>(bytes, 28);
        if(byte_order != 0xFFFE)
        {
            throw std::runtime_error("Unsupported CFBF byte order");
        }

        header.sector_size_power = ReadLE<std::uint16_t>(bytes, 30);
        header.sector_size = 1u << header.sector_size_power;

        const auto mini_size_power = ReadLE<std::uint16_t>(bytes, 32);
        header.mini_sector_size = 1u << mini_size_power;

        header.total_dir_sectors = ReadLE<std::uint32_t>(bytes, 40);
        header.total_fat_sectors = ReadLE<std::uint32_t>(bytes, 44);
        header.first_dir_sector = ReadLE<std::uint32_t>(bytes, 48);
        header.mini_stream_cutoff = ReadLE<std::uint32_t>(bytes, 56);
        header.first_mini_fat_sector = ReadLE<std::uint32_t>(bytes, 60);
        header.total_mini_fat_sectors = ReadLE<std::uint32_t>(bytes, 64);
        header.first_difat_sector = ReadLE<std::uint32_t>(bytes, 68);
        header.total_difat_sectors = ReadLE<std::uint32_t>(bytes, 72);
    }

    // Convert sector ID to file byte offset
    std::size_t SectorOffset(std::uint32_t sector_id) const
    {
        // For v3: sectors start after 512-byte header
        // For v4: sectors start after 4096-byte header
        const std::size_t header_size = (header.major_version == 4)
            ? 4096 : 512;
        return header_size + static_cast<std::size_t>(sector_id) * header.sector_size;
    }

    // Read a sector's raw bytes
    std::vector<std::uint8_t> ReadSector(std::uint32_t sector_id) const
    {
        const auto offset = SectorOffset(sector_id);
        if(offset + header.sector_size > bytes.size())
        {
            throw std::runtime_error("Sector " + std::to_string(sector_id) +
                                      " extends beyond file");
        }
        return std::vector<std::uint8_t>(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + header.sector_size)
        );
    }

    // Follow a FAT chain from a starting sector, collecting all sector IDs
    std::vector<std::uint32_t> FollowChain(std::uint32_t start_sector) const
    {
        std::vector<std::uint32_t> chain;
        std::uint32_t current = start_sector;
        const std::size_t max_iterations = fat.size() + 1;
        std::size_t iterations = 0;

        while(current != ENDOFCHAIN && current != FREESECT &&
              current < static_cast<std::uint32_t>(fat.size()))
        {
            if(++iterations > max_iterations)
            {
                throw std::runtime_error("Infinite loop in CFBF FAT chain");
            }
            chain.push_back(current);
            current = fat[current];
        }

        return chain;
    }

    // Build the full FAT from DIFAT entries in the header + DIFAT chain
    void BuildFat()
    {
        // First 109 DIFAT entries are in the header at offset 76
        std::vector<std::uint32_t> difat_entries;
        const std::size_t max_header_difat = 109;
        for(std::size_t i = 0; i < max_header_difat; ++i)
        {
            const auto entry = ReadLE<std::uint32_t>(bytes, 76 + i * 4);
            if(entry == FREESECT || entry == ENDOFCHAIN)
            {
                break;
            }
            difat_entries.push_back(entry);
        }

        // Follow DIFAT chain for additional FAT sector locations
        if(header.total_difat_sectors > 0 && header.first_difat_sector != ENDOFCHAIN)
        {
            std::uint32_t difat_sector = header.first_difat_sector;
            for(std::uint32_t i = 0; i < header.total_difat_sectors; ++i)
            {
                if(difat_sector == ENDOFCHAIN || difat_sector == FREESECT)
                {
                    break;
                }
                const auto offset = SectorOffset(difat_sector);
                const auto entries_per_sector = header.sector_size / 4 - 1; // last entry is next DIFAT
                for(std::size_t j = 0; j < entries_per_sector; ++j)
                {
                    const auto entry = ReadLE<std::uint32_t>(bytes, offset + j * 4);
                    if(entry == FREESECT || entry == ENDOFCHAIN)
                    {
                        break;
                    }
                    difat_entries.push_back(entry);
                }
                // Next DIFAT sector is at the end of this sector
                difat_sector = ReadLE<std::uint32_t>(bytes,
                    offset + (header.sector_size - 4));
            }
        }

        // Read FAT sectors
        fat.clear();
        for(const auto &fat_sector_id : difat_entries)
        {
            const auto offset = SectorOffset(fat_sector_id);
            const auto entries_per_sector = header.sector_size / 4;
            for(std::size_t i = 0; i < entries_per_sector; ++i)
            {
                fat.push_back(ReadLE<std::uint32_t>(bytes, offset + i * 4));
            }
        }
    }

    // Build mini-FAT table
    void BuildMiniFat()
    {
        mini_fat.clear();
        if(header.first_mini_fat_sector == ENDOFCHAIN ||
           header.total_mini_fat_sectors == 0)
        {
            return;
        }

        auto chain = FollowChain(header.first_mini_fat_sector);
        for(const auto &sector_id : chain)
        {
            const auto offset = SectorOffset(sector_id);
            const auto entries_per_sector = header.sector_size / 4;
            for(std::size_t i = 0; i < entries_per_sector; ++i)
            {
                mini_fat.push_back(ReadLE<std::uint32_t>(bytes, offset + i * 4));
            }
        }
    }

    // Parse all directory entries
    void ParseDirectory()
    {
        directory.clear();
        auto dir_chain = FollowChain(header.first_dir_sector);

        const auto entries_per_sector = header.sector_size / 128;
        std::size_t idx = 0;

        for(const auto &sector_id : dir_chain)
        {
            const auto sector_offset = SectorOffset(sector_id);
            for(std::size_t i = 0; i < entries_per_sector; ++i)
            {
                const auto entry_offset = sector_offset + i * 128;
                if(entry_offset + 128 > bytes.size())
                {
                    break;
                }

                CfbDirEntry entry{};
                entry.dir_index = idx++;

                // Name: UTF-16LE at offset 0, up to 64 bytes (32 chars)
                entry.name_size = ReadLE<std::uint16_t>(bytes, entry_offset + 64);
                if(entry.name_size > 0 && entry.name_size <= 64)
                {
                    const auto name_chars = (entry.name_size / 2) - 1; // exclude null terminator
                    for(std::size_t c = 0; c < name_chars; ++c)
                    {
                        char16_t ch = ReadLE<std::uint16_t>(bytes, entry_offset + c * 2);
                        entry.name.push_back(ch);
                    }
                }

                entry.type = bytes[entry_offset + 66];
                entry.left_sibling = ReadLE<std::uint32_t>(bytes, entry_offset + 68);
                entry.right_sibling = ReadLE<std::uint32_t>(bytes, entry_offset + 72);
                entry.child = ReadLE<std::uint32_t>(bytes, entry_offset + 76);
                entry.start_sector = ReadLE<std::uint32_t>(bytes, entry_offset + 116);

                if(header.major_version == 4)
                {
                    entry.stream_size = ReadLE<std::uint64_t>(bytes, entry_offset + 120);
                }
                else
                {
                    // v3: only lower 32 bits
                    entry.stream_size = ReadLE<std::uint32_t>(bytes, entry_offset + 120);
                }

                directory.push_back(entry);
            }
        }
    }

    // Read a stream's data given its directory entry
    std::vector<std::uint8_t> ReadStream(const CfbDirEntry &entry) const
    {
        if(entry.stream_size == 0)
        {
            return {};
        }

        std::vector<std::uint8_t> data;
        data.reserve(static_cast<std::size_t>(entry.stream_size));

        // Use mini-stream only if the entry qualifies AND its start
        // sector is actually within the mini-FAT.  WriteStream always
        // allocates regular sectors, so newly-written small streams will
        // have a start_sector that exceeds the mini-FAT range and should
        // be read from regular sectors.
        const bool use_mini_stream =
            entry.type != DIR_TYPE_ROOT &&
            entry.stream_size < header.mini_stream_cutoff &&
            !mini_fat.empty() &&
            entry.start_sector < static_cast<std::uint32_t>(mini_fat.size());

        if(use_mini_stream)
        {
            // Read from mini-stream
            // The mini-stream is stored in the root entry's stream
            const auto &root = directory[0];
            auto root_chain = FollowChain(root.start_sector);

            // Build the complete mini-stream data
            std::vector<std::uint8_t> mini_stream_data;
            for(const auto &sid : root_chain)
            {
                const auto offset = SectorOffset(sid);
                const auto end = std::min(offset + header.sector_size, bytes.size());
                mini_stream_data.insert(mini_stream_data.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(end));
            }

            // Follow mini-FAT chain
            std::uint32_t mini_sector = entry.start_sector;
            std::uint64_t remaining = entry.stream_size;
            const std::size_t max_iter = mini_fat.size() + 1;
            std::size_t iter = 0;

            while(mini_sector != ENDOFCHAIN && remaining > 0 &&
                  mini_sector < static_cast<std::uint32_t>(mini_fat.size()))
            {
                if(++iter > max_iter)
                {
                    throw std::runtime_error("Infinite loop in mini-FAT chain");
                }
                const auto mini_offset = static_cast<std::size_t>(mini_sector) *
                                          header.mini_sector_size;
                const auto to_read = std::min(
                    static_cast<std::uint64_t>(header.mini_sector_size), remaining);

                if(mini_offset + to_read <= mini_stream_data.size())
                {
                    data.insert(data.end(),
                        mini_stream_data.begin() + static_cast<std::ptrdiff_t>(mini_offset),
                        mini_stream_data.begin() + static_cast<std::ptrdiff_t>(mini_offset + to_read));
                }
                remaining -= to_read;
                mini_sector = mini_fat[mini_sector];
            }
        }
        else
        {
            // Read from regular sectors
            auto chain = FollowChain(entry.start_sector);
            std::uint64_t remaining = entry.stream_size;

            for(const auto &sector_id : chain)
            {
                if(remaining == 0) break;
                const auto offset = SectorOffset(sector_id);
                const auto to_read = std::min(
                    static_cast<std::uint64_t>(header.sector_size), remaining);
                const auto end = std::min(offset + static_cast<std::size_t>(to_read),
                                           bytes.size());

                data.insert(data.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(end));
                remaining -= to_read;
            }
        }

        // Trim to exact stream size
        if(data.size() > static_cast<std::size_t>(entry.stream_size))
        {
            data.resize(static_cast<std::size_t>(entry.stream_size));
        }

        return data;
    }

    // Convert UTF-16 name to UTF-8 for comparison
    static std::string Utf16ToUtf8(const std::u16string &u16)
    {
        std::string result;
        for(char16_t ch : u16)
        {
            if(ch < 0x80)
            {
                result.push_back(static_cast<char>(ch));
            }
            else if(ch < 0x800)
            {
                result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
            else
            {
                result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }
        return result;
    }

    // Check if a name matches the digital signature stream names
    static bool IsSignatureStream(const std::u16string &name)
    {
        // \x05DigitalSignature
        static const std::u16string sig_name = {
            0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
            u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
        };
        // \x05MsiDigitalSignatureEx
        static const std::u16string sig_ex_name = {
            0x0005, u'M', u's', u'i', u'D', u'i', u'g', u'i', u't', u'a', u'l',
            u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e', u'E', u'x'
        };

        return name == sig_name || name == sig_ex_name;
    }

    static bool IsDigitalSignatureStream(const std::u16string &name)
    {
        static const std::u16string sig_name = {
            0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
            u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
        };
        return name == sig_name;
    }

    // Find a directory entry by name (searches children of root)
    const CfbDirEntry* FindEntry(const std::u16string &name) const
    {
        for(const auto &entry : directory)
        {
            if(entry.name == name && entry.type == DIR_TYPE_STREAM)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    // Recursively collect all streams from the directory tree via red-black tree traversal
    void CollectStreams(std::uint32_t entry_id,
                        std::vector<const CfbDirEntry*> &streams,
                        bool include_signature_streams = false) const
    {
        if(entry_id == NOSTREAM || entry_id >= directory.size())
        {
            return;
        }

        const auto &entry = directory[entry_id];

        // In-order traversal of the red-black tree
        CollectStreams(entry.left_sibling, streams, include_signature_streams);

        if(entry.type == DIR_TYPE_STREAM)
        {
            if(include_signature_streams || !IsSignatureStream(entry.name))
            {
                streams.push_back(&entry);
            }
        }
        else if(entry.type == DIR_TYPE_STORAGE)
        {
            // Recurse into storage's children
            if(entry.child != NOSTREAM)
            {
                CollectStreams(entry.child, streams, include_signature_streams);
            }
        }

        CollectStreams(entry.right_sibling, streams, include_signature_streams);
    }

    // Allocate a new sector, extending the file and FAT
    std::uint32_t AllocateSector()
    {
        // Find a free sector in existing FAT
        for(std::size_t i = 0; i < fat.size(); ++i)
        {
            if(fat[i] == FREESECT)
            {
                fat[i] = ENDOFCHAIN;
                // Ensure file is large enough
                const auto needed = SectorOffset(static_cast<std::uint32_t>(i)) +
                                     header.sector_size;
                if(bytes.size() < needed)
                {
                    bytes.resize(needed, 0);
                }
                return static_cast<std::uint32_t>(i);
            }
        }

        // No free sector — extend FAT
        const auto new_id = static_cast<std::uint32_t>(fat.size());
        fat.push_back(ENDOFCHAIN);

        // Extend file
        const auto needed = SectorOffset(new_id) + header.sector_size;
        if(bytes.size() < needed)
        {
            bytes.resize(needed, 0);
        }

        return new_id;
    }

    // Write FAT back to file
    void WriteFat()
    {
        // Collect ALL DIFAT entries: first 109 from header, then DIFAT chain
        std::vector<std::uint32_t> fat_sector_ids;
        const std::size_t max_header_difat = 109;
        for(std::size_t i = 0; i < max_header_difat; ++i)
        {
            const auto entry = ReadLE<std::uint32_t>(bytes, 76 + i * 4);
            if(entry == FREESECT || entry == ENDOFCHAIN)
            {
                break;
            }
            fat_sector_ids.push_back(entry);
        }

        // Follow DIFAT chain for additional FAT sector locations
        if(header.total_difat_sectors > 0 && header.first_difat_sector != ENDOFCHAIN)
        {
            std::uint32_t difat_sector = header.first_difat_sector;
            for(std::uint32_t i = 0; i < header.total_difat_sectors; ++i)
            {
                if(difat_sector == ENDOFCHAIN || difat_sector == FREESECT)
                {
                    break;
                }
                const auto offset = SectorOffset(difat_sector);
                const auto entries_per_difat_sector = header.sector_size / 4 - 1;
                for(std::size_t j = 0; j < entries_per_difat_sector; ++j)
                {
                    const auto entry = ReadLE<std::uint32_t>(bytes, offset + j * 4);
                    if(entry == FREESECT || entry == ENDOFCHAIN)
                    {
                        break;
                    }
                    fat_sector_ids.push_back(entry);
                }
                difat_sector = ReadLE<std::uint32_t>(bytes,
                    offset + (header.sector_size - 4));
            }
        }

        // Calculate how many entries fit per FAT sector
        const auto entries_per_sector = header.sector_size / 4;

        // Ensure we have enough FAT sectors
        while(fat_sector_ids.size() * entries_per_sector < fat.size())
        {
            // Need to allocate a new FAT sector
            // This is tricky — the new FAT sector itself needs a FAT entry
            // For simplicity, extend the file and add the sector to DIFAT
            const auto new_fat_sector = static_cast<std::uint32_t>(
                (bytes.size() - ((header.major_version == 4) ? 4096 : 512)) /
                header.sector_size);
            bytes.resize(bytes.size() + header.sector_size, 0);
            fat_sector_ids.push_back(new_fat_sector);

            // Extend FAT to cover the new sector
            while(fat.size() <= new_fat_sector)
            {
                fat.push_back(FREESECT);
            }
            fat[new_fat_sector] = FATSECT;

            // Update header
            header.total_fat_sectors = static_cast<std::uint32_t>(fat_sector_ids.size());
            WriteLE<std::uint32_t>(bytes, 44, header.total_fat_sectors);

            // Write DIFAT entry in header
            if(fat_sector_ids.size() <= 109)
            {
                WriteLE<std::uint32_t>(bytes,
                    76 + (fat_sector_ids.size() - 1) * 4, new_fat_sector);
            }
        }

        // Write FAT entries into FAT sectors
        std::size_t fat_idx = 0;
        for(const auto &fat_sector_id : fat_sector_ids)
        {
            const auto offset = SectorOffset(fat_sector_id);
            for(std::size_t i = 0; i < entries_per_sector && fat_idx < fat.size(); ++i)
            {
                WriteLE<std::uint32_t>(bytes, offset + i * 4, fat[fat_idx++]);
            }
            // Zero remaining entries in this sector (only if we stopped mid-sector)
            const auto remainder_start = fat_idx % entries_per_sector;
            if(fat_idx >= fat.size() && remainder_start > 0)
            {
                for(std::size_t i = remainder_start; i < entries_per_sector; ++i)
                {
                    WriteLE<std::uint32_t>(bytes, offset + i * 4, FREESECT);
                }
            }
        }
    }

    // Write a directory entry back to file
    void WriteDirEntry(std::size_t entry_index, const CfbDirEntry &entry)
    {
        auto dir_chain = FollowChain(header.first_dir_sector);
        const auto entries_per_sector = header.sector_size / 128;

        const auto sector_idx = entry_index / entries_per_sector;
        const auto offset_in_sector = (entry_index % entries_per_sector) * 128;

        if(sector_idx >= dir_chain.size())
        {
            throw std::runtime_error("Directory entry index out of range");
        }

        const auto file_offset = SectorOffset(dir_chain[sector_idx]) + offset_in_sector;

        // Write name (UTF-16LE, null-padded to 64 bytes)
        std::memset(bytes.data() + file_offset, 0, 64);
        for(std::size_t i = 0; i < entry.name.size() && i < 31; ++i)
        {
            WriteLE<std::uint16_t>(bytes, file_offset + i * 2, entry.name[i]);
        }
        // Null terminator
        if(entry.name.size() < 32)
        {
            WriteLE<std::uint16_t>(bytes, file_offset + entry.name.size() * 2, 0);
        }

        // Name size (including null terminator, in bytes)
        WriteLE<std::uint16_t>(bytes, file_offset + 64,
            static_cast<std::uint16_t>((entry.name.size() + 1) * 2));

        // Type
        bytes[file_offset + 66] = entry.type;

        // Siblings and child
        WriteLE<std::uint32_t>(bytes, file_offset + 68, entry.left_sibling);
        WriteLE<std::uint32_t>(bytes, file_offset + 72, entry.right_sibling);
        WriteLE<std::uint32_t>(bytes, file_offset + 76, entry.child);

        // Start sector
        WriteLE<std::uint32_t>(bytes, file_offset + 116, entry.start_sector);

        // Stream size
        if(header.major_version == 4)
        {
            WriteLE<std::uint64_t>(bytes, file_offset + 120, entry.stream_size);
        }
        else
        {
            WriteLE<std::uint32_t>(bytes, file_offset + 120,
                static_cast<std::uint32_t>(entry.stream_size));
        }
    }

    // Zero out a directory entry in the raw file bytes
    void ZeroDirEntry(std::size_t entry_index)
    {
        auto dir_chain = FollowChain(header.first_dir_sector);
        const auto entries_per_sector = header.sector_size / 128;

        const auto sector_idx = entry_index / entries_per_sector;
        const auto offset_in_sector = (entry_index % entries_per_sector) * 128;

        if(sector_idx >= dir_chain.size())
        {
            return;
        }

        const auto file_offset = SectorOffset(dir_chain[sector_idx]) + offset_in_sector;
        if(file_offset + 128 <= bytes.size())
        {
            std::memset(bytes.data() + file_offset, 0, 128);
        }
    }

    // Write stream data to a directory entry, allocating sectors as needed
    void WriteStream(std::size_t entry_index,
                     const std::vector<std::uint8_t> &data)
    {
        auto &entry = directory[entry_index];

        // Free existing sector chain if any
        if(entry.stream_size > 0 && entry.start_sector != ENDOFCHAIN &&
           entry.start_sector != FREESECT)
        {
            // Always free via regular FAT chain because WriteStream
            // always writes to regular sectors (not mini-stream).
            auto chain = FollowChain(entry.start_sector);
            for(const auto &sid : chain)
            {
                if(sid < fat.size())
                {
                    fat[sid] = FREESECT;
                }
            }
        }

        if(data.empty())
        {
            entry.start_sector = ENDOFCHAIN;
            entry.stream_size = 0;
            WriteDirEntry(entry_index, entry);
            return;
        }

        // For MSI signature streams, always use regular sectors
        // (signature data is typically > 4096 bytes)
        const auto sectors_needed = (data.size() + header.sector_size - 1) /
                                     header.sector_size;

        std::vector<std::uint32_t> new_chain;
        for(std::size_t i = 0; i < sectors_needed; ++i)
        {
            new_chain.push_back(AllocateSector());
        }

        // Link the chain
        for(std::size_t i = 0; i < new_chain.size() - 1; ++i)
        {
            fat[new_chain[i]] = new_chain[i + 1];
        }
        fat[new_chain.back()] = ENDOFCHAIN;

        // Write data to sectors
        std::size_t data_offset = 0;
        for(const auto &sid : new_chain)
        {
            const auto sector_off = SectorOffset(sid);
            const auto to_write = std::min(
                static_cast<std::size_t>(header.sector_size),
                data.size() - data_offset);

            std::memcpy(bytes.data() + sector_off, data.data() + data_offset, to_write);

            // Zero-pad remaining sector bytes
            if(to_write < header.sector_size)
            {
                std::memset(bytes.data() + sector_off + to_write, 0,
                            header.sector_size - to_write);
            }

            data_offset += to_write;
        }

        // Update entry
        entry.start_sector = new_chain[0];
        entry.stream_size = data.size();
        WriteDirEntry(entry_index, entry);
    }

    // Add a new directory entry as a child of the root entry
    std::size_t AddRootChild(const std::u16string &name, std::uint8_t type)
    {
        // Create a new blank entry
        CfbDirEntry new_entry{};
        new_entry.name = name;
        new_entry.name_size = static_cast<std::uint16_t>((name.size() + 1) * 2);
        new_entry.type = type;
        new_entry.left_sibling = NOSTREAM;
        new_entry.right_sibling = NOSTREAM;
        new_entry.child = NOSTREAM;
        new_entry.start_sector = ENDOFCHAIN;
        new_entry.stream_size = 0;

        // Find a free directory entry slot or add a new one
        std::size_t new_index = directory.size();

        // Check for an unused slot
        for(std::size_t i = 1; i < directory.size(); ++i)
        {
            if(directory[i].type == DIR_TYPE_UNKNOWN)
            {
                new_index = i;
                break;
            }
        }

        if(new_index == directory.size())
        {
            // Need to extend directory
            auto dir_chain = FollowChain(header.first_dir_sector);
            const auto entries_per_sector = header.sector_size / 128;
            const auto current_capacity = dir_chain.size() * entries_per_sector;

            if(new_index >= current_capacity)
            {
                // Allocate a new directory sector
                auto new_sector = AllocateSector();

                // Link it to the chain
                if(!dir_chain.empty())
                {
                    fat[dir_chain.back()] = new_sector;
                }
                fat[new_sector] = ENDOFCHAIN;

                // Zero the new sector
                const auto offset = SectorOffset(new_sector);
                std::memset(bytes.data() + offset, 0, header.sector_size);
            }

            directory.push_back(new_entry);
        }

        new_entry.dir_index = new_index;
        directory[new_index] = new_entry;

        // Insert into the root's child tree
        // Simple approach: add as right-most sibling
        auto &root = directory[0];
        if(root.child == NOSTREAM)
        {
            root.child = static_cast<std::uint32_t>(new_index);
            WriteDirEntry(0, root);
        }
        else
        {
            // Walk to the right-most sibling
            std::uint32_t current = root.child;
            while(directory[current].right_sibling != NOSTREAM &&
                  directory[current].right_sibling < directory.size())
            {
                current = directory[current].right_sibling;
            }
            directory[current].right_sibling = static_cast<std::uint32_t>(new_index);
            WriteDirEntry(current, directory[current]);
        }

        WriteDirEntry(new_index, directory[new_index]);
        return new_index;
    }

    // Remove a directory entry (mark as unused, free its sectors)
    void RemoveEntry(std::size_t entry_index)
    {
        if(entry_index >= directory.size())
        {
            return;
        }

        auto &entry = directory[entry_index];

        // Free the sector chain
        if(entry.stream_size > 0 && entry.start_sector != ENDOFCHAIN &&
           entry.start_sector != FREESECT)
        {
            if(entry.stream_size >= header.mini_stream_cutoff ||
               entry.type == DIR_TYPE_ROOT)
            {
                auto chain = FollowChain(entry.start_sector);
                for(const auto &sid : chain)
                {
                    if(sid < fat.size())
                    {
                        fat[sid] = FREESECT;
                    }
                }
            }
            else
            {
                // Mini-stream: free mini-FAT chain
                std::uint32_t mini_sector = entry.start_sector;
                while(mini_sector != ENDOFCHAIN &&
                      mini_sector < static_cast<std::uint32_t>(mini_fat.size()))
                {
                    auto next = mini_fat[mini_sector];
                    mini_fat[mini_sector] = FREESECT;
                    mini_sector = next;
                }
            }
        }

        // Unlink from the sibling tree
        // Find parent reference
        auto &root = directory[0];
        if(root.child == entry_index)
        {
            // Root's direct child — replace with right or left sibling
            if(entry.right_sibling != NOSTREAM)
            {
                root.child = entry.right_sibling;
                if(entry.left_sibling != NOSTREAM)
                {
                    // Attach left subtree to leftmost of right subtree
                    std::uint32_t leftmost = entry.right_sibling;
                    while(directory[leftmost].left_sibling != NOSTREAM &&
                          directory[leftmost].left_sibling < directory.size())
                    {
                        leftmost = directory[leftmost].left_sibling;
                    }
                    directory[leftmost].left_sibling = entry.left_sibling;
                    WriteDirEntry(leftmost, directory[leftmost]);
                }
            }
            else
            {
                root.child = entry.left_sibling;
            }
            WriteDirEntry(0, root);
        }
        else
        {
            // Find the entry that references this one
            for(auto &other : directory)
            {
                if(other.left_sibling == entry_index)
                {
                    other.left_sibling = entry.right_sibling != NOSTREAM
                        ? entry.right_sibling : entry.left_sibling;
                    WriteDirEntry(other.dir_index, other);
                    break;
                }
                if(other.right_sibling == entry_index)
                {
                    other.right_sibling = entry.right_sibling != NOSTREAM
                        ? entry.right_sibling : entry.left_sibling;
                    WriteDirEntry(other.dir_index, other);
                    break;
                }
            }
        }

        // Mark as unused
        entry.type = DIR_TYPE_UNKNOWN;
        entry.name.clear();
        entry.name_size = 0;
        entry.start_sector = ENDOFCHAIN;
        entry.stream_size = 0;
        entry.left_sibling = NOSTREAM;
        entry.right_sibling = NOSTREAM;
        entry.child = NOSTREAM;
        ZeroDirEntry(entry_index);
    }

    // Full parse
    void Parse()
    {
        ParseHeader();
        BuildFat();
        BuildMiniFat();
        ParseDirectory();
    }
};

} // anonymous namespace

// ── MsiSigner Public Methods ───────────────────────────────

bool MsiSigner::IsMsi(const std::string &file_path)
{
    std::ifstream file(file_path, std::ios::binary);
    if(!file)
    {
        return false;
    }

    std::uint8_t magic[8] = {0};
    file.read(reinterpret_cast<char*>(magic), 8);
    if(file.gcount() < 8)
    {
        return false;
    }

    return std::memcmp(magic, CFBF_MAGIC, 8) == 0;
}

MsiSigner::DigestResult MsiSigner::ComputeAuthenticodeDigest(const std::string &file_path)
{
    CfbDocument doc;
    doc.bytes = ReadFileBytes(file_path);
    doc.Parse();

    // Collect all non-signature streams from the root's children
    std::vector<const CfbDirEntry*> streams;
    if(!doc.directory.empty() && doc.directory[0].child != NOSTREAM)
    {
        doc.CollectStreams(doc.directory[0].child, streams, false);
    }

    // Sort streams alphabetically by name (case-insensitive)
    std::sort(streams.begin(), streams.end(),
        [](const CfbDirEntry *a, const CfbDirEntry *b)
        {
            // Case-insensitive comparison of UTF-16 names
            auto name_a = a->name;
            auto name_b = b->name;
            for(auto &ch : name_a) { if(ch >= u'A' && ch <= u'Z') ch += 32; }
            for(auto &ch : name_b) { if(ch >= u'A' && ch <= u'Z') ch += 32; }
            return name_a < name_b;
        });

    // Hash all stream data in sorted order
    picosha2::hash256_one_by_one hasher;
    hasher.init();

    for(const auto *entry : streams)
    {
        auto data = doc.ReadStream(*entry);
        if(!data.empty())
        {
            hasher.process(data.begin(), data.end());
        }
    }

    hasher.finish();

    DigestResult result;
    result.digest.resize(picosha2::k_digest_size);
    hasher.get_hash_bytes(result.digest.begin(), result.digest.end());

    return result;
}

void MsiSigner::EmbedSignature(
    const std::string &file_path,
    const std::vector<std::uint8_t> &pkcs7_der)
{
    CfbDocument doc;
    doc.bytes = ReadFileBytes(file_path);
    doc.Parse();

    // Find or create the \x05DigitalSignature stream
    static const std::u16string sig_name = {
        0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
        u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
    };

    const CfbDirEntry *existing = doc.FindEntry(sig_name);
    std::size_t entry_index;

    if(existing)
    {
        entry_index = existing->dir_index;
    }
    else
    {
        entry_index = doc.AddRootChild(sig_name, DIR_TYPE_STREAM);
    }

    // Write CMS blob to the stream
    doc.WriteStream(entry_index, pkcs7_der);

    // Update FAT in file
    doc.WriteFat();

    // Atomic write
    WriteFileBytes(file_path, doc.bytes);
}

std::optional<std::vector<std::uint8_t>> MsiSigner::ExtractSignature(
    const std::string &file_path)
{
    CfbDocument doc;
    doc.bytes = ReadFileBytes(file_path);
    doc.Parse();

    static const std::u16string sig_name = {
        0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
        u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
    };

    const auto *entry = doc.FindEntry(sig_name);
    if(!entry || entry->stream_size == 0)
    {
        return std::nullopt;
    }

    return doc.ReadStream(*entry);
}

bool MsiSigner::HasEmbeddedSignature(const std::string &file_path)
{
    CfbDocument doc;
    doc.bytes = ReadFileBytes(file_path);
    doc.Parse();

    static const std::u16string sig_name = {
        0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
        u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
    };

    const auto *entry = doc.FindEntry(sig_name);
    return entry != nullptr && entry->stream_size > 0;
}

void MsiSigner::StripSignature(const std::string &file_path)
{
    CfbDocument doc;
    doc.bytes = ReadFileBytes(file_path);
    doc.Parse();

    bool modified = false;

    // Remove \x05DigitalSignature
    static const std::u16string sig_name = {
        0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
        u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
    };

    // Remove \x05MsiDigitalSignatureEx
    static const std::u16string sig_ex_name = {
        0x0005, u'M', u's', u'i', u'D', u'i', u'g', u'i', u't', u'a', u'l',
        u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e', u'E', u'x'
    };

    for(std::size_t i = 1; i < doc.directory.size(); ++i)
    {
        if(doc.directory[i].name == sig_name || doc.directory[i].name == sig_ex_name)
        {
            doc.RemoveEntry(i);
            modified = true;
        }
    }

    if(modified)
    {
        doc.WriteFat();
        WriteFileBytes(file_path, doc.bytes);
    }
}
