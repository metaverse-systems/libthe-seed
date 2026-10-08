// The installer fingerprint (Authenticode digest of a Windows Installer
// package) against values recorded from a standard verifier.
//
// fixtures/msi-reference.txt holds, for every installer sample, the value
// osslsigncode calculates for the package without its signature
// ("Calculated DigitalSignature"). The tests read those values, so they need
// neither osslsigncode nor any new tool. Other line types of the file (stored
// digests, entry counts, blobs) are read only where a test uses them.
//
// The independent implementation in CfbReference.hpp (its own reader, ordering
// and SHA-256) is run on every sample first. It is the calibration: it must
// reproduce each recorded value before it is trusted for packages that no
// standard tool wrote (nested storages four deep, names that order differently
// by raw bytes, many entries, 4,096-byte sectors). The values recorded for those
// shapes where osslsigncode 2.14 can process them agreed with it; osslsigncode
// does not process storages nested two deep, so that shape is reference only.
//
// Each sample needs the recorded value from the library as well; a sample whose
// value differs is a failure of its own case.

#include "CfbReference.hpp"
#include "MalformedInput.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;

// SHA-256 of no bytes: what a fingerprint of nothing looks like.
const char *const kEmptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

struct EntryCount
{
    std::size_t below_root = 0;
    std::optional<std::uint64_t> signature_size;
};

struct Reference
{
    std::vector<std::pair<std::string, std::string>> fingerprints; // sample, hex, in file order
    std::map<std::string, EntryCount> entries;
};

const Reference &Recorded()
{
    static const Reference table = [] {
        Reference rows;
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
            std::string type;
            std::string sample;
            fields >> type >> sample;
            if(type == "fingerprint")
            {
                std::string digest;
                REQUIRE(static_cast<bool>(fields >> digest));
                rows.fingerprints.emplace_back(sample, digest);
            }
            else if(type == "entries")
            {
                std::string count;
                std::string size;
                REQUIRE(static_cast<bool>(fields >> count >> size));
                EntryCount entry;
                entry.below_root = static_cast<std::size_t>(std::stoul(count));
                if(size != "none")
                {
                    entry.signature_size = std::stoull(size);
                }
                rows.entries[sample] = entry;
            }
            // Any other line type is not used here.
        }
        return rows;
    }();
    return table;
}

std::vector<std::string> Samples()
{
    std::vector<std::string> names;
    for(const auto &row : Recorded().fingerprints)
    {
        names.push_back(row.first);
    }
    return names;
}

// The `signed-out <sample> <blob> <sha256>` lines, as (sample, blob, hash).
std::vector<std::vector<std::string>> ReferenceSignedOut()
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
        std::string type;
        std::string sample;
        std::string blob;
        std::string hash;
        fields >> type;
        if(type != "signed-out")
        {
            continue;
        }
        REQUIRE(static_cast<bool>(fields >> sample >> blob >> hash));
        rows.push_back({sample, blob, hash});
    }
    return rows;
}

const std::string &RecordedFingerprint(const std::string &sample)
{
    for(const auto &row : Recorded().fingerprints)
    {
        if(row.first == sample)
        {
            return row.second;
        }
    }
    INFO("no recorded fingerprint for " << sample);
    REQUIRE(false);
    static const std::string none;
    return none;
}

std::string LibraryDigest(const std::string &path)
{
    const auto result = MsiSigner::ComputeAuthenticodeDigest(path);
    return cfb::ToHex(result.digest.data(), result.digest.size());
}

// Streams (not storages) below the root, signature streams of the root left out.
void CollectStreams(const cfb::Package &package, std::uint32_t storage, std::vector<std::uint32_t> &out)
{
    for(const std::uint32_t index : package.Children(storage))
    {
        const cfb::DirEntry &entry = package.Entries()[index];
        if(storage == 0 && cfb::IsSignatureName(entry.name))
        {
            continue;
        }
        if(entry.type == cfb::kTypeStream)
        {
            out.push_back(index);
        }
        else
        {
            CollectStreams(package, index, out);
        }
    }
}

std::size_t CountEntries(const cfb::Package &package, std::uint32_t storage)
{
    std::size_t count = 0;
    for(const std::uint32_t index : package.Children(storage))
    {
        ++count;
        if(package.Entries()[index].type != cfb::kTypeStream)
        {
            count += CountEntries(package, index);
        }
    }
    return count;
}

