#include <libthe-seed/MsiSigner.hpp>

#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"
#include "internal/MsiSignatureSize.hpp"

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
using seed::internal::CheckMsiSignatureSize;
using seed::internal::GuardEntryPoint;
using seed::internal::MutableByteSpan;
using seed::internal::RangeFits;
using seed::internal::ThrowMalformed;

constexpr const char *kFormat = "MSI";

// Compound files are always little-endian.
constexpr ByteOrder kLittle = ByteOrder::Little;

// ── CFBF Constants ──────────────────────────────────────────

constexpr std::uint8_t CFBF_MAGIC[8] = {
    0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1
};

constexpr std::uint32_t ENDOFCHAIN  = 0xFFFFFFFE;
constexpr std::uint32_t FREESECT    = 0xFFFFFFFF;
constexpr std::uint32_t FATSECT     = 0xFFFFFFFD;
constexpr std::uint32_t NOSTREAM    = 0xFFFFFFFF;

// Directory entry object types
constexpr std::uint8_t DIR_TYPE_UNKNOWN  = 0;
constexpr std::uint8_t DIR_TYPE_STORAGE  = 1;
constexpr std::uint8_t DIR_TYPE_STREAM   = 2;
constexpr std::uint8_t DIR_TYPE_ROOT     = 5;

constexpr std::uint16_t SECTOR_EXPONENT_V3 = 9;
constexpr std::uint16_t SECTOR_EXPONENT_V4 = 12;
constexpr std::uint16_t MINI_SECTOR_EXPONENT = 6;
constexpr std::uint32_t MINI_STREAM_CUTOFF = 4096;
constexpr std::size_t HEADER_DIFAT_ENTRIES = 109;
constexpr std::uint64_t DIR_ENTRY_SIZE = 128;

// ── CFBF Data Structures ───────────────────────────────────

struct CfbHeader
{
    std::uint16_t major_version = 0;
    std::uint16_t minor_version = 0;
    std::uint32_t sector_size = 0;           // 512 (v3) or 4096 (v4)
    std::uint16_t sector_size_power = 0;     // 9 or 12
    std::uint32_t mini_sector_size = 0;      // 64
    std::uint32_t total_dir_sectors = 0;     // v4 only (0 for v3)
    std::uint32_t total_fat_sectors = 0;
    std::uint32_t first_dir_sector = 0;
    std::uint32_t mini_stream_cutoff = 0;    // 4096
    std::uint32_t first_mini_fat_sector = 0;
    std::uint32_t total_mini_fat_sectors = 0;
    std::uint32_t first_difat_sector = 0;
    std::uint32_t total_difat_sectors = 0;
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

    std::vector<std::uint32_t> fat_sectors;  // Sectors that hold the FAT
    std::vector<std::uint32_t> dir_chain;    // Sectors that hold the directory
    std::vector<std::size_t> tree_streams;   // Streams reachable from the root, in tree order

    // The mini stream (the root's stream) is built once per document.
    std::vector<std::uint8_t> mini_stream;
    bool mini_stream_ready = false;

    // Which stream read each sector and mini sector in this operation.
    std::vector<std::uint32_t> sector_owner;
    std::vector<std::uint32_t> mini_owner;
    std::uint32_t last_owner = 0;

    // Lowest FAT index that may still be free.
    std::size_t free_cursor = 0;

    // ── Geometry ───────────────────────────────────────────

    std::uint64_t HeaderSize() const
    {
        return (this->header.major_version == 4) ? 4096 : 512;
    }

    // Number of sectors after the header; a partial last sector counts.
    std::uint64_t SectorCount() const
    {
        const std::uint64_t header_size = this->HeaderSize();
        if(this->bytes.size() <= header_size)
        {
            return 0;
        }
        const std::uint64_t sector_size = this->header.sector_size;
        return (this->bytes.size() - header_size + sector_size - 1) / sector_size;
    }

    // Convert sector ID to file byte offset
    std::uint64_t SectorStart(std::uint32_t sector_id) const
    {
        return this->HeaderSize() +
               static_cast<std::uint64_t>(sector_id) * this->header.sector_size;
    }

    // The first `length` bytes of a sector, which must lie inside the file.
    ByteSpan Sector(std::uint32_t sector_id, std::uint64_t length, std::string_view what) const
    {
        const std::uint64_t count = this->SectorCount();
        if(sector_id >= count)
        {
            ThrowMalformed(kFormat, std::string(what) + " " + std::to_string(sector_id) +
                                        " is past the end of the file (" +
                                        std::to_string(count) + " sectors)");
        }
        const ByteSpan file(this->bytes, kFormat);
        return file.Sub(this->SectorStart(sector_id), length, what);
    }

    // Make the file long enough to hold a whole sector.
    void EnsureSectorBytes(std::uint32_t sector_id)
    {
        const std::uint64_t needed = this->SectorStart(sector_id) + this->header.sector_size;
        if(this->bytes.size() < needed)
        {
            this->bytes.resize(static_cast<std::size_t>(needed), 0);
        }
    }

    // ── Header ─────────────────────────────────────────────

