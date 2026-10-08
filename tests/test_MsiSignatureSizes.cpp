// Where installer signatures live, and reading them back by size.
//
// An installer signature smaller than the header's cut-off (4,096 bytes) is
// kept in the mini stream, one at or above it in ordinary sectors, and the
// stream's recorded size and the cut-off alone decide which. The samples
// signed by osslsigncode hold both kinds, in tree layouts where the signature
// entry sits among other entries.
//
// This file holds the reading half: the independent walk of CfbReference.hpp
// finds \005DigitalSignature by searching the directory with the format's
// ordering (never by listing it), and the library's ExtractSignature returns
// the same bytes. The sizes recorded in fixtures/msi-reference.txt (what libgsf
// reports for the stream) are the known answers for the size.

#include "CfbReference.hpp"
#include "MalformedInput.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <cstdint>
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