// The samples that carry a signature made by another tool.
const std::vector<std::string> kSignedSamples = {"tiny-osslsig-small.msi", "tiny-osslsig-large.msi",
                                                 "tiny-osslsig-dse.msi", "nested-osslsig.msi",
                                                 "two-neighbours.msi"};

struct Layout
{
    const char *label;
    unsigned version;
    cfb::Picker picker;
};

std::vector<Layout> Layouts()
{
    return {{"version 3 balanced", 3, cfb::BalancedPicker()},
            {"version 3 left chain", 3, cfb::LeftChainPicker()},
            {"version 3 right chain", 3, cfb::RightChainPicker()},
            {"version 4 balanced", 4, cfb::BalancedPicker()},
            {"version 4 right chain", 4, cfb::RightChainPicker()}};
}

} // namespace

TEST_CASE("fingerprint file lists every installer sample", "[MsiDigestReference]")
{
    const std::vector<std::string> names = Samples();
    for(const char *expected : {"tiny.msi", "tiny-v4.msi", "tiny-osslsig-small.msi", "tiny-osslsig-large.msi",
                                "tiny-osslsig-dse.msi", "nested.msi", "nested-osslsig.msi",
                                "two-neighbours.msi", "legacy-the-seed-0.6.0.msi"})
    {
        INFO("sample missing from msi-reference.txt: " << expected);
        CHECK(std::find(names.begin(), names.end(), expected) != names.end());
    }
}

TEST_CASE("calibration: the independent implementation reproduces the recorded fingerprint",
          "[MsiDigestReference]")
{
    const std::string sample = GENERATE_COPY(Catch::Generators::from_range(Samples()));
    INFO("sample " << sample);
    const cfb::Package package(sm::LoadSample(sample));
    CHECK(cfb::Fingerprint(package) == RecordedFingerprint(sample));
}

TEST_CASE("calibration: the independent reader agrees with the recorded entry counts and signature sizes",
          "[MsiDigestReference]")
{
    for(const auto &row : Recorded().entries)
    {
        if(row.first == "legacy-the-seed-0.6.0.msi")
        {
            // Damaged by an earlier version: its signature stream does not
            // read, and the other tools disagree about what it holds.
            continue;
        }
        INFO("sample " << row.first);
        const cfb::Package package(sm::LoadSample(row.first));
        CHECK(CountEntries(package, 0) == row.second.below_root);
        const auto found = package.FindSignature();
        if(row.second.signature_size)
        {
            REQUIRE(found.has_value());
            CHECK(package.Entries()[*found].size == *row.second.signature_size);
            CHECK(package.ReadStream(*found).size() == *row.second.signature_size);
        }
        else
        {
            CHECK_FALSE(found.has_value());
        }
    }
}

TEST_CASE("calibration: the independent builder and reader round trip every shape", "[MsiDigestReference]")
{
    for(const auto &shape : cfb::shapes::All())
    {
        for(const auto &layout : Layouts())
        {
            INFO(shape.label << " in " << layout.label);
            cfb::BuildOptions options;
            options.version = layout.version;
            options.picker = layout.picker;
            const cfb::Package package(cfb::Build(shape.root, options));
            CHECK(package.Version() == layout.version);
            // Whatever was built is found by search and holds the bytes and class identifiers given.
            std::vector<std::pair<std::uint32_t, const cfb::BuildNode *>> pending = {{0, &shape.root}};
            while(!pending.empty())
            {
                const auto [storage, node] = pending.back();
                pending.pop_back();
                CHECK(package.Entries()[storage].class_id == node->class_id);
                CHECK(package.Children(storage).size() == node->children.size());
                for(const cfb::BuildNode &child : node->children)
                {
                    INFO("child " << cfb::Display(child.name));
                    const auto found = package.Find(storage, child.name);
                    REQUIRE(found.has_value());
                    CHECK(package.Entries()[*found].class_id == child.class_id);
                    if(child.storage)
                    {
                        pending.emplace_back(*found, &child);
                    }
                    else
                    {
                        CHECK(package.ReadStream(*found) == child.data);
                        CHECK(package.InMiniStream(*found) == (child.data.size() < cfb::kMiniCutoff));
                    }
                }
            }
        }
    }
}