    // Parse CFBF header
    void ParseHeader()
    {
        if(this->bytes.size() < 512)
        {
            ThrowMalformed(kFormat, "file is too small for the header (" +
                                        std::to_string(this->bytes.size()) + " bytes)");
        }

        // Verify magic
        if(std::memcmp(this->bytes.data(), CFBF_MAGIC, 8) != 0)
        {
            ThrowMalformed(kFormat, "signature is not the compound file magic");
        }

        const ByteSpan file(this->bytes, kFormat);
        this->header.minor_version = file.Read<std::uint16_t>(24, kLittle, "minor version");
        this->header.major_version = file.Read<std::uint16_t>(26, kLittle, "major version");

        // Verify byte order (0xFFFE = little-endian)
        const auto byte_order = file.Read<std::uint16_t>(28, kLittle, "byte order");
        if(byte_order != 0xFFFE)
        {
            ThrowMalformed(kFormat, "byte order mark " + std::to_string(byte_order) +
                                        " is not 65534");
        }

        const auto sector_power = file.Read<std::uint16_t>(30, kLittle, "sector size exponent");
        const auto mini_power = file.Read<std::uint16_t>(32, kLittle, "mini-sector exponent");

        if(sector_power != SECTOR_EXPONENT_V3 && sector_power != SECTOR_EXPONENT_V4)
        {
            ThrowMalformed(kFormat, "sector size exponent " + std::to_string(sector_power) +
                                        " is not 9 or 12");
        }
        if(this->header.major_version != 3 && this->header.major_version != 4)
        {
            ThrowMalformed(kFormat, "major version " + std::to_string(this->header.major_version) +
                                        " is not 3 or 4");
        }
        if((this->header.major_version == 3) != (sector_power == SECTOR_EXPONENT_V3))
        {
            ThrowMalformed(kFormat, "sector size exponent " + std::to_string(sector_power) +
                                        " does not match major version " +
                                        std::to_string(this->header.major_version));
        }
        if(mini_power != MINI_SECTOR_EXPONENT)
        {
            ThrowMalformed(kFormat, "mini-sector exponent " + std::to_string(mini_power) +
                                        " is not 6");
        }
        if(this->bytes.size() < this->HeaderSize())
        {
            ThrowMalformed(kFormat, "file is too small for the version 4 header (" +
                                        std::to_string(this->bytes.size()) + " bytes)");
        }

        this->header.sector_size_power = sector_power;
        this->header.sector_size = 1u << sector_power;
        this->header.mini_sector_size = 1u << mini_power;

        this->header.total_dir_sectors = file.Read<std::uint32_t>(40, kLittle, "directory sector count");
        this->header.total_fat_sectors = file.Read<std::uint32_t>(44, kLittle, "FAT sector count");
        this->header.first_dir_sector = file.Read<std::uint32_t>(48, kLittle, "first directory sector");
        this->header.mini_stream_cutoff = file.Read<std::uint32_t>(56, kLittle, "mini-stream cutoff");
        this->header.first_mini_fat_sector = file.Read<std::uint32_t>(60, kLittle, "first mini-FAT sector");
        this->header.total_mini_fat_sectors = file.Read<std::uint32_t>(64, kLittle, "mini-FAT sector count");
        this->header.first_difat_sector = file.Read<std::uint32_t>(68, kLittle, "first DIFAT sector");
        this->header.total_difat_sectors = file.Read<std::uint32_t>(72, kLittle, "DIFAT sector count");

        if(this->header.mini_stream_cutoff != MINI_STREAM_CUTOFF)
        {
            ThrowMalformed(kFormat, "mini-stream cutoff " +
                                        std::to_string(this->header.mini_stream_cutoff) +
                                        " is not 4096");
        }
    }

    // ── Chains ─────────────────────────────────────────────

    // Follow a FAT chain from a starting sector, collecting all sector IDs.
    // `owner` records which walk visited each sector (`id` is never 0): a
    // second visit by the same walk is a loop, a visit by another walk means
    // the sector belongs to two streams.
    std::vector<std::uint32_t> WalkChain(std::uint32_t start_sector,
                                         std::vector<std::uint32_t> &owner, std::uint32_t id,
                                         std::string_view what) const
    {
        std::vector<std::uint32_t> chain;
        const std::uint64_t count = this->SectorCount();
        std::uint32_t current = start_sector;

        while(current != ENDOFCHAIN)
        {
            if(current >= this->fat.size())
            {
                ThrowMalformed(kFormat, std::string(what) + " chain sector " +
                                            std::to_string(current) +
                                            " is past the end of the FAT (" +
                                            std::to_string(this->fat.size()) + " entries)");
            }
            if(current >= count)
            {
                ThrowMalformed(kFormat, std::string(what) + " chain sector " +
                                            std::to_string(current) +
                                            " is past the end of the file (" +
                                            std::to_string(count) + " sectors)");
            }
            if(owner[current] == id)
            {
                ThrowMalformed(kFormat, std::string(what) + " chain has a loop at sector " +
                                            std::to_string(current));
            }
            if(owner[current] != 0)
            {
                ThrowMalformed(kFormat, "sector " + std::to_string(current) +
                                            " is used by more than one stream");
            }
            owner[current] = id;
            chain.push_back(current);
            current = this->fat[current];
        }

        return chain;
    }

    // A chain walked on its own.
    std::vector<std::uint32_t> FollowChain(std::uint32_t start_sector, std::string_view what) const
    {
        std::vector<std::uint32_t> owner(static_cast<std::size_t>(this->SectorCount()), 0);
        return this->WalkChain(start_sector, owner, 1, what);
    }

