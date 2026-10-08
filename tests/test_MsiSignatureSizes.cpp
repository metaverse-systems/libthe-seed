// Where installer signatures live, and reading them back by size.
//
// An installer signature smaller than the header's cut-off (4,096 bytes) is
// kept in the mini stream, one at or above it in ordinary sectors, and the
// stream's recorded size and the cut-off alone decide which. The samples
// signed by osslsigncode hold both kinds, in tree layouts where the signature
// entry sits among other entries.
//
// The first half of this file is the reading half: the independent walk of CfbReference.hpp
// finds \005DigitalSignature by searching the directory with the format's
// ordering (never by listing it), and the library's ExtractSignature returns
// the same bytes. The sizes recorded in fixtures/msi-reference.txt (what libgsf
// reports for the stream) are the known answers for the size.
//
// The second half is the writing half: blobs of 1 to 50,000 bytes embedded by
// the library into the unsigned samples and into samples another tool signed,
// then read back by the library and by the independent reader (found by
// searching the directory with the format's ordering, never by listing),
// placed in the mini stream below the cut-off and in ordinary sectors at and
// above it, replaced across the cut-off in both directions and signed again.
// A blob of size zero is not a signature: it is refused and the file is left
// as it was.

#include "CfbReference.hpp"
#include "MalformedInput.hpp"
#include "MsiTestSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;

// Recorded size of the DigitalSignature stream per sample ("none" is absent).
const std::map<std::string, std::optional<std::uint64_t>> &RecordedSizes()
{
    static const auto table = [] {
        std::map<std::string, std::optional<std::uint64_t>> rows;
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
            std::string count;
            std::string size;
            fields >> type >> sample >> count >> size;
            if(type == "entries")
            {
                rows[sample] = size == "none" ? std::nullopt : std::optional<std::uint64_t>(std::stoull(size));
            }
        }
        return rows;
    }();
    return table;
}

struct Signed
{
    const char *sample;
    bool mini; // expected placement from the recorded size and the 4,096 cut-off
};

const Signed kSigned[] = {
    {"tiny-osslsig-small.msi", true},
    {"tiny-osslsig-large.msi", false},
    {"nested-osslsig.msi", true},
    {"two-neighbours.msi", true},
};

} // namespace

TEST_CASE("signature found by search and read by size equals the library", "[MsiSignatureSizes][read]")
{
    for(const Signed &item : kSigned)
    {
        INFO("sample " << item.sample);
        const Bytes bytes = sm::LoadSample(item.sample);
        const cfb::Package package(bytes);

        // The independent walk finds the entry by the format's ordering.
        const auto found = package.FindSignature();
        REQUIRE(found.has_value());
        const cfb::DirEntry &entry = package.Entries()[*found];

        // The recorded size is libgsf's; placement follows size and cut-off alone.
        const auto recorded = RecordedSizes().at(item.sample);
        REQUIRE(recorded.has_value());
        CHECK(entry.size == *recorded);
        CHECK(package.Cutoff() == 4096);
        CHECK(package.InMiniStream(*found) == (entry.size < package.Cutoff()));
        CHECK(package.InMiniStream(*found) == item.mini);

        const Bytes expected = package.ReadStream(*found);
        REQUIRE(expected.size() == *recorded);

        // The library returns the same bytes, and reports the signature present.
        const std::string path = seedtest::FixturePath(item.sample);
        CHECK(MsiSigner::HasEmbeddedSignature(path));
        const auto extracted = MsiSigner::ExtractSignature(path);
        REQUIRE(extracted.has_value());
        CHECK(extracted->size() == *recorded);
        CHECK(*extracted == expected);
    }
}

TEST_CASE("signature of two sizes read from a copy of the sample", "[MsiSignatureSizes][read]")
{
    // The same through a scratch copy, so the read does not depend on the
    // fixtures directory and the file is unchanged afterwards.
    for(const Signed &item : kSigned)
    {
        INFO("sample " << item.sample);
        const Bytes bytes = sm::LoadSample(item.sample);
        seedtest::ScratchDir scratch;
        const std::string path = sm::WriteScratch(scratch, "copy.msi", bytes);
        const cfb::Package package(bytes);
        const auto found = package.FindSignature();
        REQUIRE(found.has_value());

        const auto extracted = MsiSigner::ExtractSignature(path);
        REQUIRE(extracted.has_value());
        CHECK(*extracted == package.ReadStream(*found));
        sm::RequireUnchanged(path, bytes);
    }
}

TEST_CASE("samples without a signature have none to read", "[MsiSignatureSizes][read]")
{
    for(const char *sample : {"tiny.msi", "tiny-v4.msi", "nested.msi"})
    {
        INFO("sample " << sample);
        const Bytes bytes = sm::LoadSample(sample);
        CHECK_FALSE(cfb::Package(bytes).FindSignature().has_value());
        const std::string path = seedtest::FixturePath(sample);
        CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
        CHECK_FALSE(MsiSigner::ExtractSignature(path).has_value());
    }
}