TEST_CASE("calibration: the independent builder writes an extended index for a large package",
          "[MsiDigestReference]")
{
    // 7.3 MB in 512-byte sectors needs more than the 109 index sectors the header holds.
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
    root.Add(cfb::BuildNode::Stream(u"small", cfb::PatternBytes(300, 1)));
    root.Add(cfb::BuildNode::Stream(u"big", cfb::PatternBytes(7'300'000, 2)));
    const cfb::Package package(cfb::Build(root));
    CHECK(package.FatSectorCount() > 109);
    CHECK(package.DifatSectorCount() >= 1);
    const auto found = package.Find(0, u"big");
    REQUIRE(found.has_value());
    CHECK(package.ReadStream(*found) == cfb::PatternBytes(7'300'000, 2));
}

TEST_CASE("fingerprint equals the recorded osslsigncode value for every sample", "[MsiDigestReference]")
{
    const std::string sample = GENERATE_COPY(Catch::Generators::from_range(Samples()));
    INFO("sample " << sample);
    std::string digest;
    REQUIRE_NOTHROW(digest = LibraryDigest(seedtest::FixturePath(sample)));
    CHECK(digest == RecordedFingerprint(sample));
}

TEST_CASE("fingerprint is the same in both layouts", "[MsiDigestReference]")
{
    const std::string version3 = LibraryDigest(seedtest::FixturePath("tiny.msi"));
    const std::string version4 = LibraryDigest(seedtest::FixturePath("tiny-v4.msi"));
    CHECK(version3 == version4);
    CHECK(version3 == RecordedFingerprint("tiny.msi"));
    CHECK(version4 == RecordedFingerprint("tiny-v4.msi"));
}

TEST_CASE("fingerprint is the same before signing and after signing by another tool", "[MsiDigestReference]")
{
    const std::string unsigned_value = RecordedFingerprint("tiny.msi");
    for(const char *signed_sample : {"tiny-osslsig-small.msi", "tiny-osslsig-large.msi", "tiny-osslsig-dse.msi"})
    {
        INFO("signed sample " << signed_sample);
        CHECK(LibraryDigest(seedtest::FixturePath(signed_sample)) == unsigned_value);
    }
    CHECK(LibraryDigest(seedtest::FixturePath("nested-osslsig.msi")) == RecordedFingerprint("nested.msi"));
    CHECK(LibraryDigest(seedtest::FixturePath("tiny.msi")) == unsigned_value);
}

TEST_CASE("fingerprint is the same after removing a signature", "[MsiDigestReference]")
{
    seedtest::ScratchDir scratch("msi-digest-strip");
    for(const std::string &sample : kSignedSamples)
    {
        INFO("sample " << sample);
        const std::string path = sm::WriteScratch(scratch, sample, sm::LoadSample(sample));
        REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
        CHECK(LibraryDigest(path) == RecordedFingerprint(sample));
    }
}

TEST_CASE("fingerprint is the same after the library signs", "[MsiDigestReference]")
{
    seedtest::ScratchDir scratch("msi-digest-embed");
    for(const char *sample : {"tiny.msi", "tiny-v4.msi", "nested.msi"})
    {
        for(const std::size_t size : {100u, 1426u, 6000u})
        {
            INFO("sample " << sample << " blob of " << size << " bytes");
            const std::string path = sm::WriteScratch(scratch, sample, sm::LoadSample(sample));
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(size, 9)));
            CHECK(LibraryDigest(path) == RecordedFingerprint(sample));
        }
    }
}

TEST_CASE("one changed byte of one stream changes the fingerprint", "[MsiDigestReference]")
{
    seedtest::ScratchDir scratch("msi-digest-flip");
    for(const char *sample : {"tiny.msi", "tiny-v4.msi", "nested.msi", "tiny-osslsig-large.msi"})
    {
        const Bytes original = sm::LoadSample(sample);
        const cfb::Package package(original);
        const std::string recorded = RecordedFingerprint(sample);
        std::vector<std::uint32_t> streams;
        CollectStreams(package, 0, streams);
        REQUIRE(!streams.empty());
        std::size_t changed = 0;
        for(const std::uint32_t index : streams)
        {
            const auto extents = package.StreamExtents(index);
            if(extents.empty())
            {
                continue;
            }
            for(const std::uint64_t at : {extents.front().offset, extents.back().offset + extents.back().length - 1})
            {
                INFO(sample << " stream " << cfb::Display(package.Entries()[index].name) << " byte at file offset " << at);
                Bytes damaged = original;
                damaged[at] ^= 0x01;
                const std::string expected = cfb::Fingerprint(damaged);
                CHECK(expected != recorded); // the independent value moves
                const std::string path = sm::WriteScratch(scratch, "flipped.msi", damaged);
                const std::string actual = LibraryDigest(path);
                CHECK(actual != recorded);   // the library's moves off the recorded value
                CHECK(actual == expected);   // and to the same new value
                ++changed;
            }
        }
        CHECK(changed >= 2);
    }
}