    // Same walk through the mini-FAT.
    std::vector<std::uint32_t> WalkMiniChain(std::uint32_t start_sector, std::uint32_t id)
    {
        if(this->mini_owner.size() != this->mini_fat.size())
        {
            this->mini_owner.assign(this->mini_fat.size(), 0);
        }

        std::vector<std::uint32_t> chain;
        std::uint32_t current = start_sector;
        while(current != ENDOFCHAIN)
        {
            if(current >= this->mini_fat.size())
            {
                ThrowMalformed(kFormat, "stream mini sector " + std::to_string(current) +
                                            " is past the end of the mini-FAT (" +
                                            std::to_string(this->mini_fat.size()) + " entries)");
            }
            if(this->mini_owner[current] == id)
            {
                ThrowMalformed(kFormat, "stream mini-FAT chain has a loop at mini sector " +
                                            std::to_string(current));
            }
            if(this->mini_owner[current] != 0)
            {
                ThrowMalformed(kFormat, "mini sector " + std::to_string(current) +
                                            " is used by more than one stream");
            }
            this->mini_owner[current] = id;
            chain.push_back(current);
            current = this->mini_fat[current];
        }
        return chain;
    }

    // ── Tables ─────────────────────────────────────────────

    // Build the full FAT from DIFAT entries in the header + DIFAT chain
    void BuildFat()
    {
        const ByteSpan file(this->bytes, kFormat);
        const std::uint64_t count = this->SectorCount();
        const std::uint32_t sector_size = this->header.sector_size;

        // First 109 DIFAT entries are in the header at offset 76
        this->fat_sectors.clear();
        for(std::size_t i = 0; i < HEADER_DIFAT_ENTRIES; ++i)
        {
            const auto entry = file.Read<std::uint32_t>(76 + i * 4, kLittle, "DIFAT entry");
            if(entry == FREESECT || entry == ENDOFCHAIN)
            {
                break;
            }
            this->fat_sectors.push_back(entry);
        }

        // Follow DIFAT chain for additional FAT sector locations
        if(this->header.total_difat_sectors > 0 && this->header.first_difat_sector != ENDOFCHAIN)
        {
            std::vector<bool> seen(static_cast<std::size_t>(count), false);
            std::uint32_t difat_sector = this->header.first_difat_sector;
            for(std::uint32_t i = 0; i < this->header.total_difat_sectors; ++i)
            {
                if(difat_sector == ENDOFCHAIN || difat_sector == FREESECT)
                {
                    break;
                }
                const ByteSpan sector = this->Sector(difat_sector, sector_size, "DIFAT sector");
                if(seen[difat_sector])
                {
                    ThrowMalformed(kFormat, "DIFAT chain has a loop at sector " +
                                                std::to_string(difat_sector));
                }
                seen[difat_sector] = true;

                const std::size_t entries_per_sector = sector_size / 4 - 1; // last entry is next DIFAT
                for(std::size_t j = 0; j < entries_per_sector; ++j)
                {
                    const auto entry = sector.Read<std::uint32_t>(j * 4, kLittle, "DIFAT entry");
                    if(entry == FREESECT || entry == ENDOFCHAIN)
                    {
                        break;
                    }
                    this->fat_sectors.push_back(entry);
                }
                // Next DIFAT sector is at the end of this sector
                difat_sector = sector.Read<std::uint32_t>(sector_size - 4, kLittle, "next DIFAT sector");
            }
        }

        // Read FAT sectors; each is listed once, so the table cannot outgrow the file
        this->fat.clear();
        std::vector<bool> used(static_cast<std::size_t>(count), false);
        for(const auto &fat_sector_id : this->fat_sectors)
        {
            const ByteSpan sector = this->Sector(fat_sector_id, sector_size, "FAT sector");
            if(used[fat_sector_id])
            {
                ThrowMalformed(kFormat, "FAT sector " + std::to_string(fat_sector_id) +
                                            " is listed twice");
            }
            used[fat_sector_id] = true;

            const std::size_t entries_per_sector = sector_size / 4;
            for(std::size_t i = 0; i < entries_per_sector; ++i)
            {
                this->fat.push_back(sector.Read<std::uint32_t>(i * 4, kLittle, "FAT entry"));
            }
        }
    }

    // Build mini-FAT table
    void BuildMiniFat()
    {
        this->mini_fat.clear();
        if(this->header.first_mini_fat_sector == ENDOFCHAIN ||
           this->header.total_mini_fat_sectors == 0)
        {
            return;
        }

        const auto chain = this->FollowChain(this->header.first_mini_fat_sector, "mini-FAT");
        for(const auto &sector_id : chain)
        {
            const ByteSpan sector = this->Sector(sector_id, this->header.sector_size, "mini-FAT sector");
            const std::size_t entries_per_sector = this->header.sector_size / 4;
            for(std::size_t i = 0; i < entries_per_sector; ++i)
            {
                this->mini_fat.push_back(sector.Read<std::uint32_t>(i * 4, kLittle, "mini-FAT entry"));
            }
        }
    }