TEST_CASE("large signature is read from ordinary sectors and small from the mini stream", "[MsiSignatureSizes][read]")
{
    // Each side of the cut-off, in an independently built package with the
    // signature entry in the middle of its storage.
    for(const std::size_t size : {1u, 100u, 1426u, 4095u, 4096u, 4097u, 20000u})
    {
        INFO("size " << size);
        cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
        root.Add(cfb::BuildNode::Stream(u"alpha", cfb::PatternBytes(70, 1)));
        root.Add(cfb::BuildNode::Stream(u"zzz-after", cfb::PatternBytes(5000, 2)));
        const Bytes blob = cfb::PatternBytes(size, 11);
        root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), blob));
        const Bytes bytes = cfb::Build(root);

        const cfb::Package package(bytes);
        const auto found = package.FindSignature();
        REQUIRE(found.has_value());
        CHECK(package.InMiniStream(*found) == (size < 4096));

        seedtest::ScratchDir scratch;
        const std::string path = sm::WriteScratch(scratch, "built.msi", bytes);
        CHECK(MsiSigner::HasEmbeddedSignature(path));
        const auto extracted = MsiSigner::ExtractSignature(path);
        REQUIRE(extracted.has_value());
        CHECK(*extracted == blob);
    }
}

namespace {

namespace msi = seedtest::msi;

const std::size_t kSizes[] = {1, 100, 1426, 4095, 4096, 4097, 20000, 50000};

// What must be true of a package after a blob was embedded into it.
void RequireEmbedded(const std::string &path, const Bytes &original, const Bytes &blob,
                     const std::string &fingerprint)
{
    const Bytes bytes = sm::ReadAll(path);
    INFO("blob of " << blob.size() << " bytes");

    // The library reads it back.
    CHECK(MsiSigner::HasEmbeddedSignature(path));
    const auto extracted = MsiSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    CHECK(*extracted == blob);

    // The independent reader finds it by searching and reads the same bytes.
    const cfb::Package package(bytes);
    const auto found = package.FindSignature();
    REQUIRE(found.has_value());
    const cfb::DirEntry &entry = package.Entries()[*found];
    CHECK(entry.size == blob.size());
    CHECK(package.ReadStream(*found) == blob);

    // Placement follows the size and the cut-off alone.
    CHECK(package.Cutoff() == 4096);
    CHECK(package.InMiniStream(*found) == (blob.size() < package.Cutoff()));
    if(blob.size() < package.Cutoff())
    {
        CHECK(package.Entries()[0].size >= (blob.size() + 63) / 64 * 64);
        CHECK(entry.start < package.Entries()[0].size / 64);
    }
    else
    {
        CHECK(static_cast<std::uint64_t>(entry.start) < package.FileSectorCount());
    }

    // Nothing else changed, and the layout and the fingerprint are the same.
    msi::RequireSameContent(original, bytes);
    CHECK(package.Version() == cfb::Package(original).Version());
    CHECK(package.SectorSize() == cfb::Package(original).SectorSize());
    CHECK(cfb::Fingerprint(package) == fingerprint);
    CHECK(msi::ToHexString(MsiSigner::ComputeAuthenticodeDigest(path).digest) == fingerprint);
    CHECK_FALSE(msi::HasExtendedStream(bytes));
}

} // namespace

TEST_CASE("blobs of every size are embedded and read back", "[MsiSignatureSizes][write]")
{
    seedtest::ScratchDir scratch("msi-sizes-embed");
    for(const char *sample : {"tiny.msi", "tiny-v4.msi", "nested.msi"})
    {
        for(const std::size_t size : kSizes)
        {
            INFO("sample " << sample << " blob of " << size << " bytes");
            const Bytes original = sm::LoadSample(sample);
            const std::string path = msi::CopySample(scratch, sample);
            const Bytes blob = cfb::PatternBytes(size, static_cast<std::uint8_t>(size));
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
            RequireEmbedded(path, original, blob, msi::RecordedFingerprint(sample));
        }
    }
}

TEST_CASE("a blob of size zero is refused and the file is unchanged", "[MsiSignatureSizes][write]")
{
    seedtest::ScratchDir scratch("msi-sizes-zero");
    for(const char *sample : {"tiny.msi", "tiny-v4.msi", "tiny-osslsig-small.msi"})
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const std::string path = msi::CopySample(scratch, sample);
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, Bytes()), std::runtime_error);
        sm::RequireUnchanged(path, original);
    }
}

