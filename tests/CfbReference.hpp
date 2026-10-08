#pragma once

// An independent reader, searcher, builder and Authenticode fingerprint for
// Compound File packages (Windows Installer files), written for the tests.
//
// It includes nothing from src/ and shares no code with the library: parsing,
// the directory ordering, the SHA-256 and the package layout are written here
// from the published format, so a test that compares the library with it
// compares two separate implementations.
//
//   Package        reads version 3 and version 4 headers, the FAT with its
//                  extended index (DIFAT), the mini stream and the directory.
//                  It enumerates a storage (in-order walk of its search tree)
//                  and, separately, searches for one name by the format's
//                  ordering without enumerating.
//   Build          writes a package from a BuildNode tree: shapes of the search
//                  tree, 512 and 4096-byte sectors, extended index, growth of
//                  the directory and of the mini stream, nested storages and
//                  class identifiers.
//   Listing        names, bytes, class identifiers, state bits and times of
//                  everything, to compare before and after an operation.
//   Fingerprint    the Authenticode digest of a package (SHA-256): the children
//                  of each storage ordered by the raw bytes of their UTF-16LE
//                  names, stream bytes hashed, storages recursed, the class
//                  identifier of each storage hashed after its children, the
//                  two signature streams of the root left out. The recorded
//                  osslsigncode values decide whether it is right: the tests
//                  run it on every sample first.
//
// Errors are std::runtime_error with a message starting "reference reader: ".

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace seedtest::cfb
{

using Bytes = std::vector<std::uint8_t>;
using Name = std::u16string;
using ClassId = std::array<std::uint8_t, 16>;

inline constexpr std::uint32_t kEndOfChain = 0xFFFFFFFEu;
inline constexpr std::uint32_t kFreeSector = 0xFFFFFFFFu;
inline constexpr std::uint32_t kFatSector = 0xFFFFFFFDu;
inline constexpr std::uint32_t kDifatSector = 0xFFFFFFFCu;
inline constexpr std::uint32_t kNoStream = 0xFFFFFFFFu;
inline constexpr std::uint32_t kMiniCutoff = 4096;
inline constexpr std::uint32_t kMiniSectorSize = 64;

inline constexpr std::uint8_t kTypeUnused = 0;
inline constexpr std::uint8_t kTypeStorage = 1;
inline constexpr std::uint8_t kTypeStream = 2;
inline constexpr std::uint8_t kTypeRoot = 5;

[[noreturn]] inline void Fail(const std::string &message)
{
    throw std::runtime_error("reference reader: " + message);
}

inline Name SignatureName()
{
    Name name(1, static_cast<char16_t>(5));
    name += u"DigitalSignature";
    return name;
}

inline Name ExtendedSignatureName()
{
    Name name(1, static_cast<char16_t>(5));
    name += u"MsiDigitalSignatureEx";
    return name;
}

// Printable form of a name: ASCII as is, anything else as \uXXXX.
inline std::string Display(const Name &name)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for(const char16_t unit : name)
    {
        if(unit >= 0x20 && unit < 0x7F)
        {
            text += static_cast<char>(unit);
        }
        else
        {
            text += "\\u";
            for(int shift = 12; shift >= 0; shift -= 4)
            {
                text += digits[(unit >> shift) & 0xF];
            }
        }
    }
    return text;
}

inline std::string ToHex(const std::uint8_t *data, std::size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for(std::size_t i = 0; i < size; ++i)
    {
        text += digits[data[i] >> 4];
        text += digits[data[i] & 0xF];
    }
    return text;
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4).

class Sha256
{
public:
    Sha256()
    {
        static const std::uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::copy(initial, initial + 8, this->state);
    }

    void Update(const std::uint8_t *data, std::size_t size)
    {
        this->total += size;
        while(size > 0)
        {
            const std::size_t take = std::min<std::size_t>(size, 64 - this->fill);
            std::memcpy(this->block + this->fill, data, take);
            this->fill += take;
            data += take;
            size -= take;
            if(this->fill == 64)
            {
                this->Compress(this->block);
                this->fill = 0;
            }
        }
    }

    void Update(const Bytes &bytes)
    {
        this->Update(bytes.data(), bytes.size());
    }

    std::array<std::uint8_t, 32> Final()
    {
        const std::uint64_t bits = this->total * 8;
        const std::uint8_t one = 0x80;
        this->Update(&one, 1);
        const std::uint8_t zero = 0;
        while(this->fill != 56)
        {
            this->Update(&zero, 1);
        }
        std::uint8_t length[8];
        for(int i = 0; i < 8; ++i)
        {
            length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        }
        this->Update(length, 8);
        std::array<std::uint8_t, 32> out{};
        for(int i = 0; i < 8; ++i)
        {
            out[4 * i] = static_cast<std::uint8_t>(this->state[i] >> 24);
            out[4 * i + 1] = static_cast<std::uint8_t>(this->state[i] >> 16);
            out[4 * i + 2] = static_cast<std::uint8_t>(this->state[i] >> 8);
            out[4 * i + 3] = static_cast<std::uint8_t>(this->state[i]);
        }
        return out;
    }

    std::string FinalHex()
    {
        const auto digest = this->Final();
        return ToHex(digest.data(), digest.size());
    }

private:
    static std::uint32_t Rotate(std::uint32_t value, int bits)
    {
        return (value >> bits) | (value << (32 - bits));
    }