    // Parse all directory entries
    void ParseDirectory()
    {
        this->directory.clear();
        this->dir_chain = this->FollowChain(this->header.first_dir_sector, "directory");

        const std::size_t entries_per_sector = this->header.sector_size / DIR_ENTRY_SIZE;
        std::size_t idx = 0;

        for(const auto &sector_id : this->dir_chain)
        {
            const ByteSpan sector = this->Sector(sector_id, this->header.sector_size, "directory sector");
            for(std::size_t i = 0; i < entries_per_sector; ++i)
            {
                const std::uint64_t base = i * DIR_ENTRY_SIZE;

                CfbDirEntry entry{};
                entry.dir_index = idx++;

                // Name: UTF-16LE at offset 0, up to 64 bytes (32 chars)
                entry.name_size = sector.Read<std::uint16_t>(base + 64, kLittle, "directory entry name size");
                if(entry.name_size >= 2 && entry.name_size <= 64)
                {
                    const std::size_t name_chars = (entry.name_size / 2) - 1; // exclude null terminator
                    for(std::size_t c = 0; c < name_chars; ++c)
                    {
                        const char16_t ch = sector.Read<std::uint16_t>(base + c * 2, kLittle, "directory entry name");
                        entry.name.push_back(ch);
                    }
                }

                entry.type = sector.Read<std::uint8_t>(base + 66, kLittle, "directory entry type");
                entry.left_sibling = sector.Read<std::uint32_t>(base + 68, kLittle, "left sibling");
                entry.right_sibling = sector.Read<std::uint32_t>(base + 72, kLittle, "right sibling");
                entry.child = sector.Read<std::uint32_t>(base + 76, kLittle, "child");
                entry.start_sector = sector.Read<std::uint32_t>(base + 116, kLittle, "start sector");

                if(this->header.major_version == 4)
                {
                    entry.stream_size = sector.Read<std::uint64_t>(base + 120, kLittle, "stream size");
                }
                else
                {
                    // v3: only lower 32 bits
                    entry.stream_size = sector.Read<std::uint32_t>(base + 120, kLittle, "stream size");
                }

                this->directory.push_back(entry);
            }
        }
    }

    // Check that entry 0 is the root and walk the tree from it once, with an
    // explicit stack, recording the streams in tree order.
    void ValidateTree()
    {
        this->tree_streams.clear();
        if(this->directory.empty() || this->directory[0].type != DIR_TYPE_ROOT)
        {
            ThrowMalformed(kFormat, "directory entry 0 is not the root storage");
        }
        if(this->directory[0].child == NOSTREAM)
        {
            return;
        }

        struct Frame
        {
            std::uint32_t id;
            int stage;
        };

        std::vector<bool> visited(this->directory.size(), false);
        visited[0] = true;
        std::vector<Frame> stack;

        const auto push = [&](std::uint32_t id)
        {
            if(id == NOSTREAM)
            {
                return;
            }
            if(id >= this->directory.size())
            {
                ThrowMalformed(kFormat, "directory entry link " + std::to_string(id) +
                                            " is past the end of the directory (" +
                                            std::to_string(this->directory.size()) + " entries)");
            }
            if(visited[id])
            {
                ThrowMalformed(kFormat, "directory entry " + std::to_string(id) +
                                            " is reached twice from the root");
            }
            visited[id] = true;
            if(this->directory[id].type == DIR_TYPE_UNKNOWN)
            {
                ThrowMalformed(kFormat, "directory entry " + std::to_string(id) +
                                            " is unused but the tree reaches it");
            }
            stack.push_back({id, 0});
        };

        push(this->directory[0].child);
        while(!stack.empty())
        {
            const std::uint32_t id = stack.back().id;
            const int stage = stack.back().stage;
            const CfbDirEntry &entry = this->directory[id];

            // In-order traversal of the red-black tree
            if(stage == 0)
            {
                stack.back().stage = 1;
                push(entry.left_sibling);
            }
            else if(stage == 1)
            {
                stack.back().stage = 2;
                if(entry.type == DIR_TYPE_STREAM)
                {
                    this->tree_streams.push_back(id);
                }
                else if(entry.type == DIR_TYPE_STORAGE)
                {
                    // Recurse into storage's children
                    push(entry.child);
                }
            }
            else
            {
                stack.pop_back();
                push(entry.right_sibling);
            }
        }
    }

    // ── Streams ────────────────────────────────────────────

    // A stream is read from the mini stream when it is small and its start
    // sector is actually within the mini-FAT.  WriteStream always allocates
    // regular sectors, so newly-written small streams will have a start_sector
    // that exceeds the mini-FAT range and are read from regular sectors.
    bool UsesMiniStream(const CfbDirEntry &entry) const
    {
        return entry.type != DIR_TYPE_ROOT &&
               entry.stream_size < this->header.mini_stream_cutoff &&
               !this->mini_fat.empty() &&
               entry.start_sector < static_cast<std::uint32_t>(this->mini_fat.size());
    }

    static std::uint64_t UnitsFor(std::uint64_t size, std::uint64_t unit)
    {
        return size / unit + ((size % unit) != 0 ? 1 : 0);
    }