TEST_CASE("blobs are embedded into samples another tool signed", "[MsiSignatureSizes][write]")
{
    seedtest::ScratchDir scratch("msi-sizes-replace-other");
    for(const std::string &sample : msi::kSignedSamples)
    {
        for(const std::size_t size : {100u, 4095u, 4096u, 20000u})
        {
            INFO("sample " << sample << " blob of " << size << " bytes");
            const Bytes original = sm::LoadSample(sample);
            const std::string path = msi::CopySample(scratch, sample);
            const Bytes blob = cfb::PatternBytes(size, 31);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
            RequireEmbedded(path, original, blob, msi::RecordedFingerprint(sample));
            // The other tool's signature is gone, and so is its extended stream.
            CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));
        }
    }
}

TEST_CASE("a signature replaced across the cut-off is read back in both directions", "[MsiSignatureSizes][write]")
{
    seedtest::ScratchDir scratch("msi-sizes-replace");
    const std::vector<std::vector<std::size_t>> sequences = {
        {100, 5000, 100},        {4095, 4096, 4095}, {4096, 4095, 4096}, {1, 50000, 1426, 4097, 4095},
        {50000, 100, 20000, 1},
    };
    for(const char *sample : {"tiny.msi", "tiny-v4.msi"})
    {
        for(const auto &sequence : sequences)
        {
            const Bytes original = sm::LoadSample(sample);
            const std::string path = msi::CopySample(scratch, sample);
            Bytes blob;
            for(const std::size_t size : sequence)
            {
                INFO("sample " << sample << " then " << size << " bytes");
                blob = cfb::PatternBytes(size, static_cast<std::uint8_t>(size + 1));
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                RequireEmbedded(path, original, blob, msi::RecordedFingerprint(sample));
            }
            // The package is what signing the unsigned original with the last blob gives: nothing is left over.
            const std::string direct = msi::CopySample(scratch, sample, "direct.msi");
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(direct, blob));
            CHECK(sm::ReadAll(path) == sm::ReadAll(direct));
        }
    }
}

TEST_CASE("signing again with the same then a larger and a smaller signature", "[MsiSignatureSizes][write]")
{
    seedtest::ScratchDir scratch("msi-sizes-again");
    for(const char *sample : {"tiny.msi", "tiny-v4.msi", "tiny-osslsig-large.msi"})
    {
        for(const std::size_t first_size : {100u, 4096u})
        {
            const Bytes original = sm::LoadSample(sample);
            const std::string path = msi::CopySample(scratch, sample);
            const Bytes first = cfb::PatternBytes(first_size, 5);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, first));
            const Bytes once = sm::ReadAll(path);

            // The same signature gives the same bytes.
            {
                INFO("sample " << sample << " first " << first_size);
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, first));
                CHECK(sm::ReadAll(path) == once);
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, first));
                CHECK(sm::ReadAll(path) == once);
            }
            // A larger and a smaller signature leave no trace of the first.
            {
                for(const std::size_t size : {first_size * 3 + 1, first_size / 2 + 1, first_size + 1, first_size - 1})
                {
                    INFO("sample " << sample << " first " << first_size << " then " << size);
                    const Bytes next = cfb::PatternBytes(size, 6);
                    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, next));
                    RequireEmbedded(path, original, next, msi::RecordedFingerprint(sample));
                    const std::string direct = msi::CopySample(scratch, sample, "direct.msi");
                    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(direct, next));
                    CHECK(sm::ReadAll(path) == sm::ReadAll(direct));
                }
            }
        }
    }
}

TEST_CASE("signature entry among neighbours is found by search after embedding", "[MsiSignatureSizes][write]")
{
    // Packages where the signature entry has neighbours on both sides, on one
    // side only, or is the top of the tree, in both sector sizes: the entry the
    // library writes is reached by searching in every one.
    seedtest::ScratchDir scratch("msi-sizes-tree");
    struct Shape
    {
        const char *label;
        cfb::Picker picker;
    };
    const Shape shapes[] = {{"balanced", cfb::BalancedPicker()},
                            {"left chain", cfb::LeftChainPicker()},
                            {"right chain", cfb::RightChainPicker()}};
    const cfb::Package base(sm::LoadSample("tiny.msi"));
    for(const unsigned version : {3u, 4u})
    {
        for(const Shape &shape : shapes)
        {
            for(const std::size_t size : {100u, 4096u})
            {
                INFO("version " << version << " " << shape.label << " blob of " << size << " bytes");
                cfb::BuildOptions options;
                options.version = version;
                options.picker = shape.picker;
                const Bytes original = cfb::Build(cfb::ToBuildTree(base, true), options);
                const std::string path = sm::WriteScratch(scratch, "tree.msi", original);
                const Bytes blob = cfb::PatternBytes(size, 12);
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                RequireEmbedded(path, original, blob, msi::RecordedFingerprint("tiny.msi"));
            }
        }
    }
}
