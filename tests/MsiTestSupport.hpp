#pragma once

// Helpers shared by the installer signing tests: copies of the samples in a
// scratch folder, the recorded known answers of fixtures/msi-reference.txt, and
// before/after comparisons made with the independent reader of CfbReference.hpp.
//
// Every program that includes this header must expand
// SEED_DEFINE_HEAP_COUNTER() exactly once (see HeapCounter.hpp).

#include "CfbReference.hpp"
#include "MalformedInput.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace seedtest::msi
{

namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
using Bytes = std::vector<std::uint8_t>;

// The samples that hold no signature, and the ones another tool signed.
inline const std::vector<std::string> kUnsignedSamples = {"tiny.msi", "tiny-v4.msi", "nested.msi"};
inline const std::vector<std::string> kSignedSamples = {"tiny-osslsig-small.msi", "tiny-osslsig-large.msi",
                                                        "tiny-osslsig-dse.msi", "nested-osslsig.msi",
                                                        "two-neighbours.msi"};

// One line of msi-reference.txt split into words.
inline std::vector<std::vector<std::string>> ReferenceLines(const std::string &type)
{
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(seedtest::FixturePath("msi-reference.txt"));
    REQUIRE(in.good());
    std::string line;
    while(std::getline(in, line))
    {
        if(line.empty() || line[0] == '#')
        {
            continue;
        }
        std::istringstream fields(line);
        std::vector<std::string> words;
        std::string word;
        while(fields >> word)
        {
            words.push_back(word);
        }
        if(!words.empty() && words[0] == type)
        {
            rows.push_back(words);
        }
    }
    return rows;
}

// The fingerprint osslsigncode calculated for a sample (lower-case hex).
inline std::string RecordedFingerprint(const std::string &sample)
{
    for(const auto &row : ReferenceLines("fingerprint"))
    {
        if(row.size() >= 3 && row[1] == sample)
        {
            return row[2];
        }
    }
    FAIL("no recorded fingerprint for " << sample);
    return "";
}

inline Bytes FromHex(const std::string &hex)
{
    Bytes out;
    for(std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        out.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

inline std::string ToHexString(const Bytes &bytes)
{
    return cfb::ToHex(bytes.data(), bytes.size());
}

// Copies a sample into the scratch folder and returns the path of the copy.
inline std::string CopySample(const seedtest::ScratchDir &scratch, const std::string &sample,
                              const std::string &as = "")
{
    return sm::WriteScratch(scratch, as.empty() ? sample : as, sm::LoadSample(sample));
}

// Everything but the signature streams: names, bytes, class identifiers, state
// bits and times of every storage and stream.
inline std::vector<cfb::ListedEntry> Content(const Bytes &bytes)
{
    return cfb::Listing(cfb::Package(bytes), true);
}

// Fails the running test unless everything but the signature streams is equal.
inline void RequireSameContent(const Bytes &before, const Bytes &after)
{
    const std::string difference = cfb::FirstDifference(Content(before), Content(after));
    INFO("content changed: " << difference);
    REQUIRE(difference.empty());
}

// The bytes of the signature stream found by searching the directory with the
// format's ordering; empty optional when the search does not reach it.
inline std::optional<Bytes> FoundSignature(const Bytes &bytes)
{
    const cfb::Package package(bytes);
    const auto found = package.FindSignature();
    if(!found.has_value())
    {
        return std::nullopt;
    }
    return package.ReadStream(*found);
}

// The signature stream of a sample, read by the independent reader.
inline Bytes SampleSignature(const std::string &sample)
{
    const auto blob = FoundSignature(sm::LoadSample(sample));
    REQUIRE(blob.has_value());
    return *blob;
}

// Whether either signature stream is listed anywhere under the root.
inline bool HasExtendedStream(const Bytes &bytes)
{
    const cfb::Package package(bytes);
    for(const std::uint32_t index : package.Children(0))
    {
        if(package.Entries()[index].name == cfb::ExtendedSignatureName())
        {
            return true;
        }
    }
    return false;
}

// A header field of a compound file (little-endian, 32 bits).
inline std::uint32_t HeaderField(const Bytes &bytes, std::size_t offset)
{
    REQUIRE(offset + 4 <= bytes.size());
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline constexpr std::size_t kHeaderDirectorySectors = 40;
inline constexpr std::size_t kHeaderFatSectors = 44;
inline constexpr std::size_t kHeaderMiniFatStart = 60;
inline constexpr std::size_t kHeaderMiniFatSectors = 64;

// Every entry listed under a storage is also reached by searching that storage
// with the format's ordering (no enumeration).
inline void RequireAllFindable(const cfb::Package &package)
{
    for(std::uint32_t storage = 0; storage < package.Entries().size(); ++storage)
    {
        const cfb::DirEntry &parent = package.Entries()[storage];
        if(parent.type != cfb::kTypeStorage && parent.type != cfb::kTypeRoot)
        {
            continue;
        }
        for(const std::uint32_t child : package.Children(storage))
        {
            INFO("searching " << cfb::Display(parent.name) << " for " << cfb::Display(package.Entries()[child].name));
            const auto found = package.Find(storage, package.Entries()[child].name);
            REQUIRE(found.has_value());
            REQUIRE(*found == child);
        }
    }
}

namespace detail
{
inline void CollectFindable(const cfb::Package &package, std::uint32_t storage, const std::string &prefix,
                            std::set<std::string> &out, unsigned depth)
{
    if(depth > 64)
    {
        return;
    }
    for(const std::uint32_t child : package.Children(storage))
    {
        const cfb::DirEntry &entry = package.Entries()[child];
        const std::string path = prefix + "/" + cfb::Display(entry.name);
        const auto found = package.Find(storage, entry.name);
        if(found.has_value() && *found == child)
        {
            out.insert(path);
        }
        if(entry.type == cfb::kTypeStorage)
        {
            CollectFindable(package, child, path, out, depth + 1);
        }
    }
}
} // namespace detail

// The paths (outside the signature streams) that searching a storage with the
// format's ordering reaches.
inline std::set<std::string> FindablePaths(const cfb::Package &package)
{
    std::set<std::string> out;
    detail::CollectFindable(package, 0, "", out, 0);
    out.erase("/" + cfb::Display(cfb::SignatureName()));
    out.erase("/" + cfb::Display(cfb::ExtendedSignatureName()));
    return out;
}

// Every entry that a search reached before the operation is reached after it,
// and every entry of the result is reached. The second part matters for the
// packages another tool wrote with a storage out of the format's order
// (nested-osslsig.msi has the root entries "B" and "a" in raw byte order): the
// writer puts such a storage into the format's order, so nothing is lost and
// the entries that no search reached before are reachable after.
inline void RequireStillFindable(const cfb::Package &before, const cfb::Package &after)
{
    const std::set<std::string> was = FindablePaths(before);
    const std::set<std::string> is = FindablePaths(after);
    for(const std::string &path : was)
    {
        INFO("entry " << path << " was found by searching before and is not after");
        CHECK(is.count(path) == 1);
    }
    RequireAllFindable(after);
}

// The listing paths of the signature streams in a package.
inline std::vector<std::string> SignaturePaths(const Bytes &bytes)
{
    const cfb::Package package(bytes);
    std::vector<std::string> without;
    for(const cfb::ListedEntry &entry : cfb::Listing(package, true))
    {
        without.push_back(entry.path);
    }
    std::vector<std::string> only;
    for(const cfb::ListedEntry &entry : cfb::Listing(package, false))
    {
        if(std::find(without.begin(), without.end(), entry.path) == without.end())
        {
            only.push_back(entry.path);
        }
    }
    return only;
}

// Number of mini sectors the streams below the cut-off need, counting every
// stream of the package as listed by the independent reader.
inline std::uint64_t MiniSectorsNeeded(const cfb::Package &package)
{
    std::uint64_t total = 0;
    for(std::size_t i = 1; i < package.Entries().size(); ++i)
    {
        const cfb::DirEntry &entry = package.Entries()[i];
        if(entry.type == cfb::kTypeStream && entry.size > 0 && entry.size < package.Cutoff())
        {
            total += (entry.size + cfb::kMiniSectorSize - 1) / cfb::kMiniSectorSize;
        }
    }
    return total;
}

} // namespace seedtest::msi