    // Read a stream held in regular sectors. The whole chain is validated
    // before any memory is reserved.
    std::vector<std::uint8_t> ReadRegularStream(const CfbDirEntry &entry)
    {
        if(this->sector_owner.size() != this->SectorCount())
        {
            this->sector_owner.assign(static_cast<std::size_t>(this->SectorCount()), 0);
        }
        const std::uint32_t id = ++this->last_owner;
        const auto chain = this->WalkChain(entry.start_sector, this->sector_owner, id, "stream");

        const std::uint64_t sector_size = this->header.sector_size;
        const std::uint64_t needed = this->UnitsFor(entry.stream_size, sector_size);
        if(chain.size() < needed)
        {
            ThrowMalformed(kFormat, "stream size " + std::to_string(entry.stream_size) +
                                        " exceeds its chain of " + std::to_string(chain.size()) +
                                        " sectors");
        }

        // A partial last sector is fine when the bytes that are needed are present.
        std::uint64_t remaining = entry.stream_size;
        for(std::uint64_t i = 0; i < needed; ++i)
        {
            const std::uint64_t chunk = std::min(sector_size, remaining);
            if(!RangeFits(this->SectorStart(chain[i]), chunk, this->bytes.size()))
            {
                ThrowMalformed(kFormat, "stream sector " + std::to_string(chain[i]) +
                                            " extends past the end of the file (" +
                                            std::to_string(this->bytes.size()) + " bytes)");
            }
            remaining -= chunk;
        }

        std::vector<std::uint8_t> data;
        data.reserve(static_cast<std::size_t>(entry.stream_size));
        const ByteSpan file(this->bytes, kFormat);
        remaining = entry.stream_size;
        for(std::uint64_t i = 0; i < needed; ++i)
        {
            const std::uint64_t chunk = std::min(sector_size, remaining);
            const ByteSpan part = file.Sub(this->SectorStart(chain[i]), chunk, "stream sector");
            data.insert(data.end(), part.Data(), part.Data() + chunk);
            remaining -= chunk;
        }
        return data;
    }

    // Build the mini stream (the root's stream) once.
    void EnsureMiniStream()
    {
        if(this->mini_stream_ready)
        {
            return;
        }
        const CfbDirEntry root = this->directory[0];
        if(root.stream_size != 0)
        {
            this->mini_stream = this->ReadRegularStream(root);
        }
        this->mini_stream_ready = true;
    }

    std::vector<std::uint8_t> ReadMiniStream(const CfbDirEntry &entry)
    {
        this->EnsureMiniStream();
        const std::uint32_t id = ++this->last_owner;
        const auto chain = this->WalkMiniChain(entry.start_sector, id);

        const std::uint64_t unit = this->header.mini_sector_size;
        const std::uint64_t needed = this->UnitsFor(entry.stream_size, unit);
        if(chain.size() < needed)
        {
            ThrowMalformed(kFormat, "stream size " + std::to_string(entry.stream_size) +
                                        " exceeds its chain of " + std::to_string(chain.size()) +
                                        " mini sectors");
        }

        std::vector<std::uint8_t> data;
        data.reserve(static_cast<std::size_t>(entry.stream_size));
        const ByteSpan mini(this->mini_stream, kFormat, "the mini stream");
        std::uint64_t remaining = entry.stream_size;
        for(std::uint64_t i = 0; i < needed; ++i)
        {
            const std::uint64_t chunk = std::min(unit, remaining);
            const ByteSpan part = mini.Sub(static_cast<std::uint64_t>(chain[i]) * unit, chunk,
                                           "stream mini sector");
            data.insert(data.end(), part.Data(), part.Data() + chunk);
            remaining -= chunk;
        }
        return data;
    }

    // Read a stream's data given its directory entry
    std::vector<std::uint8_t> ReadStream(const CfbDirEntry &entry)
    {
        if(entry.stream_size == 0)
        {
            return {};
        }
        if(this->UsesMiniStream(entry))
        {
            return this->ReadMiniStream(entry);
        }
        return this->ReadRegularStream(entry);
    }