TEST_CASE("the signature streams do not contribute to the fingerprint", "[MsiDigestReference]")
{
    seedtest::ScratchDir scratch("msi-digest-sig");
    SECTION("changing a byte inside the signature stream of a signed sample")
    {
        for(const std::string &sample : kSignedSamples)
        {
            const Bytes original = sm::LoadSample(sample);
            const cfb::Package package(original);
            const auto found = package.FindSignature();
            REQUIRE(found.has_value());
            const auto extents = package.StreamExtents(*found);
            REQUIRE(!extents.empty());
            for(const std::uint64_t at : {extents.front().offset, extents.back().offset + extents.back().length - 1})
            {
                INFO(sample << " signature byte at file offset " << at);
                Bytes damaged = original;
                damaged[at] ^= 0xFF;
                CHECK(cfb::Fingerprint(damaged) == RecordedFingerprint(sample));
                const std::string path = sm::WriteScratch(scratch, "sig-flipped.msi", damaged);
                CHECK(LibraryDigest(path) == RecordedFingerprint(sample));
            }
        }
    }
    SECTION("signature streams of every size in every tree position of a rebuilt sample")
    {
        const cfb::Package base(sm::LoadSample("tiny.msi"));
        const std::string recorded = RecordedFingerprint("tiny.msi");
        for(const auto &layout : Layouts())
        {
            for(const std::size_t size : {1u, 100u, 1444u, 4095u, 4096u, 5000u})
            {
                cfb::BuildNode root = cfb::ToBuildTree(base, true);
                root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), cfb::PatternBytes(size, 5)));
                if(size % 2 == 0)
                {
                    root.Add(cfb::BuildNode::Stream(cfb::ExtendedSignatureName(), cfb::PatternBytes(32, 6)));
                }
                cfb::BuildOptions options;
                options.version = layout.version;
                options.picker = layout.picker;
                INFO(layout.label << " signature of " << size << " bytes");
                const Bytes built = cfb::Build(root, options);
                CHECK(cfb::Fingerprint(built) == recorded);
                const std::string path = sm::WriteScratch(scratch, "rebuilt.msi", built);
                CHECK(LibraryDigest(path) == recorded);
            }
        }
        // The signature stream as the top of the search tree, with neighbours on both sides.
        cfb::BuildNode root = cfb::ToBuildTree(base, true);
        root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), cfb::PatternBytes(1444, 5)));
        cfb::BuildOptions options;
        options.picker = cfb::TopPicker(cfb::SignatureName());
        const std::string path = sm::WriteScratch(scratch, "top.msi", cfb::Build(root, options));
        CHECK(LibraryDigest(path) == recorded);
    }
}

TEST_CASE("fingerprint of generated shapes equals the independent value", "[MsiDigestReference]")
{
    seedtest::ScratchDir scratch("msi-digest-shapes");
    for(const auto &shape : cfb::shapes::All())
    {
        std::string first;
        for(const auto &layout : Layouts())
        {
            INFO(shape.label << " in " << layout.label);
            cfb::BuildOptions options;
            options.version = layout.version;
            options.picker = layout.picker;
            const Bytes built = cfb::Build(shape.root, options);
            const std::string expected = cfb::Fingerprint(built);
            if(first.empty())
            {
                first = expected;
            }
            CHECK(expected == first); // the value does not depend on layout or tree shape
            const std::string path = sm::WriteScratch(scratch, "shape.msi", built);
            std::string actual;
            REQUIRE_NOTHROW(actual = LibraryDigest(path));
            CHECK(actual == expected);
        }
    }
}