    void Compress(const std::uint8_t *chunk)
    {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        std::uint32_t w[64];
        for(int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<std::uint32_t>(chunk[4 * i]) << 24) |
                   (static_cast<std::uint32_t>(chunk[4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(chunk[4 * i + 2]) << 8) |
                   static_cast<std::uint32_t>(chunk[4 * i + 3]);
        }
        for(int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = this->state[0], b = this->state[1], c = this->state[2], d = this->state[3];
        std::uint32_t e = this->state[4], f = this->state[5], g = this->state[6], h = this->state[7];
        for(int i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + choose + k[i] + w[i];
            const std::uint32_t s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        this->state[0] += a;
        this->state[1] += b;
        this->state[2] += c;
        this->state[3] += d;
        this->state[4] += e;
        this->state[5] += f;
        this->state[6] += g;
        this->state[7] += h;
    }

    std::uint32_t state[8];
    std::uint8_t block[64] = {};
    std::size_t fill = 0;
    std::uint64_t total = 0;
};

inline std::string Sha256Hex(const Bytes &bytes)
{
    Sha256 hash;
    hash.Update(bytes);
    return hash.FinalHex();
}

// ---------------------------------------------------------------------------
// The format's ordering of names inside one storage: shorter names first, then
// the code units upper-cased (ASCII letters). Negative, zero or positive.

inline char16_t UpperCase(char16_t unit)
{
    return (unit >= u'a' && unit <= u'z') ? static_cast<char16_t>(unit - 32) : unit;
}

inline int CompareNames(const Name &a, const Name &b)
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

// ---------------------------------------------------------------------------
// Reader.

struct DirEntry
{
    Name name;
    std::uint8_t type = kTypeUnused;
    std::uint8_t color = 0;
    std::uint32_t left = kNoStream;
    std::uint32_t right = kNoStream;
    std::uint32_t child = kNoStream;
    ClassId class_id{};
    std::uint32_t state = 0;
    std::uint64_t created = 0;
    std::uint64_t modified = 0;
    std::uint32_t start = 0;
    std::uint64_t size = 0;
};

struct Extent
{
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

class Package
{
public:
    explicit Package(Bytes bytes) : bytes(std::move(bytes))
    {
        this->Parse();
    }

    const Bytes &Data() const
    {
        return this->bytes;
    }
    unsigned Version() const
    {
        return this->major;
    }
    std::uint32_t SectorSize() const
    {
        return this->sector_size;
    }
    std::uint32_t Cutoff() const
    {
        return this->cutoff;
    }
    const std::vector<DirEntry> &Entries() const
    {
        return this->entries;
    }
    std::uint32_t FatSectorCount() const
    {
        return this->fat_sector_count;
    }
    std::uint32_t DifatSectorCount() const
    {
        return this->difat_sector_count;
    }
    std::uint32_t FileSectorCount() const
    {
        return static_cast<std::uint32_t>((this->bytes.size() - this->sector_size) / this->sector_size);
    }

    // File offset of the directory entry with this index.
    std::uint64_t EntryOffset(std::uint32_t index) const
    {
        const std::uint32_t per_sector = this->sector_size / 128;
        const std::uint32_t hop = index / per_sector;
        if(hop >= this->directory_sectors.size())
        {
            Fail("directory entry " + std::to_string(index) + " is outside the directory");
        }
        return this->SectorOffset(this->directory_sectors[hop]) + 128ull * (index % per_sector);
    }

    // Children of a storage in the order of an in-order walk of its search
    // tree (which is the ordering of the format). Never searches by name.
    std::vector<std::uint32_t> Children(std::uint32_t storage) const
    {
        std::vector<std::uint32_t> order;
        if(storage >= this->entries.size())
        {
            Fail("entry out of range");
        }
        std::vector<bool> seen(this->entries.size(), false);
        std::vector<std::uint32_t> stack;
        std::uint32_t node = this->entries[storage].child;
        while(node != kNoStream || !stack.empty())
        {
            while(node != kNoStream)
            {
                if(node >= this->entries.size() || seen[node])
                {
                    Fail("search tree of entry " + std::to_string(storage) + " is not a tree");
                }
                seen[node] = true;
                stack.push_back(node);
                node = this->entries[node].left;
            }
            node = stack.back();
            stack.pop_back();
            order.push_back(node);
            node = this->entries[node].right;
        }
        return order;
    }

    // Looks a name up in one storage by walking its search tree with the
    // format's ordering. Never enumerates. Empty when the walk does not reach
    // the name (which is what a reader that searches would conclude).
    std::optional<std::uint32_t> Find(std::uint32_t storage, const Name &name) const
    {
        if(storage >= this->entries.size())
        {
            Fail("entry out of range");
        }
        std::uint32_t node = this->entries[storage].child;
        for(std::size_t steps = 0; node != kNoStream && steps <= this->entries.size(); ++steps)
        {
            if(node >= this->entries.size())
            {
                return std::nullopt;
            }
            const int order = CompareNames(name, this->entries[node].name);
            if(order == 0)
            {
                return node;
            }
            node = order < 0 ? this->entries[node].left : this->entries[node].right;
        }
        return std::nullopt;
    }

    // The signature stream of the root found by search, if the search reaches it.
    std::optional<std::uint32_t> FindSignature() const
    {
        return this->Find(0, SignatureName());
    }

    // Whether the stream's bytes are in the mini stream (decided by size).
    bool InMiniStream(std::uint32_t index) const
    {
        return this->entries.at(index).size < this->cutoff;
    }

    // The pieces of the file that hold the bytes of a stream, in order.
    std::vector<Extent> StreamExtents(std::uint32_t index) const
    {
        const DirEntry &entry = this->entries.at(index);
        std::vector<Extent> extents;
        if(entry.type != kTypeStream && entry.type != kTypeRoot)
        {
            Fail("entry " + std::to_string(index) + " is not a stream");
        }
        if(entry.size == 0)
        {
            return extents;
        }
        std::uint64_t left = entry.size;
        if(entry.size < this->cutoff && entry.type == kTypeStream)
        {
            const auto chain = this->Chain(this->mini_fat, entry.start);
            if(chain.size() * kMiniSectorSize < entry.size)
            {
                Fail("stream " + std::to_string(index) + " exceeds its chain of mini sectors");
            }
            for(const std::uint32_t mini : chain)
            {
                const std::uint64_t length = std::min<std::uint64_t>(left, kMiniSectorSize);
                const std::uint64_t at = static_cast<std::uint64_t>(mini) * kMiniSectorSize;
                this->AppendFromContainer(extents, at, length);
                left -= length;
                if(left == 0)
                {
                    break;
                }
            }
            return extents;
        }
        const auto chain = this->Chain(this->fat, entry.start);
        if(chain.size() * static_cast<std::uint64_t>(this->sector_size) < entry.size)
        {
            Fail("stream " + std::to_string(index) + " exceeds its chain of sectors");
        }
        for(const std::uint32_t sector : chain)
        {
            const std::uint64_t length = std::min<std::uint64_t>(left, this->sector_size);
            extents.push_back({this->SectorOffset(sector), length});
            left -= length;
            if(left == 0)
            {
                break;
            }
        }
        return extents;
    }

    Bytes ReadStream(std::uint32_t index) const
    {
        Bytes out;
        for(const Extent &extent : this->StreamExtents(index))
        {
            out.insert(out.end(), this->bytes.begin() + static_cast<std::ptrdiff_t>(extent.offset),
                       this->bytes.begin() + static_cast<std::ptrdiff_t>(extent.offset + extent.length));
        }
        return out;
    }

    // Sector numbers of the chain that holds the mini stream container.
    std::vector<std::uint32_t> MiniContainerChain() const
    {
        return this->Chain(this->fat, this->entries.at(0).start);
    }

private:
    static std::uint32_t GetU32(const Bytes &b, std::uint64_t at)
    {
        return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
               (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
    }
    static std::uint64_t GetU64(const Bytes &b, std::uint64_t at)
    {
        return static_cast<std::uint64_t>(GetU32(b, at)) | (static_cast<std::uint64_t>(GetU32(b, at + 4)) << 32);
    }
    static std::uint16_t GetU16(const Bytes &b, std::uint64_t at)
    {
        return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
    }

    std::uint64_t SectorOffset(std::uint32_t sector) const
    {
        const std::uint64_t offset = (static_cast<std::uint64_t>(sector) + 1) * this->sector_size;
        if(offset + this->sector_size > this->bytes.size())
        {
            Fail("sector " + std::to_string(sector) + " is past the end of the file");
        }
        return offset;
    }

    // Follows a chain through an allocation table; limits the steps so a loop
    // is an error.
    std::vector<std::uint32_t> Chain(const std::vector<std::uint32_t> &table, std::uint32_t start) const
    {
        std::vector<std::uint32_t> chain;
        std::uint32_t sector = start;
        while(sector != kEndOfChain)
        {
            if(sector >= table.size())
            {
                Fail("chain leaves its table at " + std::to_string(sector));
            }
            if(chain.size() > table.size())
            {
                Fail("chain loops");
            }
            chain.push_back(sector);
            sector = table[sector];
        }
        return chain;
    }

    // Appends the file pieces that hold [at, at + length) of the mini stream container.
    void AppendFromContainer(std::vector<Extent> &extents, std::uint64_t at, std::uint64_t length) const
    {
        const auto container = this->MiniContainerChain();
        const std::uint64_t index = at / this->sector_size;
        if(index >= container.size())
        {
            Fail("mini sector is past the end of the mini stream");
        }
        extents.push_back({this->SectorOffset(container[index]) + at % this->sector_size, length});
    }

    void Parse()
    {
        static const std::uint8_t magic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        if(this->bytes.size() < 512 || std::memcmp(this->bytes.data(), magic, 8) != 0)
        {
            Fail("not a compound file");
        }
        this->major = GetU16(this->bytes, 26);
        const unsigned shift = GetU16(this->bytes, 30);
        if((this->major == 3 && shift != 9) || (this->major == 4 && shift != 12) ||
           (this->major != 3 && this->major != 4))
        {
            Fail("unsupported version or sector size");
        }
        this->sector_size = 1u << shift;
        if(this->bytes.size() < this->sector_size)
        {
            Fail("file is shorter than one sector");
        }
        this->cutoff = GetU32(this->bytes, 56);
        this->fat_sector_count = GetU32(this->bytes, 44);
        this->difat_sector_count = GetU32(this->bytes, 72);
        const std::uint32_t per = this->sector_size / 4;

        // FAT sectors: 109 in the header, then the extended index chain.
        std::vector<std::uint32_t> fat_sectors;
        for(unsigned i = 0; i < 109 && fat_sectors.size() < this->fat_sector_count; ++i)
        {
            fat_sectors.push_back(GetU32(this->bytes, 76 + 4ull * i));
        }
        std::uint32_t difat = GetU32(this->bytes, 68);
        std::uint32_t visited = 0;
        while(fat_sectors.size() < this->fat_sector_count)
        {
            if(difat == kEndOfChain || difat == kFreeSector || ++visited > this->difat_sector_count)
            {
                Fail("the extended index is shorter than the FAT needs");
            }
            const std::uint64_t at = this->SectorOffset(difat);
            for(std::uint32_t i = 0; i + 1 < per && fat_sectors.size() < this->fat_sector_count; ++i)
            {
                fat_sectors.push_back(GetU32(this->bytes, at + 4ull * i));
            }
            difat = GetU32(this->bytes, at + 4ull * (per - 1));
        }
        for(const std::uint32_t sector : fat_sectors)
        {
            const std::uint64_t at = this->SectorOffset(sector);
            for(std::uint32_t i = 0; i < per; ++i)
            {
                this->fat.push_back(GetU32(this->bytes, at + 4ull * i));
            }
        }

        // Mini FAT.
        const std::uint32_t first_mini_fat = GetU32(this->bytes, 60);
        if(first_mini_fat != kEndOfChain)
        {
            for(const std::uint32_t sector : this->Chain(this->fat, first_mini_fat))
            {
                const std::uint64_t at = this->SectorOffset(sector);
                for(std::uint32_t i = 0; i < per; ++i)
                {
                    this->mini_fat.push_back(GetU32(this->bytes, at + 4ull * i));
                }
            }
        }

        // Directory.
        this->directory_sectors = this->Chain(this->fat, GetU32(this->bytes, 48));
        for(const std::uint32_t sector : this->directory_sectors)
        {
            const std::uint64_t base = this->SectorOffset(sector);
            for(std::uint32_t slot = 0; slot < this->sector_size / 128; ++slot)
            {
                this->entries.push_back(this->ParseEntry(base + 128ull * slot));
            }
        }
        if(this->entries.empty() || this->entries[0].type != kTypeRoot)
        {
            Fail("entry 0 is not the root storage");
        }
    }

    DirEntry ParseEntry(std::uint64_t at) const
    {
        DirEntry entry;
        const unsigned name_bytes = GetU16(this->bytes, at + 64);
        if(name_bytes > 64 || (name_bytes % 2) != 0)
        {
            Fail("bad name length");
        }
        for(unsigned i = 0; i + 2 <= name_bytes && i < 62; i += 2)
        {
            entry.name.push_back(static_cast<char16_t>(GetU16(this->bytes, at + i)));
        }
        entry.type = this->bytes[at + 66];
        entry.color = this->bytes[at + 67];
        entry.left = GetU32(this->bytes, at + 68);
        entry.right = GetU32(this->bytes, at + 72);
        entry.child = GetU32(this->bytes, at + 76);
        std::copy(this->bytes.begin() + static_cast<std::ptrdiff_t>(at + 80),
                  this->bytes.begin() + static_cast<std::ptrdiff_t>(at + 96), entry.class_id.begin());
        entry.state = GetU32(this->bytes, at + 96);
        entry.created = GetU64(this->bytes, at + 100);
        entry.modified = GetU64(this->bytes, at + 108);
        entry.start = GetU32(this->bytes, at + 116);
        entry.size = this->major == 3 ? GetU32(this->bytes, at + 120) : GetU64(this->bytes, at + 120);
        if(entry.type != kTypeUnused && !entry.name.empty() && entry.name.back() == 0)
        {
            entry.name.pop_back();
        }
        return entry;
    }

    Bytes bytes;
    unsigned major = 0;
    std::uint32_t sector_size = 0;
    std::uint32_t cutoff = 0;
    std::uint32_t fat_sector_count = 0;
    std::uint32_t difat_sector_count = 0;
    std::vector<std::uint32_t> fat;
    std::vector<std::uint32_t> mini_fat;
    std::vector<std::uint32_t> directory_sectors;
    std::vector<DirEntry> entries;
};

// ---------------------------------------------------------------------------
// Listing: everything about a package that an operation must keep.

struct ListedEntry
{
    std::string path;       // names joined with '/', the root is "/"
    bool storage = false;
    std::uint64_t size = 0; // streams
    std::string sha256;     // streams: SHA-256 of the bytes
    std::string class_id;   // hex
    std::uint32_t state = 0;
    std::uint64_t created = 0;
    std::uint64_t modified = 0;

    bool operator==(const ListedEntry &other) const
    {
        return this->path == other.path && this->storage == other.storage && this->size == other.size &&
               this->sha256 == other.sha256 && this->class_id == other.class_id &&
               this->state == other.state && this->created == other.created &&
               this->modified == other.modified;
    }
};

inline bool IsSignatureName(const Name &name)
{
    return name == SignatureName() || name == ExtendedSignatureName();
}

namespace detail
{
inline void ListInto(const Package &package, std::uint32_t storage, const std::string &prefix,
                     bool skip_signatures, std::vector<ListedEntry> &out, unsigned depth)
{
    if(depth > 64)
    {
        Fail("storages nested deeper than 64");
    }
    for(const std::uint32_t index : package.Children(storage))
    {
        const DirEntry &entry = package.Entries()[index];
        if(storage == 0 && skip_signatures && IsSignatureName(entry.name))
        {
            continue;
        }
        ListedEntry listed;
        listed.path = prefix + "/" + Display(entry.name);
        listed.storage = entry.type != kTypeStream;
        listed.class_id = ToHex(entry.class_id.data(), entry.class_id.size());
        listed.state = entry.state;
        listed.created = entry.created;
        listed.modified = entry.modified;
        if(!listed.storage)
        {
            listed.size = entry.size;
            listed.sha256 = Sha256Hex(package.ReadStream(index));
        }
        out.push_back(listed);
        if(listed.storage)
        {
            ListInto(package, index, listed.path, skip_signatures, out, depth + 1);
        }
    }
}
} // namespace detail

// Every storage and stream below the root plus the root itself, sorted by
// path. With skip_signatures the two signature streams of the root are left out.
inline std::vector<ListedEntry> Listing(const Package &package, bool skip_signatures = true)
{
    std::vector<ListedEntry> out;
    const DirEntry &root = package.Entries()[0];
    ListedEntry first;
    first.path = "/";
    first.storage = true;
    first.class_id = ToHex(root.class_id.data(), root.class_id.size());
    first.state = root.state;
    first.created = root.created;
    first.modified = root.modified;
    out.push_back(first);
    detail::ListInto(package, 0, "", skip_signatures, out, 0);
    std::sort(out.begin(), out.end(),
              [](const ListedEntry &a, const ListedEntry &b) { return a.path < b.path; });
    return out;
}

// Empty when the two listings are equal; otherwise a sentence naming the first difference.
inline std::string FirstDifference(const std::vector<ListedEntry> &before, const std::vector<ListedEntry> &after)
{
    const std::size_t common = std::min(before.size(), after.size());
    for(std::size_t i = 0; i < common; ++i)
    {
        if(before[i].path != after[i].path)
        {
            return "entry " + std::to_string(i) + " is " + before[i].path + " before and " + after[i].path + " after";
        }
        if(!(before[i] == after[i]))
        {
            const ListedEntry &a = before[i];
            const ListedEntry &b = after[i];
            std::string what = a.size != b.size ? "size" : a.sha256 != b.sha256 ? "bytes"
                               : a.class_id != b.class_id ? "class identifier"
                               : a.state != b.state ? "state bits"
                               : a.storage != b.storage ? "kind" : "times";
            return a.path + ": " + what + " differ";
        }
    }
    if(before.size() != after.size())
    {
        return "there are " + std::to_string(before.size()) + " entries before and " +
               std::to_string(after.size()) + " after";
    }
    return "";
}

// Number of storages and streams below the root, signature streams included.
inline std::size_t CountBelowRoot(const Package &package)
{
    return Listing(package, false).size() - 1;
}

// ---------------------------------------------------------------------------
// Authenticode fingerprint.

namespace detail
{
inline Bytes RawName(const Name &name)
{
    Bytes raw;
    for(const char16_t unit : name)
    {
        raw.push_back(static_cast<std::uint8_t>(unit & 0xFF));
        raw.push_back(static_cast<std::uint8_t>(unit >> 8));
    }
    return raw;
}

inline void HashStorage(const Package &package, std::uint32_t storage, Sha256 &hash, unsigned depth)
{
    if(depth > 64)
    {
        Fail("storages nested deeper than 64");
    }
    std::vector<std::pair<Bytes, std::uint32_t>> kids;
    for(const std::uint32_t index : package.Children(storage))
    {
        const DirEntry &entry = package.Entries()[index];
        if(storage == 0 && IsSignatureName(entry.name))
        {
            continue;
        }
        kids.emplace_back(RawName(entry.name), index);
    }
    // Raw bytes of the UTF-16LE name; on a common prefix the shorter name is first.
    std::sort(kids.begin(), kids.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for(const auto &kid : kids)
    {
        const DirEntry &entry = package.Entries()[kid.second];
        if(entry.type == kTypeStream)
        {
            hash.Update(package.ReadStream(kid.second));
        }
        else
        {
            HashStorage(package, kid.second, hash, depth + 1);
        }
    }
    const ClassId &id = package.Entries()[storage].class_id;
    hash.Update(id.data(), id.size());
}
} // namespace detail

inline std::string Fingerprint(const Package &package)
{
    Sha256 hash;
    detail::HashStorage(package, 0, hash, 0);
    return hash.FinalHex();
}

inline std::string Fingerprint(const Bytes &bytes)
{
    return Fingerprint(Package(bytes));
}

// ---------------------------------------------------------------------------
// Builder.

struct BuildNode
{
    Name name;
    bool storage = false;
    Bytes data;                     // streams
    ClassId class_id{};
    std::uint32_t state = 0;
    std::uint64_t created = 0;
    std::uint64_t modified = 0;
    std::vector<BuildNode> children; // storages, in any order

    static BuildNode Stream(Name name, Bytes data)
    {
        BuildNode node;
        node.name = std::move(name);
        node.data = std::move(data);
        return node;
    }

    static BuildNode Storage(Name name, ClassId class_id = {})
    {
        BuildNode node;
        node.name = std::move(name);
        node.storage = true;
        node.class_id = class_id;
        return node;
    }

    BuildNode &Add(BuildNode child)
    {
        this->children.push_back(std::move(child));
        return this->children.back();
    }

    // The first child with exactly this name, or nullptr.
    BuildNode *Child(const Name &wanted)
    {
        for(BuildNode &child : this->children)
        {
            if(child.name == wanted)
            {
                return &child;
            }
        }
        return nullptr;
    }
};

// Chooses which of the names sorted[lo, hi) becomes the top of that part of the
// search tree; the rest of the range is split to its left and right.
using Picker = std::function<std::size_t(const std::vector<Name> &sorted, std::size_t lo, std::size_t hi)>;

inline Picker BalancedPicker()
{
    return [](const std::vector<Name> &, std::size_t lo, std::size_t hi) { return (lo + hi) / 2; };
}

// Every entry has only a left neighbour (the largest name is the top).
inline Picker LeftChainPicker()
{
    return [](const std::vector<Name> &, std::size_t, std::size_t hi) { return hi - 1; };
}

// Every entry has only a right neighbour (the smallest name is the top).
inline Picker RightChainPicker()
{
    return [](const std::vector<Name> &, std::size_t lo, std::size_t) { return lo; };
}

// The named entry is the top wherever its range contains it; elsewhere balanced.
inline Picker TopPicker(Name wanted)
{
    return [wanted](const std::vector<Name> &sorted, std::size_t lo, std::size_t hi) {
        for(std::size_t i = lo; i < hi; ++i)
        {
            if(sorted[i] == wanted)
            {
                return i;
            }
        }
        return (lo + hi) / 2;
    };
}

struct BuildOptions
{
    unsigned version = 3;   // 3: 512-byte sectors, 4: 4,096-byte sectors
    Picker picker = BalancedPicker();
};

namespace detail
{
inline void Put16(Bytes &b, std::uint64_t at, std::uint16_t v)
{
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
inline void Put32(Bytes &b, std::uint64_t at, std::uint32_t v)
{
    for(int i = 0; i < 4; ++i)
    {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}
inline void Put64(Bytes &b, std::uint64_t at, std::uint64_t v)
{
    for(int i = 0; i < 8; ++i)
    {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}
inline std::uint64_t CeilDiv(std::uint64_t a, std::uint64_t b)
{
    return (a + b - 1) / b;
}

struct Placed
{
    const BuildNode *node = nullptr;
    std::uint32_t left = kNoStream;
    std::uint32_t right = kNoStream;
    std::uint32_t child = kNoStream;
    std::uint32_t start = kEndOfChain;
    std::uint64_t size = 0;
};

class Builder
{
public:
    Builder(const BuildNode &root, const BuildOptions &options) : root(root), options(options)
    {
    }

    Bytes Run()
    {
        if(this->options.version != 3 && this->options.version != 4)
        {
            Fail("builder: version must be 3 or 4");
        }
        this->sector = this->options.version == 3 ? 512 : 4096;
        this->per = this->sector / 4;
        this->placed.push_back({&this->root, kNoStream, kNoStream, kNoStream, kEndOfChain, 0});
        this->Assign(0, this->root);
        this->Link(0, this->root);
        this->Layout();
        return this->Write();
    }

private:
    // Sorted children of one storage, as indices into `placed`.
    std::vector<std::uint32_t> Assign(std::uint32_t self, const BuildNode &storage)
    {
        std::vector<const BuildNode *> kids;
        for(const BuildNode &child : storage.children)
        {
            kids.push_back(&child);
        }
        std::stable_sort(kids.begin(), kids.end(), [](const BuildNode *a, const BuildNode *b) {
            return CompareNames(a->name, b->name) < 0;
        });
        std::vector<std::uint32_t> indices;
        for(std::size_t i = 0; i < kids.size(); ++i)
        {
            if(kids[i]->name.empty() || kids[i]->name.size() > 31)
            {
                Fail("builder: a name must have 1 to 31 units");
            }
            if(i > 0 && CompareNames(kids[i - 1]->name, kids[i]->name) == 0)
            {
                Fail("builder: two names are equal by the format's ordering in one storage");
            }
            const std::uint32_t index = static_cast<std::uint32_t>(this->placed.size());
            this->placed.push_back({kids[i], kNoStream, kNoStream, kNoStream, kEndOfChain, 0});
            indices.push_back(index);
            if(kids[i]->storage)
            {
                this->sorted_children[index] = this->Assign(index, *kids[i]);
            }
        }
        this->sorted_children[self] = indices;
        return indices;
    }

    std::uint32_t Tree(const std::vector<std::uint32_t> &indices, const std::vector<Name> &names,
                       std::size_t lo, std::size_t hi)
    {
        if(lo >= hi)
        {
            return kNoStream;
        }
        std::size_t top = this->options.picker(names, lo, hi);
        top = std::min(std::max(top, lo), hi - 1);
        const std::uint32_t index = indices[top];
        this->placed[index].left = this->Tree(indices, names, lo, top);
        this->placed[index].right = this->Tree(indices, names, top + 1, hi);
        return index;
    }

    void Link(std::uint32_t self, const BuildNode &)
    {
        const auto &indices = this->sorted_children[self];
        std::vector<Name> names;
        for(const std::uint32_t index : indices)
        {
            names.push_back(this->placed[index].node->name);
        }
        this->placed[self].child = this->Tree(indices, names, 0, indices.size());
        for(const std::uint32_t index : indices)
        {
            if(this->placed[index].node->storage)
            {
                this->Link(index, *this->placed[index].node);
            }
        }
    }

    void Layout()
    {
        std::uint64_t data_sectors = 0;
        std::uint64_t mini_sectors = 0;
        for(Placed &p : this->placed)
        {
            if(p.node == &this->root || p.node->storage)
            {
                continue;
            }
            p.size = p.node->data.size();
            if(p.size >= kMiniCutoff)
            {
                data_sectors += CeilDiv(p.size, this->sector);
            }
            else if(p.size > 0)
            {
                mini_sectors += CeilDiv(p.size, kMiniSectorSize);
            }
        }
        this->data_sector_count = data_sectors;
        this->mini_sector_count = mini_sectors;
        this->container_sectors = CeilDiv(mini_sectors * kMiniSectorSize, this->sector);
        this->mini_fat_sectors = CeilDiv(mini_sectors * 4, this->sector);
        this->dir_sectors = CeilDiv(this->placed.size() * 128, this->sector);
        const std::uint64_t base = data_sectors + this->container_sectors + this->mini_fat_sectors + this->dir_sectors;
        std::uint64_t fat = 0;
        for(;;)
        {
            const std::uint64_t difat = fat > 109 ? CeilDiv(fat - 109, this->per - 1) : 0;
            const std::uint64_t next = CeilDiv(base + difat + fat, this->per);
            if(next == fat)
            {
                this->difat_sectors = difat;
                break;
            }
            fat = next;
        }
        this->fat_sectors = fat;
        this->total_sectors = base + this->difat_sectors + this->fat_sectors;
        if(this->total_sectors > 0xFFFFFFFAull)
        {
            Fail("builder: too many sectors");
        }
    }

    Bytes Write()
    {
        Bytes out(static_cast<std::size_t>((this->total_sectors + 1) * this->sector), 0);
        std::vector<std::uint32_t> fat(static_cast<std::size_t>(this->fat_sectors * this->per), kFreeSector);
        std::vector<std::uint32_t> mini_fat(static_cast<std::size_t>(this->mini_fat_sectors * this->per), kFreeSector);
        const auto at = [&](std::uint64_t sector_number) { return (sector_number + 1) * this->sector; };
        const auto chain = [&](std::uint64_t first, std::uint64_t count) {
            for(std::uint64_t i = 0; i < count; ++i)
            {
                fat[first + i] = i + 1 == count ? kEndOfChain : static_cast<std::uint32_t>(first + i + 1);
            }
        };

        std::uint64_t next = 0;
        std::uint64_t mini_next = 0;
        const std::uint64_t container_first = this->data_sector_count;
        // Streams.
        for(Placed &p : this->placed)
        {
            if(p.node == &this->root || p.node->storage || p.size == 0)
            {
                continue;
            }
            if(p.size >= kMiniCutoff)
            {
                const std::uint64_t count = CeilDiv(p.size, this->sector);
                p.start = static_cast<std::uint32_t>(next);
                std::copy(p.node->data.begin(), p.node->data.end(),
                          out.begin() + static_cast<std::ptrdiff_t>(at(next)));
                chain(next, count);
                next += count;
            }
            else
            {
                const std::uint64_t count = CeilDiv(p.size, kMiniSectorSize);
                p.start = static_cast<std::uint32_t>(mini_next);
                std::copy(p.node->data.begin(), p.node->data.end(),
                          out.begin() + static_cast<std::ptrdiff_t>(at(container_first) + mini_next * kMiniSectorSize));
                for(std::uint64_t i = 0; i < count; ++i)
                {
                    mini_fat[mini_next + i] = i + 1 == count ? kEndOfChain : static_cast<std::uint32_t>(mini_next + i + 1);
                }
                mini_next += count;
            }
        }
        // Mini stream container, mini FAT, directory.
        next = container_first;
        std::uint32_t container_start = kEndOfChain;
        if(this->container_sectors > 0)
        {
            container_start = static_cast<std::uint32_t>(next);
            chain(next, this->container_sectors);
            next += this->container_sectors;
        }
        std::uint32_t mini_fat_start = kEndOfChain;
        if(this->mini_fat_sectors > 0)
        {
            mini_fat_start = static_cast<std::uint32_t>(next);
            chain(next, this->mini_fat_sectors);
            for(std::size_t i = 0; i < mini_fat.size(); ++i)
            {
                Put32(out, at(next) + 4 * i, mini_fat[i]);
            }
            next += this->mini_fat_sectors;
        }
        const std::uint32_t dir_start = static_cast<std::uint32_t>(next);
        chain(next, this->dir_sectors);
        for(std::size_t i = 0; i < this->dir_sectors * (this->sector / 128); ++i)
        {
            const std::uint64_t entry_at = at(dir_start) + 128 * i;
            if(i >= this->placed.size())
            {
                Put32(out, entry_at + 68, kNoStream);
                Put32(out, entry_at + 72, kNoStream);
                Put32(out, entry_at + 76, kNoStream);
                continue;
            }
            const Placed &p = this->placed[i];
            const bool is_root = i == 0;
            const Name name = is_root ? Name(u"Root Entry") : p.node->name;
            for(std::size_t u = 0; u < name.size(); ++u)
            {
                Put16(out, entry_at + 2 * u, name[u]);
            }
            Put16(out, entry_at + 64, static_cast<std::uint16_t>((name.size() + 1) * 2));
            out[entry_at + 66] = is_root ? kTypeRoot : (p.node->storage ? kTypeStorage : kTypeStream);
            out[entry_at + 67] = 1;
            Put32(out, entry_at + 68, p.left);
            Put32(out, entry_at + 72, p.right);
            Put32(out, entry_at + 76, p.child);
            std::copy(p.node->class_id.begin(), p.node->class_id.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(entry_at + 80));
            Put32(out, entry_at + 96, p.node->state);
            Put64(out, entry_at + 100, p.node->created);
            Put64(out, entry_at + 108, p.node->modified);
            if(is_root)
            {
                Put32(out, entry_at + 116, container_start);
                Put64(out, entry_at + 120, this->mini_sector_count * kMiniSectorSize);
            }
            else
            {
                Put32(out, entry_at + 116, p.start);
                Put64(out, entry_at + 120, p.node->storage ? 0 : p.size);
            }
        }
        next += this->dir_sectors;
        // Extended index, then the FAT sectors themselves.
        const std::uint64_t difat_first = next;
        const std::uint64_t fat_first = next + this->difat_sectors;
        for(std::uint64_t i = 0; i < this->difat_sectors; ++i)
        {
            fat[difat_first + i] = kDifatSector;
        }
        for(std::uint64_t i = 0; i < this->fat_sectors; ++i)
        {
            fat[fat_first + i] = kFatSector;
        }
        for(std::size_t i = 0; i < fat.size(); ++i)
        {
            const std::uint64_t sector_number = i / this->per;
            Put32(out, at(fat_first + sector_number) + 4 * (i % this->per), fat[i]);
        }
        for(std::uint64_t i = 0; i < this->fat_sectors; ++i)
        {
            if(i < 109)
            {
                Put32(out, 76 + 4 * i, static_cast<std::uint32_t>(fat_first + i));
            }
        }
        for(std::uint64_t d = 0; d < this->difat_sectors; ++d)
        {
            const std::uint64_t base = at(difat_first + d);
            for(std::uint32_t k = 0; k + 1 < this->per; ++k)
            {
                const std::uint64_t which = 109 + d * (this->per - 1) + k;
                Put32(out, base + 4ull * k, which < this->fat_sectors ? static_cast<std::uint32_t>(fat_first + which) : kFreeSector);
            }
            Put32(out, base + 4ull * (this->per - 1),
                  d + 1 == this->difat_sectors ? kEndOfChain : static_cast<std::uint32_t>(difat_first + d + 1));
        }
        // The header.
        static const std::uint8_t magic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        std::copy(magic, magic + 8, out.begin());
        Put16(out, 24, 0x3E);
        Put16(out, 26, static_cast<std::uint16_t>(this->options.version));
        Put16(out, 28, 0xFFFE);
        Put16(out, 30, this->options.version == 3 ? 9 : 12);
        Put16(out, 32, 6);
        Put32(out, 40, this->options.version == 3 ? 0 : static_cast<std::uint32_t>(this->dir_sectors));
        Put32(out, 44, static_cast<std::uint32_t>(this->fat_sectors));
        Put32(out, 48, dir_start);
        Put32(out, 56, kMiniCutoff);
        Put32(out, 60, mini_fat_start);
        Put32(out, 64, static_cast<std::uint32_t>(this->mini_fat_sectors));
        Put32(out, 68, this->difat_sectors > 0 ? static_cast<std::uint32_t>(difat_first) : kEndOfChain);
        Put32(out, 72, static_cast<std::uint32_t>(this->difat_sectors));
        for(std::uint64_t i = this->fat_sectors; i < 109; ++i)
        {
            Put32(out, 76 + 4 * i, kFreeSector);
        }
        return out;
    }

    const BuildNode &root;
    BuildOptions options;
    std::uint32_t sector = 512;
    std::uint32_t per = 128;
    std::vector<Placed> placed;
    std::map<std::uint32_t, std::vector<std::uint32_t>> sorted_children;
    std::uint64_t data_sector_count = 0;
    std::uint64_t mini_sector_count = 0;
    std::uint64_t container_sectors = 0;
    std::uint64_t mini_fat_sectors = 0;
    std::uint64_t dir_sectors = 0;
    std::uint64_t difat_sectors = 0;
    std::uint64_t fat_sectors = 0;
    std::uint64_t total_sectors = 0;
};
} // namespace detail

inline Bytes Build(const BuildNode &root, const BuildOptions &options = {})
{
    return detail::Builder(root, options).Run();
}

// A BuildNode tree holding what a package holds (for adding to or changing a
// sample before building it again).
namespace detail
{
inline void CopyChildren(const Package &package, std::uint32_t storage, BuildNode &into, bool skip_signatures)
{
    for(const std::uint32_t index : package.Children(storage))
    {
        const DirEntry &entry = package.Entries()[index];
        if(storage == 0 && skip_signatures && IsSignatureName(entry.name))
        {
            continue;
        }
        BuildNode node;
        node.name = entry.name;
        node.storage = entry.type != kTypeStream;
        node.class_id = entry.class_id;
        node.state = entry.state;
        node.created = entry.created;
        node.modified = entry.modified;
        if(!node.storage)
        {
            node.data = package.ReadStream(index);
        }
        else
        {
            CopyChildren(package, index, node, skip_signatures);
        }
        into.Add(std::move(node));
    }
}
} // namespace detail

inline BuildNode ToBuildTree(const Package &package, bool skip_signatures)
{
    BuildNode root = BuildNode::Storage(u"Root Entry", package.Entries()[0].class_id);
    root.state = package.Entries()[0].state;
    root.created = package.Entries()[0].created;
    root.modified = package.Entries()[0].modified;
    detail::CopyChildren(package, 0, root, skip_signatures);
    return root;
}

// Deterministic bytes: byte i is (i * 7 + seed) mod 256 with a bump every 251 bytes so repeats do not line up.
inline Bytes PatternBytes(std::size_t size, std::uint8_t seed = 3)
{
    Bytes out(size);
    for(std::size_t i = 0; i < size; ++i)
    {
        out[i] = static_cast<std::uint8_t>(i * 7 + seed + i / 251);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Shapes the tests build. Names are written with the clause "what it tests".

namespace shapes
{
inline ClassId Id(std::uint8_t seed)
{
    ClassId id{};
    for(std::size_t i = 0; i < id.size(); ++i)
    {
        id[i] = static_cast<std::uint8_t>(seed + 17 * i);
    }
    return id;
}

// Storages nested four deep, each with its own class identifier, streams of
// sizes around the 64-byte mini sector and the 4,096-byte cut-off, an empty
// stream and an empty storage; the root has a class identifier too.
inline BuildNode Nested()
{
    BuildNode root = BuildNode::Storage(u"Root Entry", Id(1));
    root.Add(BuildNode::Stream(u"top", PatternBytes(100, 1)));
    BuildNode &a = root.Add(BuildNode::Storage(u"A", Id(2)));
    a.Add(BuildNode::Stream(u"a1", PatternBytes(63, 2)));
    a.Add(BuildNode::Stream(u"a2", PatternBytes(64, 3)));
    BuildNode &b = a.Add(BuildNode::Storage(u"B", Id(3)));
    b.Add(BuildNode::Stream(u"b1", PatternBytes(65, 4)));
    b.Add(BuildNode::Stream(u"b0", Bytes()));
    BuildNode &c = b.Add(BuildNode::Storage(u"C", Id(4)));
    c.Add(BuildNode::Stream(u"c1", PatternBytes(4095, 5)));
    c.Add(BuildNode::Stream(u"c2", PatternBytes(4096, 6)));
    BuildNode &d = c.Add(BuildNode::Storage(u"D", Id(5)));
    d.Add(BuildNode::Stream(u"d1", PatternBytes(4097, 7)));
    d.Add(BuildNode::Storage(u"E", Id(6)));
    root.Add(BuildNode::Storage(u"Empty", Id(7)));
    return root;
}

// Names whose order by raw bytes of their UTF-16LE form differs from their
// order by the format's ordering: "B" before "a" by raw bytes, "a" before "B"
// by the format's; U+0102 and U+0201 swap because the low byte comes first;
// "a" is a prefix of "ab".
inline BuildNode Ordering()
{
    BuildNode root = BuildNode::Storage(u"Root Entry");
    std::uint8_t seed = 10;
    for(const Name &name : {Name(u"a"), Name(u"B"), Name(u"ab"), Name(u"_"), Name(u"Z"), Name(u"b2"),
                            Name(u"\u0102"), Name(u"\u0201"), Name(u"\u0102\u0201"), Name(u"\u0201\u0102")})
    {
        root.Add(BuildNode::Stream(name, PatternBytes(20 + seed, seed)));
        ++seed;
    }
    BuildNode &sub = root.Add(BuildNode::Storage(u"s"));
    for(const Name &name : {Name(u"x"), Name(u"X1"), Name(u"\u00e9"), Name(u"\u00c9z")})
    {
        sub.Add(BuildNode::Stream(name, PatternBytes(30 + seed, seed)));
        ++seed;
    }
    return root;
}

// Streams of the sizes where a stream moves between the mini stream and ordinary sectors.
inline BuildNode Sizes()
{
    BuildNode root = BuildNode::Storage(u"Root Entry", Id(9));
    std::uint8_t seed = 1;
    for(const std::size_t size : {1, 63, 64, 65, 511, 512, 513, 1000, 4094, 4095, 4096, 4097, 5000, 9000})
    {
        root.Add(BuildNode::Stream(Name(u"n") + Name(1, static_cast<char16_t>(u'a' + (seed % 26))) +
                                       Name(1, static_cast<char16_t>(u'a' + (seed / 26))),
                                   PatternBytes(size, seed)));
        ++seed;
    }
    return root;
}

// More entries than one directory sector holds (four with 512-byte sectors).
inline BuildNode ManyEntries(std::size_t count = 300)
{
    BuildNode root = BuildNode::Storage(u"Root Entry", Id(11));
    for(std::size_t i = 0; i < count; ++i)
    {
        const std::string text = "s" + std::to_string(1000 + i);
        root.Add(BuildNode::Stream(Name(text.begin(), text.end()), PatternBytes(10 + i % 150, static_cast<std::uint8_t>(i))));
    }
    return root;
}

struct Named
{
    std::string label;
    BuildNode root;
};

inline std::vector<Named> All()
{
    std::vector<Named> out;
    out.push_back({"nested storages with class identifiers", Nested()});
    out.push_back({"names that order differently by raw bytes", Ordering()});
    out.push_back({"sizes around the mini stream cut-off", Sizes()});
    out.push_back({"more entries than one directory sector", ManyEntries()});
    return out;
}
} // namespace shapes

} // namespace seedtest::cfb