    // The presence check does not read the stream; it only makes sure that
    // the storage the stream would be read from holds its start sector.
    void CheckStreamStart(const CfbDirEntry &entry) const
    {
        if(entry.stream_size == 0 || this->UsesMiniStream(entry))
        {
            return;
        }
        const std::uint64_t count = this->SectorCount();
        if(entry.start_sector >= count)
        {
            ThrowMalformed(kFormat, "stream start sector " + std::to_string(entry.start_sector) +
                                        " is past the end of the file (" +
                                        std::to_string(count) + " sectors)");
        }
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
        for(const auto &entry : this->directory)
        {
            if(entry.name == name && entry.type == DIR_TYPE_STREAM)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    // ── Writing ────────────────────────────────────────────

    // Allocate a new sector: reuse a free one inside the file, or extend the
    // file by exactly one sector.
    std::uint32_t AllocateSector()
    {
        const std::uint64_t count = this->SectorCount();
        const std::uint64_t limit = std::min<std::uint64_t>(count, this->fat.size());

        while(this->free_cursor < limit)
        {
            const std::size_t index = this->free_cursor++;
            if(this->fat[index] == FREESECT)
            {
                this->fat[index] = ENDOFCHAIN;
                this->EnsureSectorBytes(static_cast<std::uint32_t>(index));
                return static_cast<std::uint32_t>(index);
            }
        }

        // No free sector — extend the file by one sector
        if(count >= FATSECT)
        {
            ThrowMalformed(kFormat, "file has no room for another sector");
        }
        const auto new_id = static_cast<std::uint32_t>(count);
        if(new_id < this->fat.size())
        {
            if(this->fat[new_id] != FREESECT)
            {
                ThrowMalformed(kFormat, "FAT entry of the sector after the end of the file (" +
                                            std::to_string(new_id) + ") is in use");
            }
            this->fat[new_id] = ENDOFCHAIN;
        }
        else
        {
            while(this->fat.size() < new_id)
            {
                this->fat.push_back(FREESECT);
            }
            this->fat.push_back(ENDOFCHAIN);
        }
        this->free_cursor = std::max<std::size_t>(this->free_cursor, new_id + 1);

        this->EnsureSectorBytes(new_id);
        return new_id;
    }

    // Write FAT back to file
    void WriteFat()
    {
        // Calculate how many entries fit per FAT sector
        const std::size_t entries_per_sector = this->header.sector_size / 4;

        // Ensure we have enough FAT sectors
        while(this->fat_sectors.size() * entries_per_sector < this->fat.size())
        {
            // Need to allocate a new FAT sector
            // This is tricky — the new FAT sector itself needs a FAT entry
            // For simplicity, extend the file and add the sector to DIFAT
            const std::uint64_t count = this->SectorCount();
            if(count >= FATSECT)
            {
                ThrowMalformed(kFormat, "file has no room for another FAT sector");
            }
            const auto new_fat_sector = static_cast<std::uint32_t>(count);
            this->EnsureSectorBytes(new_fat_sector);
            this->fat_sectors.push_back(new_fat_sector);

            // Extend FAT to cover the new sector
            while(this->fat.size() <= new_fat_sector)
            {
                this->fat.push_back(FREESECT);
            }
            if(this->fat[new_fat_sector] != FREESECT)
            {
                ThrowMalformed(kFormat, "FAT entry of the new FAT sector " +
                                            std::to_string(new_fat_sector) + " is in use");
            }
            this->fat[new_fat_sector] = FATSECT;

            // Update header
            const MutableByteSpan header_out(this->bytes, kFormat);
            this->header.total_fat_sectors = static_cast<std::uint32_t>(this->fat_sectors.size());
            header_out.Write<std::uint32_t>(44, this->header.total_fat_sectors, kLittle, "FAT sector count");

            // Write DIFAT entry in header
            if(this->fat_sectors.size() <= HEADER_DIFAT_ENTRIES)
            {
                header_out.Write<std::uint32_t>(76 + (this->fat_sectors.size() - 1) * 4,
                                                new_fat_sector, kLittle, "DIFAT entry");
            }
        }

        // Write FAT entries into FAT sectors
        const MutableByteSpan out(this->bytes, kFormat);
        std::size_t fat_idx = 0;
        for(const auto &fat_sector_id : this->fat_sectors)
        {
            const std::uint64_t offset = this->SectorStart(fat_sector_id);
            for(std::size_t i = 0; i < entries_per_sector && fat_idx < this->fat.size(); ++i)
            {
                out.Write<std::uint32_t>(offset + i * 4, this->fat[fat_idx++], kLittle, "FAT entry");
            }
            // Zero remaining entries in this sector (only if we stopped mid-sector)
            const std::size_t remainder_start = fat_idx % entries_per_sector;
            if(fat_idx >= this->fat.size() && remainder_start > 0)
            {
                for(std::size_t i = remainder_start; i < entries_per_sector; ++i)
                {
                    out.Write<std::uint32_t>(offset + i * 4, FREESECT, kLittle, "FAT entry");
                }
            }
        }
    }

    // File offset of a directory entry
    std::uint64_t DirEntryOffset(std::size_t entry_index) const
    {
        const std::size_t entries_per_sector = this->header.sector_size / DIR_ENTRY_SIZE;
        const std::size_t sector_idx = entry_index / entries_per_sector;
        const std::uint64_t offset_in_sector = (entry_index % entries_per_sector) * DIR_ENTRY_SIZE;

        if(sector_idx >= this->dir_chain.size())
        {
            ThrowMalformed(kFormat, "directory entry index " + std::to_string(entry_index) +
                                        " is past the end of the directory");
        }
        return this->SectorStart(this->dir_chain[sector_idx]) + offset_in_sector;
    }

    // Write a directory entry back to file
    void WriteDirEntry(std::size_t entry_index, const CfbDirEntry &entry)
    {
        const std::uint64_t file_offset = this->DirEntryOffset(entry_index);
        const MutableByteSpan out(this->bytes, kFormat);

        // Write name (UTF-16LE, null-padded to 64 bytes)
        out.Fill(file_offset, 64, 0, "directory entry name");
        for(std::size_t i = 0; i < entry.name.size() && i < 31; ++i)
        {
            out.Write<std::uint16_t>(file_offset + i * 2, entry.name[i], kLittle, "directory entry name");
        }
        // Null terminator
        if(entry.name.size() < 32)
        {
            out.Write<std::uint16_t>(file_offset + entry.name.size() * 2, 0, kLittle, "directory entry name");
        }

        // Name size (including null terminator, in bytes)
        out.Write<std::uint16_t>(file_offset + 64,
            static_cast<std::uint16_t>((entry.name.size() + 1) * 2), kLittle, "directory entry name size");

        // Type
        out.Write<std::uint8_t>(file_offset + 66, entry.type, kLittle, "directory entry type");

        // Siblings and child
        out.Write<std::uint32_t>(file_offset + 68, entry.left_sibling, kLittle, "left sibling");
        out.Write<std::uint32_t>(file_offset + 72, entry.right_sibling, kLittle, "right sibling");
        out.Write<std::uint32_t>(file_offset + 76, entry.child, kLittle, "child");

        // Start sector
        out.Write<std::uint32_t>(file_offset + 116, entry.start_sector, kLittle, "start sector");

        // Stream size
        if(this->header.major_version == 4)
        {
            out.Write<std::uint64_t>(file_offset + 120, entry.stream_size, kLittle, "stream size");
        }
        else
        {
            out.Write<std::uint32_t>(file_offset + 120,
                static_cast<std::uint32_t>(entry.stream_size), kLittle, "stream size");
        }
    }

    // Zero out a directory entry in the raw file bytes
    void ZeroDirEntry(std::size_t entry_index)
    {
        const std::uint64_t file_offset = this->DirEntryOffset(entry_index);
        const MutableByteSpan out(this->bytes, kFormat);
        out.Fill(file_offset, DIR_ENTRY_SIZE, 0, "directory entry");
    }

    // Mark the sectors of a regular chain as free. A signature that another
    // tool stored in the mini stream has a mini sector number here, which is
    // not a chain in the FAT, so the walk ends quietly at a link that leads
    // nowhere, as it always has; it still stops at a loop.
    void FreeChain(std::uint32_t start_sector)
    {
        std::vector<bool> seen(this->fat.size(), false);
        std::vector<std::uint32_t> chain;
        std::uint32_t current = start_sector;
        while(current != ENDOFCHAIN && current != FREESECT && current < this->fat.size())
        {
            if(seen[current])
            {
                ThrowMalformed(kFormat, "stream chain has a loop at sector " +
                                            std::to_string(current));
            }
            seen[current] = true;
            chain.push_back(current);
            current = this->fat[current];
        }

        for(const auto &sid : chain)
        {
            this->fat[sid] = FREESECT;
            this->free_cursor = std::min<std::size_t>(this->free_cursor, sid);
        }
    }

    // Write stream data to a directory entry, allocating sectors as needed
    void WriteStream(std::size_t entry_index,
                     const std::vector<std::uint8_t> &data)
    {
        auto &entry = this->directory[entry_index];

        // Free existing sector chain if any
        if(entry.stream_size > 0 && entry.start_sector != ENDOFCHAIN &&
           entry.start_sector != FREESECT)
        {
            // Always free via regular FAT chain because WriteStream
            // always writes to regular sectors (not mini-stream).
            this->FreeChain(entry.start_sector);
        }

        if(data.empty())
        {
            entry.start_sector = ENDOFCHAIN;
            entry.stream_size = 0;
            this->WriteDirEntry(entry_index, entry);
            return;
        }

        // For MSI signature streams, always use regular sectors
        // (signature data is typically > 4096 bytes)
        const std::size_t sector_size = this->header.sector_size;
        const std::size_t sectors_needed = (data.size() + sector_size - 1) / sector_size;

        std::vector<std::uint32_t> new_chain;
        for(std::size_t i = 0; i < sectors_needed; ++i)
        {
            new_chain.push_back(this->AllocateSector());
        }

        // Link the chain
        for(std::size_t i = 0; i < new_chain.size() - 1; ++i)
        {
            this->fat[new_chain[i]] = new_chain[i + 1];
        }
        this->fat[new_chain.back()] = ENDOFCHAIN;

        // Write data to sectors, zero-padding the last one
        const MutableByteSpan out(this->bytes, kFormat);
        std::size_t data_offset = 0;
        for(const auto &sid : new_chain)
        {
            const std::size_t to_write = std::min(sector_size, data.size() - data_offset);
            std::vector<std::uint8_t> sector_data(sector_size, 0);
            std::copy(data.begin() + static_cast<std::ptrdiff_t>(data_offset),
                      data.begin() + static_cast<std::ptrdiff_t>(data_offset + to_write),
                      sector_data.begin());
            out.Copy(this->SectorStart(sid), sector_data, "stream sector");
            data_offset += to_write;
        }

        // Update entry
        entry.start_sector = new_chain[0];
        entry.stream_size = data.size();
        this->WriteDirEntry(entry_index, entry);
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
        std::size_t new_index = this->directory.size();

        // Check for an unused slot
        for(std::size_t i = 1; i < this->directory.size(); ++i)
        {
            if(this->directory[i].type == DIR_TYPE_UNKNOWN)
            {
                new_index = i;
                break;
            }
        }

        if(new_index == this->directory.size())
        {
            // Need to extend directory
            const std::size_t entries_per_sector = this->header.sector_size / DIR_ENTRY_SIZE;
            const std::size_t current_capacity = this->dir_chain.size() * entries_per_sector;

            if(new_index >= current_capacity)
            {
                // Allocate a new directory sector
                const auto new_sector = this->AllocateSector();

                // Link it to the chain
                if(!this->dir_chain.empty())
                {
                    this->fat[this->dir_chain.back()] = new_sector;
                }
                this->fat[new_sector] = ENDOFCHAIN;
                this->dir_chain.push_back(new_sector);

                // Zero the new sector
                const MutableByteSpan out(this->bytes, kFormat);
                out.Fill(this->SectorStart(new_sector), this->header.sector_size, 0, "directory sector");
            }

            this->directory.push_back(new_entry);
        }

        new_entry.dir_index = new_index;
        this->directory[new_index] = new_entry;

        // Insert into the root's child tree
        // Simple approach: add as right-most sibling
        auto &root = this->directory[0];
        if(root.child == NOSTREAM)
        {
            root.child = static_cast<std::uint32_t>(new_index);
            this->WriteDirEntry(0, root);
        }
        else
        {
            // Walk to the right-most sibling
            std::uint32_t current = root.child;
            std::size_t steps = 0;
            while(this->directory[current].right_sibling != NOSTREAM &&
                  this->directory[current].right_sibling < this->directory.size())
            {
                if(++steps > this->directory.size())
                {
                    ThrowMalformed(kFormat, "right sibling chain of the root's child has a loop");
                }
                current = this->directory[current].right_sibling;
            }
            this->directory[current].right_sibling = static_cast<std::uint32_t>(new_index);
            this->WriteDirEntry(current, this->directory[current]);
        }

        this->WriteDirEntry(new_index, this->directory[new_index]);
        return new_index;
    }

    // Remove a directory entry (mark as unused, free its sectors)
    void RemoveEntry(std::size_t entry_index)
    {
        if(entry_index >= this->directory.size())
        {
            return;
        }

        auto &entry = this->directory[entry_index];

        // Free the sector chain
        if(entry.stream_size > 0 && entry.start_sector != ENDOFCHAIN &&
           entry.start_sector != FREESECT)
        {
            if(entry.stream_size >= this->header.mini_stream_cutoff ||
               entry.type == DIR_TYPE_ROOT)
            {
                this->FreeChain(entry.start_sector);
            }
            else
            {
                // Mini-stream: free the mini-FAT chain. A link that leads
                // nowhere ends the walk quietly; a loop is rejected.
                std::vector<bool> seen(this->mini_fat.size(), false);
                std::vector<std::uint32_t> chain;
                std::uint32_t mini_sector = entry.start_sector;
                while(mini_sector != ENDOFCHAIN && mini_sector != FREESECT &&
                      mini_sector < static_cast<std::uint32_t>(this->mini_fat.size()))
                {
                    if(seen[mini_sector])
                    {
                        ThrowMalformed(kFormat, "mini stream chain has a loop at mini sector " +
                                                    std::to_string(mini_sector));
                    }
                    seen[mini_sector] = true;
                    chain.push_back(mini_sector);
                    mini_sector = this->mini_fat[mini_sector];
                }
                for(const auto sid : chain)
                {
                    this->mini_fat[sid] = FREESECT;
                }
            }
        }

        // Unlink from the sibling tree
        // Find parent reference
        auto &root = this->directory[0];
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
                    std::size_t steps = 0;
                    while(this->directory[leftmost].left_sibling != NOSTREAM &&
                          this->directory[leftmost].left_sibling < this->directory.size())
                    {
                        if(++steps > this->directory.size())
                        {
                            ThrowMalformed(kFormat, "left sibling chain of a directory entry has a loop");
                        }
                        leftmost = this->directory[leftmost].left_sibling;
                    }
                    this->directory[leftmost].left_sibling = entry.left_sibling;
                    this->WriteDirEntry(leftmost, this->directory[leftmost]);
                }
            }
            else
            {
                root.child = entry.left_sibling;
            }
            this->WriteDirEntry(0, root);
        }
        else
        {
            // Find the entry that references this one
            for(auto &other : this->directory)
            {
                if(other.left_sibling == entry_index)
                {
                    other.left_sibling = entry.right_sibling != NOSTREAM
                        ? entry.right_sibling : entry.left_sibling;
                    this->WriteDirEntry(other.dir_index, other);
                    break;
                }
                if(other.right_sibling == entry_index)
                {
                    other.right_sibling = entry.right_sibling != NOSTREAM
                        ? entry.right_sibling : entry.left_sibling;
                    this->WriteDirEntry(other.dir_index, other);
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
        this->ZeroDirEntry(entry_index);
    }

    // Full parse
    void Parse()
    {
        this->ParseHeader();
        this->BuildFat();
        this->BuildMiniFat();
        this->ParseDirectory();
        this->ValidateTree();
    }
};

} // anonymous namespace

// ── MsiSigner Public Methods ───────────────────────────────

bool MsiSigner::IsMsi(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
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
    });
}

