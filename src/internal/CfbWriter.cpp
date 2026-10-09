#include "CfbWriter.hpp"

#include "MsiAuthenticode.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace seed::internal
{

namespace
{

constexpr const char *kFormat = "MSI";

constexpr std::uint8_t kMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

constexpr std::uint8_t kTypeUnused = 0;
constexpr std::uint8_t kTypeStorage = 1;
constexpr std::uint8_t kTypeStream = 2;
constexpr std::uint8_t kTypeRoot = 5;

constexpr std::uint8_t kColourBlack = 1;

constexpr std::uint32_t kFatSector = 0xFFFFFFFD;
constexpr std::uint32_t kDifatSector = 0xFFFFFFFC;

constexpr std::size_t kHeaderDifatEntries = 109;
constexpr std::uint64_t kDirEntrySize = 128;
constexpr std::uint64_t kMiniSectorSize = 64;

std::uint64_t UnitsFor(std::uint64_t size, std::uint64_t unit)
{
    return size / unit + ((size % unit) != 0 ? 1 : 0);
}

void PutLE16(std::uint8_t *at, std::uint16_t value)
{
    at[0] = static_cast<std::uint8_t>(value);
    at[1] = static_cast<std::uint8_t>(value >> 8);
}

void PutLE32(std::uint8_t *at, std::uint32_t value)
{
    for(unsigned i = 0; i < 4; ++i)
    {
        at[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void PutLE64(std::uint8_t *at, std::uint64_t value)
{
    for(unsigned i = 0; i < 8; ++i)
    {
        at[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

// A run of bytes that belongs to a stream: part of the source buffer, or the
// new signature.
struct Piece
{
    const std::uint8_t *data = nullptr;
    std::uint64_t length = 0;
};

// One directory entry of the output.
struct Entry
{
    const CfbNode *node = nullptr;
    std::vector<Piece> pieces;
    std::vector<std::uint32_t> children; // directory positions, in sequence order
    std::uint64_t size = 0;              // streams
    bool in_mini_stream = false;
    std::uint32_t start = kCfbEndOfChain; // sector, or mini sector for a mini stream
    std::uint32_t left = kCfbNoStream;
    std::uint32_t right = kCfbNoStream;
    std::uint32_t child = kCfbNoStream;
};

// Everything the layout decides.
struct Layout
{
    std::uint32_t sector_size = 0;
    std::uint64_t header_size = 0;
    std::uint64_t entries_per_index_sector = 0;

    std::uint64_t stream_sectors = 0;
    std::uint64_t mini_sectors = 0; // 64-byte mini sectors
    std::uint64_t mini_stream_start = 0;
    std::uint64_t mini_stream_sectors = 0;
    std::uint64_t mini_index_start = 0;
    std::uint64_t mini_index_sectors = 0;
    std::uint64_t directory_start = 0;
    std::uint64_t directory_sectors = 0;
    std::uint64_t difat_start = 0;
    std::uint64_t difat_sectors = 0;
    std::uint64_t fat_start = 0;
    std::uint64_t fat_sectors = 0;
    std::uint64_t total_sectors = 0;
};

// ── Tree of entries ────────────────────────────────────────

class Flattener
{
public:
    Flattener(PackageModel &model, const std::vector<std::uint8_t> *signature)
        : model(model), signature(signature)
    {
        this->signature_node.name = MsiSignatureName();
        if(signature != nullptr)
        {
            this->signature_node.size = signature->size();
        }
    }

    std::vector<Entry> Run()
    {
        this->entries.emplace_back();
        this->entries[0].node = &this->model.Root();

        // The root's signature streams are replaced, never copied.
        std::vector<const CfbNode *> sequence;
        for(const CfbNode &child : this->model.Root().children)
        {
            if(!IsMsiSignatureName(child.name))
            {
                sequence.push_back(&child);
            }
        }
        if(this->signature != nullptr)
        {
            const auto at = std::find_if(sequence.begin(), sequence.end(), [&](const CfbNode *node)
                                         { return PackageModel::CompareNames(
                                                      this->signature_node.name, node->name) < 0; });
            sequence.insert(at, &this->signature_node);
        }
        this->Expand(0, sequence);
        return std::move(this->entries);
    }

private:
    void CheckUniqueNames(const std::vector<const CfbNode *> &sequence) const
    {
        std::vector<const CfbNode *> sorted(sequence);
        std::sort(sorted.begin(), sorted.end(), [](const CfbNode *a, const CfbNode *b)
                  { return PackageModel::CompareNames(a->name, b->name) < 0; });
        for(std::size_t i = 1; i < sorted.size(); ++i)
        {
            if(PackageModel::CompareNames(sorted[i - 1]->name, sorted[i]->name) == 0)
            {
                ThrowMalformed(kFormat, "the package holds two entries named \"" +
                                            PrintableName(sorted[i]->name) +
                                            "\" in one storage; refusing to rebuild");
            }
        }
    }

    // Gives the entries of one storage consecutive positions, then does the
    // same for each of its storages.
    //
    // A storage that another writer left out of the format's order (a package
    // whose entries are in raw byte order, for example) is put into that order
    // here, so that the balanced tree built from it can find every entry. A
    // storage already in order is not touched. Names equal by the ordering are
    // refused first, so the result is a pure function of the model.
    void Expand(std::uint32_t parent, std::vector<const CfbNode *> sequence)
    {
        this->CheckUniqueNames(sequence);
        std::stable_sort(sequence.begin(), sequence.end(), [](const CfbNode *a, const CfbNode *b)
                         { return PackageModel::CompareNames(a->name, b->name) < 0; });

        std::vector<std::uint32_t> positions;
        positions.reserve(sequence.size());
        for(const CfbNode *node : sequence)
        {
            Entry entry;
            entry.node = node;
            if(!node->is_storage)
            {
                if(node == &this->signature_node)
                {
                    entry.size = this->signature->size();
                    entry.pieces.push_back({this->signature->data(), entry.size});
                }
                else
                {
                    entry.size = node->size;
                    for(const ByteExtent &extent : this->model.Resolve(*node))
                    {
                        entry.pieces.push_back({this->model.Source() + extent.offset, extent.length});
                    }
                }
            }
            positions.push_back(static_cast<std::uint32_t>(this->entries.size()));
            this->entries.push_back(std::move(entry));
        }
        this->entries[parent].children = positions;

        for(std::size_t i = 0; i < sequence.size(); ++i)
        {
            if(sequence[i]->is_storage)
            {
                std::vector<const CfbNode *> inner;
                inner.reserve(sequence[i]->children.size());
                for(const CfbNode &child : sequence[i]->children)
                {
                    inner.push_back(&child);
                }
                this->Expand(positions[i], inner);
            }
        }
    }

    PackageModel &model;
    const std::vector<std::uint8_t> *signature;
    CfbNode signature_node;
    std::vector<Entry> entries;
};

// The middle entry is the root of the tree, the halves are its subtrees.
std::uint32_t BuildTree(std::vector<Entry> &entries, const std::vector<std::uint32_t> &sequence,
                        std::size_t low, std::size_t high)
{
    if(low >= high)
    {
        return kCfbNoStream;
    }
    const std::size_t middle = (low + high) / 2;
    Entry &entry = entries[sequence[middle]];
    entry.left = BuildTree(entries, sequence, low, middle);
    entry.right = BuildTree(entries, sequence, middle + 1, high);
    return sequence[middle];
}

// ── Layout ─────────────────────────────────────────────────

Layout ComputeLayout(std::vector<Entry> &entries, const CfbHeader &header)
{
    Layout layout;
    layout.sector_size = header.sector_size;
    layout.header_size = header.major_version == 4 ? 4096 : 512;
    layout.entries_per_index_sector = header.sector_size / 4;
    const std::uint64_t sector_size = header.sector_size;

    std::uint64_t sector = 0;
    for(Entry &entry : entries)
    {
        if(entry.node->is_storage || entry.size == 0 || entry.size < header.mini_stream_cutoff)
        {
            continue;
        }
        entry.start = static_cast<std::uint32_t>(sector);
        sector += UnitsFor(entry.size, sector_size);
    }
    layout.stream_sectors = sector;

    std::uint64_t mini = 0;
    for(Entry &entry : entries)
    {
        if(entry.node->is_storage || entry.size == 0 || entry.size >= header.mini_stream_cutoff)
        {
            continue;
        }
        entry.in_mini_stream = true;
        entry.start = static_cast<std::uint32_t>(mini);
        mini += UnitsFor(entry.size, kMiniSectorSize);
    }
    layout.mini_sectors = mini;
    layout.mini_stream_start = layout.stream_sectors;
    layout.mini_stream_sectors = UnitsFor(mini * kMiniSectorSize, sector_size);
    layout.mini_index_start = layout.mini_stream_start + layout.mini_stream_sectors;
    layout.mini_index_sectors = UnitsFor(mini, layout.entries_per_index_sector);
    layout.directory_start = layout.mini_index_start + layout.mini_index_sectors;
    layout.directory_sectors = UnitsFor(entries.size(), sector_size / kDirEntrySize);
    layout.difat_start = layout.directory_start + layout.directory_sectors;

    // The allocation index must list itself and the extended index sectors,
    // and the extended index grows with it: the smallest fixed point.
    const std::uint64_t others = layout.difat_start;
    std::uint64_t fat = 0;
    std::uint64_t difat = 0;
    for(unsigned step = 0; step < 64; ++step)
    {
        const std::uint64_t listed = fat > kHeaderDifatEntries ? fat - kHeaderDifatEntries : 0;
        difat = UnitsFor(listed, layout.entries_per_index_sector - 1);
        const std::uint64_t next = UnitsFor(others + difat + fat, layout.entries_per_index_sector);
        if(next == fat)
        {
            break;
        }
        fat = next;
    }
    layout.difat_sectors = difat;
    layout.fat_start = layout.difat_start + difat;
    layout.fat_sectors = fat;
    layout.total_sectors = layout.fat_start + fat;

    if(layout.total_sectors > kCfbMaxSectors)
    {
        ThrowMalformed(kFormat, "the package needs " + std::to_string(layout.total_sectors) +
                                    " sectors; the format allows at most " +
                                    std::to_string(kCfbMaxSectors));
    }
    return layout;
}

// ── Serialisation ──────────────────────────────────────────

void WriteDirectoryEntry(std::uint8_t *at, const Entry &entry, bool is_root, const Layout &layout)
{
    const CfbNode &node = *entry.node;
    const std::u16string &name = node.name;
    for(std::size_t i = 0; i < name.size(); ++i)
    {
        PutLE16(at + 2 * i, static_cast<std::uint16_t>(name[i]));
    }
    PutLE16(at + 64, static_cast<std::uint16_t>((name.size() + 1) * 2));
    at[66] = is_root ? kTypeRoot : (node.is_storage ? kTypeStorage : kTypeStream);
    at[67] = kColourBlack;
    PutLE32(at + 68, entry.left);
    PutLE32(at + 72, entry.right);
    PutLE32(at + 76, entry.child);
    std::memcpy(at + 80, node.class_id.data(), node.class_id.size());
    PutLE32(at + 96, node.state_bits);
    PutLE64(at + 100, node.creation_time);
    PutLE64(at + 108, node.modification_time);
    if(is_root)
    {
        PutLE32(at + 116, layout.mini_sectors == 0
                              ? kCfbEndOfChain
                              : static_cast<std::uint32_t>(layout.mini_stream_start));
        PutLE64(at + 120, layout.mini_sectors * kMiniSectorSize);
    }
    else if(node.is_storage)
    {
        PutLE32(at + 116, 0);
    }
    else
    {
        PutLE32(at + 116, entry.start);
        PutLE64(at + 120, entry.size);
    }
}

void WriteUnusedEntry(std::uint8_t *at)
{
    PutLE32(at + 68, kCfbNoStream);
    PutLE32(at + 72, kCfbNoStream);
    PutLE32(at + 76, kCfbNoStream);
    PutLE32(at + 116, kCfbFreeSector);
}

// Sets a run of consecutive sectors in an index.
void SetRun(std::vector<std::uint32_t> &index, std::uint64_t first, std::uint64_t count)
{
    for(std::uint64_t i = 0; i < count; ++i)
    {
        index[static_cast<std::size_t>(first + i)] =
            i + 1 == count ? kCfbEndOfChain : static_cast<std::uint32_t>(first + i + 1);
    }
}

void CopyPieces(std::uint8_t *to, const std::vector<Piece> &pieces)
{
    for(const Piece &piece : pieces)
    {
        if(piece.length != 0)
        {
            std::memcpy(to, piece.data, static_cast<std::size_t>(piece.length));
        }
        to += piece.length;
    }
}

std::vector<std::uint8_t> Serialise(const PackageModel &model, std::vector<Entry> &entries,
                                    const Layout &layout)
{
    const CfbHeader &header = model.Header();
    const std::uint64_t sector_size = layout.sector_size;
    const std::uint64_t total_bytes = layout.header_size + layout.total_sectors * sector_size;
    std::vector<std::uint8_t> out(ToSize(total_bytes, kFormat, "package size"));
    const auto sector_at = [&](std::uint64_t sector)
    { return out.data() + layout.header_size + sector * sector_size; };

    // Directory entries and the tree of each storage.
    for(std::size_t i = 0; i < entries.size(); ++i)
    {
        if(entries[i].node->is_storage)
        {
            entries[i].child =
                BuildTree(entries, entries[i].children, 0, entries[i].children.size());
        }
    }

    // Allocation index: chains of every region, then the index sectors themselves.
    std::vector<std::uint32_t> fat(
        static_cast<std::size_t>(layout.fat_sectors * layout.entries_per_index_sector),
        kCfbFreeSector);
    for(const Entry &entry : entries)
    {
        if(!entry.node->is_storage && !entry.in_mini_stream && entry.size != 0)
        {
            SetRun(fat, entry.start, UnitsFor(entry.size, sector_size));
        }
    }
    SetRun(fat, layout.mini_stream_start, layout.mini_stream_sectors);
    SetRun(fat, layout.mini_index_start, layout.mini_index_sectors);
    SetRun(fat, layout.directory_start, layout.directory_sectors);
    for(std::uint64_t i = 0; i < layout.difat_sectors; ++i)
    {
        fat[static_cast<std::size_t>(layout.difat_start + i)] = kDifatSector;
    }
    for(std::uint64_t i = 0; i < layout.fat_sectors; ++i)
    {
        fat[static_cast<std::size_t>(layout.fat_start + i)] = kFatSector;
    }

    // Stream data, and the mini index.
    std::vector<std::uint32_t> mini_index(
        static_cast<std::size_t>(layout.mini_index_sectors * layout.entries_per_index_sector),
        kCfbFreeSector);
    for(const Entry &entry : entries)
    {
        if(entry.node->is_storage || entry.size == 0)
        {
            continue;
        }
        if(entry.in_mini_stream)
        {
            SetRun(mini_index, entry.start, UnitsFor(entry.size, kMiniSectorSize));
            CopyPieces(sector_at(layout.mini_stream_start) + entry.start * kMiniSectorSize,
                       entry.pieces);
        }
        else
        {
            CopyPieces(sector_at(entry.start), entry.pieces);
        }
    }
    for(std::size_t i = 0; i < mini_index.size(); ++i)
    {
        PutLE32(sector_at(layout.mini_index_start) + 4 * i, mini_index[i]);
    }

    // Directory, padded with free entries.
    const std::uint64_t slots = layout.directory_sectors * (sector_size / kDirEntrySize);
    for(std::uint64_t i = 0; i < slots; ++i)
    {
        std::uint8_t *at = sector_at(layout.directory_start) + i * kDirEntrySize;
        if(i < entries.size())
        {
            WriteDirectoryEntry(at, entries[static_cast<std::size_t>(i)], i == 0, layout);
        }
        else
        {
            WriteUnusedEntry(at);
        }
    }

    // The allocation index sectors, and the extended index listing those past 109.
    for(std::size_t i = 0; i < fat.size(); ++i)
    {
        PutLE32(sector_at(layout.fat_start) + 4 * i, fat[i]);
    }
    const std::uint64_t per_difat = layout.entries_per_index_sector - 1;
    for(std::uint64_t d = 0; d < layout.difat_sectors; ++d)
    {
        std::uint8_t *at = sector_at(layout.difat_start + d);
        for(std::uint64_t j = 0; j < per_difat; ++j)
        {
            const std::uint64_t listed = kHeaderDifatEntries + d * per_difat + j;
            PutLE32(at + 4 * j, listed < layout.fat_sectors
                                    ? static_cast<std::uint32_t>(layout.fat_start + listed)
                                    : kCfbFreeSector);
        }
        PutLE32(at + 4 * per_difat, d + 1 < layout.difat_sectors
                                        ? static_cast<std::uint32_t>(layout.difat_start + d + 1)
                                        : kCfbEndOfChain);
    }

    // Header.
    const std::uint8_t *source = model.Source();
    std::uint8_t *head = out.data();
    std::memcpy(head, kMagic, sizeof(kMagic));
    std::memcpy(head + 8, source + 8, 16);
    PutLE16(head + 24, header.minor_version);
    PutLE16(head + 26, header.major_version);
    PutLE16(head + 28, 0xFFFE);
    PutLE16(head + 30, header.major_version == 4 ? 12 : 9);
    PutLE16(head + 32, 6);
    PutLE32(head + 40, header.major_version == 4
                           ? static_cast<std::uint32_t>(layout.directory_sectors)
                           : 0);
    PutLE32(head + 44, static_cast<std::uint32_t>(layout.fat_sectors));
    PutLE32(head + 48, static_cast<std::uint32_t>(layout.directory_start));
    std::memcpy(head + 52, source + 52, 4);
    PutLE32(head + 56, header.mini_stream_cutoff);
    PutLE32(head + 60, layout.mini_index_sectors == 0
                           ? kCfbEndOfChain
                           : static_cast<std::uint32_t>(layout.mini_index_start));
    PutLE32(head + 64, static_cast<std::uint32_t>(layout.mini_index_sectors));
    PutLE32(head + 68, layout.difat_sectors == 0
                           ? kCfbEndOfChain
                           : static_cast<std::uint32_t>(layout.difat_start));
    PutLE32(head + 72, static_cast<std::uint32_t>(layout.difat_sectors));
    for(std::size_t i = 0; i < kHeaderDifatEntries; ++i)
    {
        PutLE32(head + 76 + 4 * i, i < layout.fat_sectors
                                       ? static_cast<std::uint32_t>(layout.fat_start + i)
                                       : kCfbFreeSector);
    }
    return out;
}

// ── Self-check ─────────────────────────────────────────────

[[noreturn]] void ThrowDifference(const std::u16string &name, const char *what)
{
    ThrowMalformed(kFormat, "internal error: the rebuilt package differs from the intended one (" +
                                std::string(what) + " of \"" + PrintableName(name) + "\")");
}

bool SameBytes(const std::vector<Piece> &a, const std::vector<Piece> &b)
{
    std::size_t i = 0;
    std::size_t j = 0;
    std::uint64_t used_a = 0;
    std::uint64_t used_b = 0;
    while(i < a.size() && j < b.size())
    {
        const std::uint64_t left_a = a[i].length - used_a;
        const std::uint64_t left_b = b[j].length - used_b;
        const std::uint64_t take = std::min(left_a, left_b);
        if(take != 0 &&
           std::memcmp(a[i].data + used_a, b[j].data + used_b, static_cast<std::size_t>(take)) != 0)
        {
            return false;
        }
        used_a += take;
        used_b += take;
        if(used_a == a[i].length)
        {
            ++i;
            used_a = 0;
        }
        if(used_b == b[j].length)
        {
            ++j;
            used_b = 0;
        }
    }
    while(i < a.size() && a[i].length == 0)
    {
        ++i;
    }
    while(j < b.size() && b[j].length == 0)
    {
        ++j;
    }
    return i == a.size() && j == b.size();
}

void CompareEntry(const std::vector<Entry> &entries, std::uint32_t position, PackageModel &check,
                  const CfbNode &got)
{
    const Entry &entry = entries[position];
    const CfbNode &want = *entry.node;
    if(want.name != got.name)
    {
        ThrowDifference(want.name, "name");
    }
    if(want.is_storage != got.is_storage)
    {
        ThrowDifference(want.name, "kind");
    }
    if(want.class_id != got.class_id)
    {
        ThrowDifference(want.name, "class identifier");
    }
    if(want.state_bits != got.state_bits)
    {
        ThrowDifference(want.name, "state bits");
    }
    if(want.creation_time != got.creation_time || want.modification_time != got.modification_time)
    {
        ThrowDifference(want.name, "times");
    }

    if(!want.is_storage)
    {
        if(entry.size != got.size)
        {
            ThrowDifference(want.name, "size");
        }
        std::vector<Piece> written;
        for(const ByteExtent &extent : check.Resolve(got))
        {
            written.push_back({check.Source() + extent.offset, extent.length});
        }
        if(!SameBytes(entry.pieces, written))
        {
            ThrowDifference(want.name, "bytes");
        }
        return;
    }

    if(entry.children.size() != got.children.size())
    {
        ThrowDifference(want.name, "entry count");
    }

    // Every entry of every storage must be reachable by a search in the format's
    // order, because the writer puts each storage's entries into that order.
    for(std::size_t i = 0; i < entry.children.size(); ++i)
    {
        const CfbNode &child = got.children[i];
        if(PackageModel::FindBySearch(got, entries[entry.children[i]].node->name) != &child)
        {
            ThrowDifference(entries[entry.children[i]].node->name, "place in the search tree");
        }
        CompareEntry(entries, entry.children[i], check, child);
    }
}

} // namespace

std::vector<std::uint8_t> WriteCfb(PackageModel &model, const std::vector<std::uint8_t> *signature)
{
    if(signature != nullptr && signature->empty())
    {
        signature = nullptr;
    }

    Flattener flattener(model, signature);
    std::vector<Entry> entries = flattener.Run();
    const Layout layout = ComputeLayout(entries, model.Header());
    std::vector<std::uint8_t> out = Serialise(model, entries, layout);

    PackageModel check = CfbReader::Parse(out);
    CompareEntry(entries, 0, check, check.Root());
    return out;
}

} // namespace seed::internal
