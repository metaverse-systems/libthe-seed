#include "CfbReader.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace seed::internal
{

namespace
{

constexpr const char *kFormat = "MSI";

// Compound files are always little-endian.
constexpr ByteOrder kLittle = ByteOrder::Little;

constexpr std::uint8_t kMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

constexpr std::uint8_t kTypeUnknown = 0;
constexpr std::uint8_t kTypeStorage = 1;
constexpr std::uint8_t kTypeStream = 2;
constexpr std::uint8_t kTypeRoot = 5;

constexpr std::uint16_t kSectorExponentV3 = 9;
constexpr std::uint16_t kSectorExponentV4 = 12;
constexpr std::uint16_t kMiniSectorExponent = 6;
constexpr std::uint32_t kMiniStreamCutoff = 4096;
constexpr std::size_t kHeaderDifatEntries = 109;
constexpr std::uint64_t kDirEntrySize = 128;

std::uint64_t UnitsFor(std::uint64_t size, std::uint64_t unit)
{
    return size / unit + ((size % unit) != 0 ? 1 : 0);
}

// Appends a range, joining it to the previous one when they touch.
void AppendExtent(ByteExtents &extents, std::uint64_t offset, std::uint64_t length)
{
    if(!extents.empty() && extents.back().offset + extents.back().length == offset)
    {
        extents.back().length += length;
        return;
    }
    extents.push_back({offset, length});
}

char16_t UpperCase(char16_t unit)
{
    return (unit >= u'a' && unit <= u'z') ? static_cast<char16_t>(unit - 32) : unit;
}

} // namespace

// ── Geometry ───────────────────────────────────────────────

std::uint64_t PackageModel::HeaderSize() const
{
    return (this->header.major_version == 4) ? 4096 : 512;
}

// Number of sectors after the header; a partial last sector counts.
std::uint64_t PackageModel::SectorCount() const
{
    const std::uint64_t header_size = this->HeaderSize();
    if(this->data_size <= header_size)
    {
        return 0;
    }
    const std::uint64_t sector_size = this->header.sector_size;
    return (this->data_size - header_size + sector_size - 1) / sector_size;
}

std::uint64_t PackageModel::SectorStart(std::uint32_t sector_id) const
{
    return this->HeaderSize() + static_cast<std::uint64_t>(sector_id) * this->header.sector_size;
}

// The first `length` bytes of a sector, which must lie inside the file.
ByteSpan PackageModel::Sector(std::uint32_t sector_id, std::uint64_t length,
                              std::string_view what) const
{
    const std::uint64_t count = this->SectorCount();
    if(sector_id >= count)
    {
        ThrowMalformed(kFormat, std::string(what) + " " + std::to_string(sector_id) +
                                    " is past the end of the file (" + std::to_string(count) +
                                    " sectors)");
    }
    const ByteSpan file(this->data, this->data_size, kFormat);
    return file.Sub(this->SectorStart(sector_id), length, what);
}

// ── Chains ─────────────────────────────────────────────────

// Follows a chain from a starting sector, collecting all sector numbers.
// `owner` records which walk visited each sector (`id` is never 0): a second
// visit by the same walk is a loop, a visit by another walk means the sector
// belongs to two streams.
std::vector<std::uint32_t> PackageModel::WalkChain(std::uint32_t start_sector,
                                                   std::vector<std::uint32_t> &owner,
                                                   std::uint32_t id, std::string_view what) const
{
    std::vector<std::uint32_t> chain;
    const std::uint64_t count = this->SectorCount();
    std::uint32_t current = start_sector;

    while(current != kCfbEndOfChain)
    {
        if(current >= this->fat.size())
        {
            ThrowMalformed(kFormat, std::string(what) + " chain sector " +
                                        std::to_string(current) + " is past the end of the FAT (" +
                                        std::to_string(this->fat.size()) + " entries)");
        }
        if(current >= count)
        {
            ThrowMalformed(kFormat, std::string(what) + " chain sector " +
                                        std::to_string(current) +
                                        " is past the end of the file (" + std::to_string(count) +
                                        " sectors)");
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
std::vector<std::uint32_t> PackageModel::FollowChain(std::uint32_t start_sector,
                                                     std::string_view what) const
{
    std::vector<std::uint32_t> owner(static_cast<std::size_t>(this->SectorCount()), 0);
    return this->WalkChain(start_sector, owner, 1, what);
}

// Same walk through the mini-FAT.
std::vector<std::uint32_t> PackageModel::WalkMiniChain(std::uint32_t start_sector, std::uint32_t id)
{
    if(this->mini_owner.size() != this->mini_fat.size())
    {
        this->mini_owner.assign(this->mini_fat.size(), 0);
    }

    std::vector<std::uint32_t> chain;
    std::uint32_t current = start_sector;
    while(current != kCfbEndOfChain)
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

// ── Streams ────────────────────────────────────────────────

// The chain must cover the recorded size and only the size.
void PackageModel::CheckChainLength(std::uint64_t size, std::uint64_t chain_length,
                                    std::uint64_t unit, const char *unit_name) const
{
    const std::uint64_t needed = UnitsFor(size, unit);
    if(chain_length < needed)
    {
        ThrowMalformed(kFormat, "stream size " + std::to_string(size) + " exceeds its chain of " +
                                    std::to_string(chain_length) + " " + unit_name);
    }
    if(chain_length > needed)
    {
        ThrowMalformed(kFormat, "stream size " + std::to_string(size) +
                                    " disagrees with its chain of " +
                                    std::to_string(chain_length) + " " + unit_name +
                                    " (it needs " + std::to_string(needed) + ")");
    }
}

ByteExtents PackageModel::ResolveOrdinary(const CfbNode &stream)
{
    if(this->sector_owner.size() != this->SectorCount())
    {
        this->sector_owner.assign(static_cast<std::size_t>(this->SectorCount()), 0);
    }
    const std::uint32_t id = ++this->last_owner;
    const auto chain = this->WalkChain(stream.start_sector, this->sector_owner, id, "stream");
    this->CheckChainLength(stream.size, chain.size(), this->header.sector_size, "sectors");

    // A partial last sector is fine when the bytes that are needed are present.
    const std::uint64_t sector_size = this->header.sector_size;
    ByteExtents extents;
    std::uint64_t remaining = stream.size;
    for(const std::uint32_t sector : chain)
    {
        const std::uint64_t chunk = std::min(sector_size, remaining);
        if(!RangeFits(this->SectorStart(sector), chunk, this->data_size))
        {
            ThrowMalformed(kFormat, "stream sector " + std::to_string(sector) +
                                        " extends past the end of the file (" +
                                        std::to_string(this->data_size) + " bytes)");
        }
        AppendExtent(extents, this->SectorStart(sector), chunk);
        remaining -= chunk;
    }
    return extents;
}

// The mini stream is the root's stream, held in ordinary sectors. Its chain
// must cover its size; slack after the size is tolerated.
void PackageModel::EnsureMiniStream()
{
    if(this->mini_container_ready)
    {
        return;
    }
    if(this->mini_container_size != 0)
    {
        if(this->sector_owner.size() != this->SectorCount())
        {
            this->sector_owner.assign(static_cast<std::size_t>(this->SectorCount()), 0);
        }
        const std::uint32_t id = ++this->last_owner;
        auto chain = this->WalkChain(this->mini_container_start, this->sector_owner, id, "stream");
        const std::uint64_t sector_size = this->header.sector_size;
        const std::uint64_t needed = UnitsFor(this->mini_container_size, sector_size);
        if(chain.size() < needed)
        {
            ThrowMalformed(kFormat, "stream size " + std::to_string(this->mini_container_size) +
                                        " exceeds its chain of " + std::to_string(chain.size()) +
                                        " sectors");
        }
        std::uint64_t remaining = this->mini_container_size;
        for(std::uint64_t i = 0; i < needed; ++i)
        {
            const std::uint64_t chunk = std::min(sector_size, remaining);
            if(!RangeFits(this->SectorStart(chain[i]), chunk, this->data_size))
            {
                ThrowMalformed(kFormat, "stream sector " + std::to_string(chain[i]) +
                                            " extends past the end of the file (" +
                                            std::to_string(this->data_size) + " bytes)");
            }
            remaining -= chunk;
        }
        this->mini_container_chain = std::move(chain);
    }
    this->mini_container_ready = true;
}

ByteExtents PackageModel::ResolveMini(const CfbNode &stream)
{
    this->EnsureMiniStream();
    const std::uint32_t id = ++this->last_owner;
    const auto chain = this->WalkMiniChain(stream.start_sector, id);
    const std::uint64_t unit = this->header.mini_sector_size;
    this->CheckChainLength(stream.size, chain.size(), unit, "mini sectors");

    const std::uint64_t sector_size = this->header.sector_size;
    ByteExtents extents;
    std::uint64_t remaining = stream.size;
    for(const std::uint32_t mini_sector : chain)
    {
        const std::uint64_t chunk = std::min(unit, remaining);
        const std::uint64_t position = static_cast<std::uint64_t>(mini_sector) * unit;
        if(!RangeFits(position, chunk, this->mini_container_size))
        {
            ThrowMalformed(kFormat, "stream mini sector " + std::to_string(mini_sector) +
                                        " extends past the end of the mini stream (" +
                                        std::to_string(this->mini_container_size) + " bytes)");
        }
        const std::uint32_t sector = this->mini_container_chain[position / sector_size];
        AppendExtent(extents, this->SectorStart(sector) + position % sector_size, chunk);
        remaining -= chunk;
    }
    return extents;
}

ByteExtents PackageModel::Resolve(const CfbNode &stream)
{
    if(stream.is_storage || stream.size == 0)
    {
        return {};
    }
    if(stream.size < this->header.mini_stream_cutoff)
    {
        return this->ResolveMini(stream);
    }
    return this->ResolveOrdinary(stream);
}

std::vector<std::uint8_t> PackageModel::ReadStream(const CfbNode &stream)
{
    const ByteExtents extents = this->Resolve(stream);
    const ByteSpan file(this->data, this->data_size, kFormat);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(stream.size));
    for(const ByteExtent &extent : extents)
    {
        const ByteSpan part = file.Sub(extent.offset, extent.length, "stream data");
        bytes.insert(bytes.end(), part.Data(), part.Data() + extent.length);
    }
    return bytes;
}

// ── Search and enumeration ─────────────────────────────────

int PackageModel::CompareNames(const std::u16string &a, const std::u16string &b)
{
    if(a.size() != b.size())
    {
        return a.size() < b.size() ? -1 : 1;
    }
    for(std::size_t i = 0; i < a.size(); ++i)
    {
        const char16_t x = UpperCase(a[i]);
        const char16_t y = UpperCase(b[i]);
        if(x != y)
        {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

const CfbNode *PackageModel::FindByEnumeration(const CfbNode &storage, const std::u16string &name)
{
    for(const CfbNode &child : storage.children)
    {
        if(child.name == name)
        {
            return &child;
        }
    }
    return nullptr;
}

const CfbNode *PackageModel::FindBySearch(const CfbNode &storage, const std::u16string &name)
{
    std::uint32_t position = storage.tree_root;
    for(std::size_t steps = 0; position != CfbNode::kNone && steps <= storage.children.size();
        ++steps)
    {
        const CfbNode &node = storage.children[position];
        const int order = CompareNames(name, node.name);
        if(order == 0)
        {
            return &node;
        }
        position = order < 0 ? node.left : node.right;
    }
    return nullptr;
}

// ── Parsing ────────────────────────────────────────────────

struct CfbReader::DirEntry
{
    std::u16string name;
    std::uint8_t type = kTypeUnknown;
    std::uint32_t left = kCfbNoStream;
    std::uint32_t right = kCfbNoStream;
    std::uint32_t child = kCfbNoStream;
    std::uint32_t start_sector = 0;
    std::uint64_t size = 0;
    CfbClassId class_id{};
    std::uint32_t state_bits = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modification_time = 0;
};

struct CfbReader::TreeState
{
    const std::vector<DirEntry> &directory;
    std::vector<bool> visited;
};

void CfbReader::ParseHeader(PackageModel &model)
{
    if(model.data_size < 512)
    {
        ThrowMalformed(kFormat, "file is too small for the header (" +
                                    std::to_string(model.data_size) + " bytes)");
    }
    if(std::memcmp(model.data, kMagic, 8) != 0)
    {
        ThrowMalformed(kFormat, "signature is not the compound file magic");
    }

    const ByteSpan file(model.data, model.data_size, kFormat);
    CfbHeader &header = model.header;
    header.minor_version = file.Read<std::uint16_t>(24, kLittle, "minor version");
    header.major_version = file.Read<std::uint16_t>(26, kLittle, "major version");

    // 0xFFFE marks little-endian.
    const auto byte_order = file.Read<std::uint16_t>(28, kLittle, "byte order");
    if(byte_order != 0xFFFE)
    {
        ThrowMalformed(kFormat, "byte order mark " + std::to_string(byte_order) + " is not 65534");
    }

    const auto sector_power = file.Read<std::uint16_t>(30, kLittle, "sector size exponent");
    const auto mini_power = file.Read<std::uint16_t>(32, kLittle, "mini-sector exponent");

    if(sector_power != kSectorExponentV3 && sector_power != kSectorExponentV4)
    {
        ThrowMalformed(kFormat,
                       "sector size exponent " + std::to_string(sector_power) + " is not 9 or 12");
    }
    if(header.major_version != 3 && header.major_version != 4)
    {
        ThrowMalformed(kFormat,
                       "major version " + std::to_string(header.major_version) + " is not 3 or 4");
    }
    if((header.major_version == 3) != (sector_power == kSectorExponentV3))
    {
        ThrowMalformed(kFormat, "sector size exponent " + std::to_string(sector_power) +
                                    " does not match major version " +
                                    std::to_string(header.major_version));
    }
    if(mini_power != kMiniSectorExponent)
    {
        ThrowMalformed(kFormat, "mini-sector exponent " + std::to_string(mini_power) +
                                    " is not 6");
    }
    if(model.data_size < model.HeaderSize())
    {
        ThrowMalformed(kFormat, "file is too small for the version 4 header (" +
                                    std::to_string(model.data_size) + " bytes)");
    }

    header.sector_size = 1u << sector_power;
    header.mini_sector_size = 1u << mini_power;

    model.first_dir_sector = file.Read<std::uint32_t>(48, kLittle, "first directory sector");
    header.mini_stream_cutoff = file.Read<std::uint32_t>(56, kLittle, "mini-stream cutoff");
    model.first_mini_fat_sector = file.Read<std::uint32_t>(60, kLittle, "first mini-FAT sector");
    model.total_mini_fat_sectors = file.Read<std::uint32_t>(64, kLittle, "mini-FAT sector count");
    model.first_difat_sector = file.Read<std::uint32_t>(68, kLittle, "first DIFAT sector");
    model.total_difat_sectors = file.Read<std::uint32_t>(72, kLittle, "DIFAT sector count");

    if(header.mini_stream_cutoff != kMiniStreamCutoff)
    {
        ThrowMalformed(kFormat, "mini-stream cutoff " + std::to_string(header.mini_stream_cutoff) +
                                    " is not 4096");
    }
}

// Builds the full FAT from the DIFAT entries in the header and the DIFAT chain.
void CfbReader::BuildFat(PackageModel &model)
{
    const ByteSpan file(model.data, model.data_size, kFormat);
    const std::uint64_t count = model.SectorCount();
    const std::uint32_t sector_size = model.header.sector_size;

    // The first 109 entries are in the header at offset 76.
    std::vector<std::uint32_t> fat_sectors;
    for(std::size_t i = 0; i < kHeaderDifatEntries; ++i)
    {
        const auto entry = file.Read<std::uint32_t>(76 + i * 4, kLittle, "DIFAT entry");
        if(entry == kCfbFreeSector || entry == kCfbEndOfChain)
        {
            break;
        }
        fat_sectors.push_back(entry);
    }

    // The chain of extra index sectors lists the rest.
    if(model.total_difat_sectors > 0 && model.first_difat_sector != kCfbEndOfChain)
    {
        std::vector<bool> seen(static_cast<std::size_t>(count), false);
        std::uint32_t difat_sector = model.first_difat_sector;
        for(std::uint32_t i = 0; i < model.total_difat_sectors; ++i)
        {
            if(difat_sector == kCfbEndOfChain || difat_sector == kCfbFreeSector)
            {
                break;
            }
            const ByteSpan sector = model.Sector(difat_sector, sector_size, "DIFAT sector");
            if(seen[difat_sector])
            {
                ThrowMalformed(kFormat,
                               "DIFAT chain has a loop at sector " + std::to_string(difat_sector));
            }
            seen[difat_sector] = true;

            const std::size_t entries_per_sector = sector_size / 4 - 1; // the last one is the link
            for(std::size_t j = 0; j < entries_per_sector; ++j)
            {
                const auto entry = sector.Read<std::uint32_t>(j * 4, kLittle, "DIFAT entry");
                if(entry == kCfbFreeSector || entry == kCfbEndOfChain)
                {
                    break;
                }
                fat_sectors.push_back(entry);
            }
            difat_sector =
                sector.Read<std::uint32_t>(sector_size - 4, kLittle, "next DIFAT sector");
        }
    }

    // Each FAT sector is listed once, so the table cannot outgrow the file.
    model.fat.clear();
    std::vector<bool> used(static_cast<std::size_t>(count), false);
    for(const std::uint32_t fat_sector_id : fat_sectors)
    {
        const ByteSpan sector = model.Sector(fat_sector_id, sector_size, "FAT sector");
        if(used[fat_sector_id])
        {
            ThrowMalformed(kFormat,
                           "FAT sector " + std::to_string(fat_sector_id) + " is listed twice");
        }
        used[fat_sector_id] = true;

        const std::size_t entries_per_sector = sector_size / 4;
        for(std::size_t i = 0; i < entries_per_sector; ++i)
        {
            model.fat.push_back(sector.Read<std::uint32_t>(i * 4, kLittle, "FAT entry"));
        }
    }
}

void CfbReader::BuildMiniFat(PackageModel &model)
{
    model.mini_fat.clear();
    if(model.first_mini_fat_sector == kCfbEndOfChain || model.total_mini_fat_sectors == 0)
    {
        return;
    }

    const auto chain = model.FollowChain(model.first_mini_fat_sector, "mini-FAT");
    for(const std::uint32_t sector_id : chain)
    {
        const ByteSpan sector = model.Sector(sector_id, model.header.sector_size, "mini-FAT sector");
        const std::size_t entries_per_sector = model.header.sector_size / 4;
        for(std::size_t i = 0; i < entries_per_sector; ++i)
        {
            model.mini_fat.push_back(sector.Read<std::uint32_t>(i * 4, kLittle, "mini-FAT entry"));
        }
    }
}

std::vector<CfbReader::DirEntry> CfbReader::ParseDirectory(const PackageModel &model)
{
    std::vector<DirEntry> directory;
    const auto chain = model.FollowChain(model.first_dir_sector, "directory");
    const std::size_t entries_per_sector = model.header.sector_size / kDirEntrySize;

    for(const std::uint32_t sector_id : chain)
    {
        const ByteSpan sector = model.Sector(sector_id, model.header.sector_size, "directory sector");
        for(std::size_t i = 0; i < entries_per_sector; ++i)
        {
            const std::uint64_t base = i * kDirEntrySize;
            DirEntry entry;

            // The name is UTF-16LE in 64 bytes; the size counts the terminator.
            const auto name_size =
                sector.Read<std::uint16_t>(base + 64, kLittle, "directory entry name size");
            if(name_size >= 2 && name_size <= 64)
            {
                const std::size_t name_chars = (name_size / 2) - 1;
                for(std::size_t c = 0; c < name_chars; ++c)
                {
                    entry.name.push_back(static_cast<char16_t>(sector.Read<std::uint16_t>(
                        base + c * 2, kLittle, "directory entry name")));
                }
            }

            entry.type = sector.Read<std::uint8_t>(base + 66, kLittle, "directory entry type");
            entry.left = sector.Read<std::uint32_t>(base + 68, kLittle, "left sibling");
            entry.right = sector.Read<std::uint32_t>(base + 72, kLittle, "right sibling");
            entry.child = sector.Read<std::uint32_t>(base + 76, kLittle, "child");
            for(std::size_t b = 0; b < entry.class_id.size(); ++b)
            {
                entry.class_id[b] =
                    sector.Read<std::uint8_t>(base + 80 + b, kLittle, "class identifier");
            }
            entry.state_bits = sector.Read<std::uint32_t>(base + 96, kLittle, "state bits");
            entry.creation_time = sector.Read<std::uint64_t>(base + 100, kLittle, "creation time");
            entry.modification_time =
                sector.Read<std::uint64_t>(base + 108, kLittle, "modification time");
            entry.start_sector = sector.Read<std::uint32_t>(base + 116, kLittle, "start sector");
            if(model.header.major_version == 4)
            {
                entry.size = sector.Read<std::uint64_t>(base + 120, kLittle, "stream size");
            }
            else
            {
                // Version 3 keeps only the low 32 bits.
                entry.size = sector.Read<std::uint32_t>(base + 120, kLittle, "stream size");
            }

            directory.push_back(std::move(entry));
        }
    }
    return directory;
}

// Reads the children of one storage: the in-order sequence of its search tree,
// walked with an explicit stack, and the tree itself as positions.
std::vector<CfbNode> CfbReader::ReadStorage(TreeState &state, std::uint32_t storage_index,
                                            unsigned depth, std::uint32_t &tree_root)
{
    tree_root = CfbNode::kNone;
    if(depth > kCfbMaxStorageDepth)
    {
        ThrowMalformed(kFormat, "storages are nested too deep (more than " +
                                    std::to_string(kCfbMaxStorageDepth) + " levels)");
    }
    const std::vector<DirEntry> &directory = state.directory;
    if(directory[storage_index].child == kCfbNoStream)
    {
        return {};
    }

    struct Frame
    {
        std::uint32_t id;
        int stage;
    };

    std::vector<std::uint32_t> order;
    std::vector<Frame> stack;
    const auto push = [&](std::uint32_t id)
    {
        if(id == kCfbNoStream)
        {
            return;
        }
        if(id >= directory.size())
        {
            ThrowMalformed(kFormat, "directory entry link " + std::to_string(id) +
                                        " is past the end of the directory (" +
                                        std::to_string(directory.size()) + " entries)");
        }
        if(state.visited[id])
        {
            ThrowMalformed(kFormat, "directory entry " + std::to_string(id) +
                                        " is reached twice from the root");
        }
        state.visited[id] = true;
        if(directory[id].type == kTypeUnknown)
        {
            ThrowMalformed(kFormat, "directory entry " + std::to_string(id) +
                                        " is unused but the tree reaches it");
        }
        stack.push_back({id, 0});
    };

    push(directory[storage_index].child);
    while(!stack.empty())
    {
        const std::uint32_t id = stack.back().id;
        const int stage = stack.back().stage;
        const DirEntry &entry = directory[id];

        if(stage == 0)
        {
            stack.back().stage = 1;
            push(entry.left);
        }
        else if(stage == 1)
        {
            stack.back().stage = 2;
            order.push_back(id);
        }
        else
        {
            stack.pop_back();
            push(entry.right);
        }
    }

    // Position of each directory entry that becomes a node.
    std::unordered_map<std::uint32_t, std::uint32_t> position;
    std::vector<CfbNode> nodes;
    std::vector<std::uint32_t> source;
    for(const std::uint32_t id : order)
    {
        const DirEntry &entry = directory[id];
        if(entry.type != kTypeStorage && entry.type != kTypeStream)
        {
            continue;
        }
        CfbNode node;
        node.name = entry.name;
        node.is_storage = entry.type == kTypeStorage;
        node.class_id = entry.class_id;
        node.state_bits = entry.state_bits;
        node.creation_time = entry.creation_time;
        node.modification_time = entry.modification_time;
        if(node.is_storage)
        {
            node.children = ReadStorage(state, id, depth + 1, node.tree_root);
        }
        else
        {
            node.start_sector = entry.start_sector;
            node.size = entry.size;
        }
        position[id] = static_cast<std::uint32_t>(nodes.size());
        source.push_back(id);
        nodes.push_back(std::move(node));
    }

    const auto lookup = [&](std::uint32_t id) -> std::uint32_t
    {
        const auto found = position.find(id);
        return found == position.end() ? CfbNode::kNone : found->second;
    };
    for(std::size_t i = 0; i < nodes.size(); ++i)
    {
        nodes[i].left = lookup(directory[source[i]].left);
        nodes[i].right = lookup(directory[source[i]].right);
    }
    tree_root = lookup(directory[storage_index].child);
    return nodes;
}

void CfbReader::BuildTree(PackageModel &model, const std::vector<DirEntry> &directory)
{
    if(directory.empty() || directory[0].type != kTypeRoot)
    {
        ThrowMalformed(kFormat, "directory entry 0 is not the root storage");
    }

    const DirEntry &root = directory[0];
    model.root.name = root.name;
    model.root.is_storage = true;
    model.root.class_id = root.class_id;
    model.root.state_bits = root.state_bits;
    model.root.creation_time = root.creation_time;
    model.root.modification_time = root.modification_time;

    // The root's own stream is the mini stream container.
    model.mini_container_start = root.start_sector;
    model.mini_container_size = root.size;

    TreeState state{directory, std::vector<bool>(directory.size(), false)};
    state.visited[0] = true;
    model.root.children = ReadStorage(state, 0, 1, model.root.tree_root);
}

PackageModel CfbReader::Parse(const std::vector<std::uint8_t> &bytes)
{
    PackageModel model;
    model.data = bytes.data();
    model.data_size = bytes.size();

    ParseHeader(model);
    BuildFat(model);
    BuildMiniFat(model);
    const std::vector<DirEntry> directory = ParseDirectory(model);
    BuildTree(model, directory);
    return model;
}

} // namespace seed::internal