MsiSigner::DigestResult MsiSigner::ComputeAuthenticodeDigest(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
        CfbDocument doc;
        doc.bytes = ReadFileBytes(file_path);
        doc.Parse();

        // Collect all non-signature streams from the root's children
        std::vector<const CfbDirEntry*> streams;
        for(const std::size_t index : doc.tree_streams)
        {
            if(!CfbDocument::IsSignatureStream(doc.directory[index].name))
            {
                streams.push_back(&doc.directory[index]);
            }
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
    });
}

void MsiSigner::EmbedSignature(
    const std::string &file_path,
    const std::vector<std::uint8_t> &pkcs7_der)
{
    GuardEntryPoint(kFormat, [&] {
        CfbDocument doc;
        doc.bytes = ReadFileBytes(file_path);
        doc.Parse();

        // Nothing is written until the signature is known to fit
        CheckMsiSignatureSize(pkcs7_der.size(), doc.header.major_version);

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

        // Replace the file as one step
        WriteFileBytes(file_path, doc.bytes);
    });
}

std::optional<std::vector<std::uint8_t>> MsiSigner::ExtractSignature(
    const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
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
            return std::optional<std::vector<std::uint8_t>>();
        }

        return std::optional<std::vector<std::uint8_t>>(doc.ReadStream(*entry));
    });
}

bool MsiSigner::HasEmbeddedSignature(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&] {
        CfbDocument doc;
        doc.bytes = ReadFileBytes(file_path);
        doc.Parse();

        static const std::u16string sig_name = {
            0x0005, u'D', u'i', u'g', u'i', u't', u'a', u'l',
            u'S', u'i', u'g', u'n', u'a', u't', u'u', u'r', u'e'
        };

        const auto *entry = doc.FindEntry(sig_name);
        if(entry == nullptr || entry->stream_size == 0)
        {
            return false;
        }

        // The container is valid; the signature's contents are not read.
        doc.CheckStreamStart(*entry);
        return true;
    });
}

void MsiSigner::StripSignature(const std::string &file_path)
{
    GuardEntryPoint(kFormat, [&] {
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
    });
}