TEST_CASE("a damaged package is rejected and never given a fingerprint of nothing", "[MsiDigestReference]")
{
    // The messages are those of the rule for damaged packages (see test_MalformedMsi).
    const Bytes original = sm::LoadSample("tiny.msi");
    const cfb::Package package(original);
    seedtest::ScratchDir scratch("msi-digest-damaged");

    // A stream below the cut-off that is not empty, and the mini-FAT entry of its first mini sector.
    std::uint32_t mini_entry = 0;
    for(std::uint32_t index = 1; index < package.Entries().size(); ++index)
    {
        const cfb::DirEntry &entry = package.Entries()[index];
        if(entry.type == cfb::kTypeStream && entry.size > 100 && entry.size < cfb::kMiniCutoff)
        {
            mini_entry = index;
            break;
        }
    }
    REQUIRE(mini_entry != 0);
    const std::uint32_t mini_fat_sector = sm::detail::GetLE<std::uint32_t>(original, 60);

    struct Case
    {
        const char *label;
        const char *keyword;
        Bytes bytes;
    };
    std::vector<Case> cases;
    cases.push_back({"shorter than the header", "too small", sm::Truncate(original, 100)});
    {
        Bytes bytes = original;
        sm::PatchLE<std::uint16_t>(bytes, 30, 10);
        cases.push_back({"sector size exponent 10 in a version 3 header", "sector size exponent", bytes});
    }
    {
        Bytes bytes = original;
        sm::PatchLE<std::uint32_t>(bytes, package.EntryOffset(mini_entry) + 116, 0x00FFFFF0u);
        cases.push_back({"stream starting past the end of the mini stream", "past the end", bytes});
    }
    {
        Bytes bytes = original;
        sm::PatchLE<std::uint32_t>(bytes, package.EntryOffset(mini_entry) + 120, 4095u);
        cases.push_back({"stream size larger than its chain", "exceeds", bytes});
    }
    {
        Bytes bytes = original;
        const std::uint32_t first = package.Entries()[mini_entry].start;
        sm::PatchLE<std::uint32_t>(bytes, (static_cast<std::uint64_t>(mini_fat_sector) + 1) * 512 + 4ull * first, first);
        cases.push_back({"mini sector chain that loops", "loop", bytes});
    }

    for(const Case &damaged : cases)
    {
        INFO(damaged.label);
        const std::string path = sm::WriteScratch(scratch, "damaged.msi", damaged.bytes);
        sm::RequireRejected(
            [&] {
                const std::string digest = LibraryDigest(path);
                INFO("accepted, giving " << digest);
                CHECK(digest != kEmptyHash);
            },
            "MSI", damaged.keyword, damaged.bytes.size());
        sm::RequireUnchanged(path, damaged.bytes);
    }
}

TEST_CASE("the package the library writes has the recorded hash", "[MsiDigestReference][signed-out]")
{
    // Lines `signed-out <sample> <blob> <sha256>` of msi-reference.txt: the
    // SHA-256 of the whole package the library writes when it signs <sample>
    // with <blob>, recorded after osslsigncode verified that exact file. <blob>
    // is either the name of a signed sample (its signature stream is the blob)
    // or `pattern-<size>` (the first `size` bytes of PatternBytes with seed 9).
    // The lines are written when the writer exists; a file without them has
    // nothing to compare, which is said in the output and is not a failure.
    seedtest::ScratchDir scratch("msi-digest-signed-out");
    const auto rows = ReferenceSignedOut();
    if(rows.empty())
    {
        WARN("msi-reference.txt holds no signed-out lines yet: they are recorded with the writer");
    }
    for(const auto &row : rows)
    {
        const std::string &sample = row[0];
        const std::string &blob_name = row[1];
        const std::string &recorded_hash = row[2];
        INFO("sample " << sample << " blob " << blob_name);

        Bytes blob;
        if(blob_name.rfind("pattern-", 0) == 0)
        {
            blob = cfb::PatternBytes(static_cast<std::size_t>(std::stoull(blob_name.substr(8))), 9);
        }
        else
        {
            const auto found = cfb::Package(sm::LoadSample(blob_name)).FindSignature();
            REQUIRE(found.has_value());
            blob = cfb::Package(sm::LoadSample(blob_name)).ReadStream(*found);
        }

        const std::string path = sm::WriteScratch(scratch, sample, sm::LoadSample(sample));
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        CHECK(cfb::Sha256Hex(sm::ReadAll(path)) == recorded_hash);

        // The same input always gives the same bytes.
        const std::string again = sm::WriteScratch(scratch, "again-" + sample, sm::LoadSample(sample));
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(again, blob));
        CHECK(sm::ReadAll(again) == sm::ReadAll(path));
    }
}
