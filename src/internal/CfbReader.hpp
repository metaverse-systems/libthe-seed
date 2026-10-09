#pragma once

// Reader for compound files (the container of Windows installer packages).
//
// A package is parsed once into a PackageModel: the header, the tables that
// locate sectors, and the tree of storages and streams with names, class
// identifiers, state bits and times. A stream is not copied: the model keeps
// where it starts and how long it is, and PackageModel::Resolve turns that
// into a list of byte ranges of the source buffer. Where the bytes of a stream
// live (the mini stream or ordinary sectors) is decided by the recorded size
// and the header's cut-off, never by guessing.
//
// Every read of the source goes through the bounds-checked views of
// BoundedBytes.hpp. A rejection is a std::runtime_error whose message starts
// with "MSI: ".

#include "BoundedBytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace seed::internal
{

inline constexpr std::uint32_t kCfbEndOfChain = 0xFFFFFFFE;
inline constexpr std::uint32_t kCfbFreeSector = 0xFFFFFFFF;
inline constexpr std::uint32_t kCfbNoStream = 0xFFFFFFFF;

// Storages nested deeper than this are refused. The root's children are at
// depth 1.
inline constexpr unsigned kCfbMaxStorageDepth = 32;

using CfbClassId = std::array<std::uint8_t, 16>;

struct CfbHeader
{
    std::uint16_t major_version = 0;
    std::uint16_t minor_version = 0;
    std::uint32_t sector_size = 0;        // 512 (version 3) or 4096 (version 4)
    std::uint32_t mini_sector_size = 0;   // 64
    std::uint32_t mini_stream_cutoff = 0; // 4096
};

// A range of the source buffer.
struct ByteExtent
{
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

using ByteExtents = std::vector<ByteExtent>;

// A storage or a stream. The children of a storage are in the in-order
// sequence of the package's search tree (sorted by the format's ordering in a
// well-formed package); left, right and tree_root give the tree itself as
// positions in that sequence.
struct CfbNode
{
    static constexpr std::uint32_t kNone = 0xFFFFFFFF;

    std::u16string name;
    bool is_storage = false;
    CfbClassId class_id{};
    std::uint32_t state_bits = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modification_time = 0;

    // Streams: where the stream starts and its recorded size.
    std::uint32_t start_sector = kCfbEndOfChain;
    std::uint64_t size = 0;

    // Storages: the children, and the root of their search tree.
    std::vector<CfbNode> children;
    std::uint32_t tree_root = kNone;

    // Position in the parent's search tree.
    std::uint32_t left = kNone;
    std::uint32_t right = kNone;
};

class CfbReader;

class PackageModel
{
public:
    const CfbHeader &Header() const
    {
        return this->header;
    }

    const CfbNode &Root() const
    {
        return this->root;
    }

    // The source bytes the model refers to; the caller keeps them alive.
    const std::uint8_t *Source() const
    {
        return this->data;
    }

    std::uint64_t SourceSize() const
    {
        return this->data_size;
    }

    // The byte ranges that hold a stream, in order. Chains are bounds-checked
    // and cycle-checked, no sector may belong to two streams within the life
    // of the model, and the chain must cover the recorded size and only the
    // size (rounded up to a unit). A stream of size zero has no ranges.
    ByteExtents Resolve(const CfbNode &stream);

    // Copy of the bytes of a stream.
    std::vector<std::uint8_t> ReadStream(const CfbNode &stream);

    // The format's ordering of names in one storage: shorter first, then the
    // code units upper-cased (ASCII letters). Zero means the same name.
    static int CompareNames(const std::u16string &a, const std::u16string &b);

    // The first child with this exact name in the sequence, or nullptr.
    static const CfbNode *FindByEnumeration(const CfbNode &storage, const std::u16string &name);

    // Search of the storage's tree by the format's ordering, as a reader that
    // does not enumerate would do it. nullptr when it does not find the name.
    static const CfbNode *FindBySearch(const CfbNode &storage, const std::u16string &name);

private:
    friend class CfbReader;

    std::uint64_t HeaderSize() const;
    std::uint64_t SectorCount() const;
    std::uint64_t SectorStart(std::uint32_t sector_id) const;
    ByteSpan Sector(std::uint32_t sector_id, std::uint64_t length, std::string_view what) const;

    std::vector<std::uint32_t> WalkChain(std::uint32_t start_sector,
                                         std::vector<std::uint32_t> &owner, std::uint32_t id,
                                         std::string_view what) const;
    std::vector<std::uint32_t> FollowChain(std::uint32_t start_sector, std::string_view what) const;
    std::vector<std::uint32_t> WalkMiniChain(std::uint32_t start_sector, std::uint32_t id);

    void CheckChainLength(std::uint64_t size, std::uint64_t chain_length, std::uint64_t unit,
                          const char *unit_name) const;
    ByteExtents ResolveOrdinary(const CfbNode &stream);
    ByteExtents ResolveMini(const CfbNode &stream);
    void EnsureMiniStream();

    const std::uint8_t *data = nullptr;
    std::uint64_t data_size = 0;
    CfbHeader header;
    CfbNode root;

    std::uint32_t first_dir_sector = 0;
    std::uint32_t first_mini_fat_sector = 0;
    std::uint32_t total_mini_fat_sectors = 0;
    std::uint32_t first_difat_sector = 0;
    std::uint32_t total_difat_sectors = 0;

    std::vector<std::uint32_t> fat;
    std::vector<std::uint32_t> mini_fat;

    // The mini stream (the root's stream), resolved on first use.
    std::uint32_t mini_container_start = kCfbEndOfChain;
    std::uint64_t mini_container_size = 0;
    std::vector<std::uint32_t> mini_container_chain;
    bool mini_container_ready = false;

    // Which stream used each sector and mini sector.
    std::vector<std::uint32_t> sector_owner;
    std::vector<std::uint32_t> mini_owner;
    std::uint32_t last_owner = 0;
};

class CfbReader
{
public:
    // Parses and validates a package. The model refers to `bytes`, which must
    // outlive it and stay unchanged.
    static PackageModel Parse(const std::vector<std::uint8_t> &bytes);

private:
    struct DirEntry;
    struct TreeState;

    static void ParseHeader(PackageModel &model);
    static void BuildFat(PackageModel &model);
    static void BuildMiniFat(PackageModel &model);
    static std::vector<DirEntry> ParseDirectory(const PackageModel &model);
    static void BuildTree(PackageModel &model, const std::vector<DirEntry> &directory);
    static std::vector<CfbNode> ReadStorage(TreeState &state, std::uint32_t storage_index,
                                            unsigned depth, std::uint32_t &tree_root);
};

} // namespace seed::internal
