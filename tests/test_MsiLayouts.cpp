// The package the library writes when it signs or strips an installer, for
// shapes that no sample covers.
//
// Packages are built by the independent builder of CfbReference.hpp (both
// sector sizes, tree shapes, nested storages, class identifiers, many entries,
// large streams, an extended index), signed or stripped by the library, and
// read back by the independent reader. What must hold after every operation:
//
//   - every storage and stream other than the two signature streams has the
//     same name, bytes, class identifier, state bits and times;
//   - searching each storage with the format's ordering finds every entry that
//     listing it finds;
//   - the version, the sector size and the header class identifier are kept;
//   - the directory holds no free entries beyond its last sector, and a
//     version 4 header records the number of directory sectors (version 3
//     records 0);
//   - the mini stream holds exactly the streams below the cut-off, its size is
//     recorded in the root entry, and the mini allocation index is as long as
//     they need; a package without such streams has none, and one that gains
//     the first gets it;
//   - the extended index exists exactly when the allocation index needs more
//     than 109 sectors;
//   - the output is no larger than the independent builder's for the same
//     content, and the same input always gives the same bytes.
//
// A refusal leaves the file byte for byte as it was. The format limit on the
// number of sectors (4,294,967,290) needs a package of terabytes and is not
// reached here; the refusals tested are the structural ones (a stream whose
// chain disagrees with its size, a sector size the library does not write, two
// entries with the same name in one storage, an empty signature).

#include "MsiTestSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::msi::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
namespace msi = seedtest::msi;

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

Bytes BuildWith(const cfb::BuildNode &root, const Layout &layout)
{
    cfb::BuildOptions options;
    options.version = layout.version;
    options.picker = layout.picker;
    return cfb::Build(root, options);
}

std::uint64_t CeilDiv(std::uint64_t a, std::uint64_t b)
{
    return (a + b - 1) / b;
}

// Every entry listed under a storage is also reached by searching it.
void RequireAllFindable(const cfb::Package &package)
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

// The directory is as short as it can be; version 4 records its sector count.
void RequireDirectoryExact(const Bytes &bytes)
{
    const cfb::Package package(bytes);
    std::uint64_t used = 0;
    for(const cfb::DirEntry &entry : package.Entries())
    {
        used += entry.type != cfb::kTypeUnused ? 1 : 0;
    }
    const std::uint64_t per_sector = package.SectorSize() / 128;
    INFO(used << " used entries, " << package.Entries().size() << " slots");
    REQUIRE(package.Entries().size() == CeilDiv(used, per_sector) * per_sector);
    const std::uint32_t recorded = msi::HeaderField(bytes, msi::kHeaderDirectorySectors);
    if(package.Version() == 4)
    {
        REQUIRE(recorded == package.Entries().size() / per_sector);
    }
    else
    {
        REQUIRE(recorded == 0);
    }
}

// The mini stream is exactly as large as the streams below the cut-off need.
void RequireMiniStreamExact(const Bytes &bytes)
{
    const cfb::Package package(bytes);
    const std::uint64_t mini = msi::MiniSectorsNeeded(package);
    const cfb::DirEntry &root = package.Entries()[0];
    const std::uint64_t index_entries_per_sector = package.SectorSize() / 4;
    INFO(mini << " mini sectors needed");
    REQUIRE(root.size == mini * cfb::kMiniSectorSize);
    if(mini == 0)
    {
        REQUIRE(root.start == cfb::kEndOfChain);
        REQUIRE(msi::HeaderField(bytes, msi::kHeaderMiniFatSectors) == 0);
        REQUIRE(msi::HeaderField(bytes, msi::kHeaderMiniFatStart) == cfb::kEndOfChain);
    }
    else
    {
        REQUIRE(root.start != cfb::kEndOfChain);
        REQUIRE(msi::HeaderField(bytes, msi::kHeaderMiniFatSectors) == CeilDiv(mini, index_entries_per_sector));
    }
}

// Structure, content and size checks that hold after any operation.
void RequireCanonical(const Bytes &original, const Bytes &after, const std::string &label)
{
    INFO(label);
    const cfb::Package before_package(original);
    const cfb::Package package(after);
    REQUIRE(package.Version() == before_package.Version());
    REQUIRE(package.SectorSize() == before_package.SectorSize());
    REQUIRE(package.Entries()[0].class_id == before_package.Entries()[0].class_id);
    REQUIRE(after.size() % package.SectorSize() == 0);
    msi::RequireSameContent(original, after);
    RequireAllFindable(package);
    RequireDirectoryExact(after);
    RequireMiniStreamExact(after);
}

void RequireSignatureIs(const std::string &path, const Bytes &blob)
{
    const Bytes bytes = sm::ReadAll(path);
    const auto found = msi::FoundSignature(bytes);
    REQUIRE(found.has_value());
    REQUIRE(*found == blob);
    const auto extracted = MsiSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    REQUIRE(*extracted == blob);
}

// The same content built again, with a signature stream, by the independent builder.
std::size_t IndependentSize(const Bytes &signed_bytes, const Layout &layout)
{
    const cfb::Package package(signed_bytes);
    cfb::BuildOptions options;
    options.version = package.Version();
    options.picker = layout.picker;
    return cfb::Build(cfb::ToBuildTree(package, false), options).size();
}

} // namespace

TEST_CASE("the writer keeps every entry of generated shapes in every layout", "[MsiLayouts][keep]")
{
    seedtest::ScratchDir scratch("msi-layouts-keep");
    for(const auto &shape : cfb::shapes::All())
    {
        for(const Layout &layout : Layouts())
        {
            for(const std::size_t size : {100u, 5000u})
            {
                const std::string label = shape.label + std::string(" in ") + layout.label + " with " +
                                          std::to_string(size) + " bytes";
                INFO(label);
                const Bytes original = BuildWith(shape.root, layout);
                const std::string path = sm::WriteScratch(scratch, "shape.msi", original);
                const Bytes blob = cfb::PatternBytes(size, 21);

                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                const Bytes signed_bytes = sm::ReadAll(path);
                RequireCanonical(original, signed_bytes, label + " signed");
                RequireSignatureIs(path, blob);
                CHECK(cfb::Fingerprint(signed_bytes) == cfb::Fingerprint(original));
                CHECK(cfb::CountBelowRoot(cfb::Package(signed_bytes)) ==
                      cfb::CountBelowRoot(cfb::Package(original)) + 1);
                CHECK(signed_bytes.size() <= IndependentSize(signed_bytes, layout));

                REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
                const Bytes stripped = sm::ReadAll(path);
                RequireCanonical(original, stripped, label + " stripped");
                CHECK_FALSE(msi::FoundSignature(stripped).has_value());
                CHECK(cfb::CountBelowRoot(cfb::Package(stripped)) == cfb::CountBelowRoot(cfb::Package(original)));
                CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));

                // The output does not depend on what was signed before: the same
                // package signed with another blob and stripped is the same bytes.
                const std::string other = sm::WriteScratch(scratch, "shape-other.msi", original);
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(other, cfb::PatternBytes(size == 100u ? 4097u : 99u, 22)));
                REQUIRE_NOTHROW(MsiSigner::StripSignature(other));
                CHECK(sm::ReadAll(other) == stripped);
            }
        }
    }
}

TEST_CASE("the writer keeps the layout of the samples in versions 3 and 4", "[MsiLayouts][keep]")
{
    seedtest::ScratchDir scratch("msi-layouts-samples");
    for(const std::string &sample : {std::string("tiny.msi"), std::string("tiny-v4.msi"), std::string("nested.msi")})
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const cfb::Package before(original);
        const std::string path = msi::CopySample(scratch, sample);
        const Bytes blob = cfb::PatternBytes(1426, 5);

        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        const Bytes signed_bytes = sm::ReadAll(path);
        RequireCanonical(original, signed_bytes, sample + " signed");
        const cfb::Package after(signed_bytes);
        CHECK(after.Version() == (sample == "tiny-v4.msi" ? 4u : 3u));
        CHECK(after.SectorSize() == (sample == "tiny-v4.msi" ? 4096u : 512u));
        CHECK(after.Cutoff() == 4096);
        RequireSignatureIs(path, blob);

        REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
        RequireCanonical(original, sm::ReadAll(path), sample + " stripped");
    }
}

TEST_CASE("a version 4 package records its directory sector count as the directory grows and shrinks",
          "[MsiLayouts][directory]")
{
    seedtest::ScratchDir scratch("msi-layouts-v4dir");
    // 31 children and the root fill one 4,096-byte directory sector (32 entries);
    // the signature is the 33rd.
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
    for(std::size_t i = 0; i < 31; ++i)
    {
        const std::string text = "stream" + std::to_string(100 + i);
        root.Add(cfb::BuildNode::Stream(cfb::Name(text.begin(), text.end()), cfb::PatternBytes(40 + i, static_cast<std::uint8_t>(i))));
    }
    cfb::BuildOptions options;
    options.version = 4;
    const Bytes original = cfb::Build(root, options);
    REQUIRE(cfb::Package(original).Entries().size() == 32);
    REQUIRE(msi::HeaderField(original, msi::kHeaderDirectorySectors) == 1);

    const std::string path = sm::WriteScratch(scratch, "v4dir.msi", original);
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(300, 1)));
    const Bytes grown = sm::ReadAll(path);
    CHECK(cfb::Package(grown).Entries().size() == 64);
    CHECK(msi::HeaderField(grown, msi::kHeaderDirectorySectors) == 2);
    RequireCanonical(original, grown, "grown");

    REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
    const Bytes shrunk = sm::ReadAll(path);
    CHECK(cfb::Package(shrunk).Entries().size() == 32);
    CHECK(msi::HeaderField(shrunk, msi::kHeaderDirectorySectors) == 1);
    RequireCanonical(original, shrunk, "shrunk");
}

TEST_CASE("the directory grows by a sector when the signature does not fit and is not left with free entries",
          "[MsiLayouts][directory]")
{
    seedtest::ScratchDir scratch("msi-layouts-dir");
    // The root and three streams fill the four entries of a 512-byte directory sector.
    for(const std::size_t children : {3u, 4u, 6u, 7u, 8u, 300u})
    {
        INFO(children << " children");
        cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
        for(std::size_t i = 0; i < children; ++i)
        {
            const std::string text = "item" + std::to_string(1000 + i);
            root.Add(cfb::BuildNode::Stream(cfb::Name(text.begin(), text.end()), cfb::PatternBytes(10 + i % 90, static_cast<std::uint8_t>(i))));
        }
        const Bytes original = cfb::Build(root);
        const std::string path = sm::WriteScratch(scratch, "dir.msi", original);
        const std::size_t slots_before = cfb::Package(original).Entries().size();

        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(200, 4)));
        const Bytes signed_bytes = sm::ReadAll(path);
        RequireCanonical(original, signed_bytes, "signed");
        CHECK(cfb::Package(signed_bytes).Entries().size() == CeilDiv(children + 2, 4) * 4);
        if(children == 3)
        {
            CHECK(slots_before == 4);
            CHECK(cfb::Package(signed_bytes).Entries().size() == 8);
        }

        REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
        const Bytes stripped = sm::ReadAll(path);
        RequireCanonical(original, stripped, "stripped");
        CHECK(cfb::Package(stripped).Entries().size() == slots_before);
    }
}

TEST_CASE("the mini stream grows and shrinks with a consistent index and recorded size", "[MsiLayouts][mini]")
{
    seedtest::ScratchDir scratch("msi-layouts-mini");
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
    root.Add(cfb::BuildNode::Stream(u"small", cfb::PatternBytes(100, 1)));
    root.Add(cfb::BuildNode::Stream(u"middle", cfb::PatternBytes(700, 2)));
    root.Add(cfb::BuildNode::Stream(u"big", cfb::PatternBytes(5000, 3)));
    for(const unsigned version : {3u, 4u})
    {
        INFO("version " << version);
        cfb::BuildOptions options;
        options.version = version;
        const Bytes original = cfb::Build(root, options);
        const std::string path = sm::WriteScratch(scratch, "mini.msi", original);

        // Up to just below the cut-off, then across it, down again, and to one byte.
        for(const std::size_t size : {4095u, 100u, 4096u, 63u, 64u, 65u, 20000u, 1u, 4095u})
        {
            INFO("signature of " << size << " bytes");
            const Bytes blob = cfb::PatternBytes(size, 9);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
            const Bytes now = sm::ReadAll(path);
            RequireCanonical(original, now, "signature of " + std::to_string(size));
            RequireSignatureIs(path, blob);
            const cfb::Package package(now);
            const auto found = package.FindSignature();
            REQUIRE(found.has_value());
            CHECK(package.InMiniStream(*found) == (size < 4096));
            // The streams below the cut-off, signature included, are all there is in the mini stream.
            CHECK(msi::MiniSectorsNeeded(package) ==
                  CeilDiv(100, 64) + CeilDiv(700, 64) + (size < 4096 ? CeilDiv(size, 64) : 0));

            // The file is no larger than one signing needs: the same as signing the original directly.
            const std::string direct = sm::WriteScratch(scratch, "direct.msi", original);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(direct, blob));
            CHECK(sm::ReadAll(direct) == now);
        }

        REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
        RequireCanonical(original, sm::ReadAll(path), "stripped");
    }
}

TEST_CASE("a package without a mini stream gets one for a small signature and drops it again", "[MsiLayouts][mini]")
{
    seedtest::ScratchDir scratch("msi-layouts-nomini");
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
    root.Add(cfb::BuildNode::Stream(u"first", cfb::PatternBytes(5000, 1)));
    root.Add(cfb::BuildNode::Stream(u"second", cfb::PatternBytes(9000, 2)));
    for(const unsigned version : {3u, 4u})
    {
        INFO("version " << version);
        cfb::BuildOptions options;
        options.version = version;
        const Bytes original = cfb::Build(root, options);
        const cfb::Package plain(original);
        REQUIRE(plain.Entries()[0].size == 0);
        REQUIRE(plain.Entries()[0].start == cfb::kEndOfChain);
        const std::string path = sm::WriteScratch(scratch, "nomini.msi", original);

        // A signature at the cut-off needs no mini stream.
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(4096, 3)));
        {
            const Bytes now = sm::ReadAll(path);
            RequireCanonical(original, now, "4096 bytes");
            CHECK(cfb::Package(now).Entries()[0].size == 0);
            CHECK(msi::HeaderField(now, msi::kHeaderMiniFatSectors) == 0);
        }

        // A smaller one creates it: 100 bytes are two mini sectors.
        const Bytes blob = cfb::PatternBytes(100, 4);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        {
            const Bytes now = sm::ReadAll(path);
            RequireCanonical(original, now, "100 bytes");
            CHECK(cfb::Package(now).Entries()[0].size == 128);
            CHECK(msi::HeaderField(now, msi::kHeaderMiniFatSectors) == 1);
            RequireSignatureIs(path, blob);
        }

        // Stripping it removes the only mini stream, and the container with it.
        REQUIRE(MsiSigner::StripSignature(path) == true);
        const Bytes stripped = sm::ReadAll(path);
        RequireCanonical(original, stripped, "stripped");
        CHECK(cfb::Package(stripped).Entries()[0].size == 0);
        CHECK(cfb::Package(stripped).Entries()[0].start == cfb::kEndOfChain);
        CHECK(msi::HeaderField(stripped, msi::kHeaderMiniFatSectors) == 0);
    }
}

TEST_CASE("the extended index appears and disappears at 109 allocation sectors", "[MsiLayouts][extended]")
{
    // One stream of 13,842 ordinary sectors, the directory sector and 109
    // allocation sectors fill 13,952 = 109 * 128 sectors; the 98 sectors of a
    // 50,000-byte signature need a 110th allocation sector, and so an extended
    // index sector.
    seedtest::ScratchDir scratch("msi-layouts-extended");
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
    root.Add(cfb::BuildNode::Stream(u"large", cfb::PatternBytes(13842ull * 512, 3)));
    const Bytes original = cfb::Build(root);
    const cfb::Package plain(original);
    REQUIRE(plain.FatSectorCount() == 109);
    REQUIRE(plain.DifatSectorCount() == 0);
    const std::string path = sm::WriteScratch(scratch, "extended.msi", original);

    const Bytes blob = cfb::PatternBytes(50000, 7);
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
    {
        const Bytes now = sm::ReadAll(path);
        const cfb::Package package(now);
        CHECK(package.FatSectorCount() == 110);
        CHECK(package.DifatSectorCount() == 1);
        RequireCanonical(original, now, "grown past 109");
        RequireSignatureIs(path, blob);
    }

    REQUIRE_NOTHROW(MsiSigner::StripSignature(path));
    {
        const Bytes now = sm::ReadAll(path);
        const cfb::Package package(now);
        CHECK(package.FatSectorCount() == 109);
        CHECK(package.DifatSectorCount() == 0);
        CHECK(msi::HeaderField(now, 68) == cfb::kEndOfChain);
        RequireCanonical(original, now, "shrunk to 109");
    }
}

TEST_CASE("a package with an extended index keeps it and its content", "[MsiLayouts][extended]")
{
    seedtest::ScratchDir scratch("msi-layouts-extended-keep");
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry", cfb::shapes::Id(4));
    root.Add(cfb::BuildNode::Stream(u"alpha", cfb::PatternBytes(70, 1)));
    root.Add(cfb::BuildNode::Stream(u"large", cfb::PatternBytes(7300000, 2)));
    cfb::BuildNode &sub = root.Add(cfb::BuildNode::Storage(u"Sub", cfb::shapes::Id(8)));
    sub.Add(cfb::BuildNode::Stream(u"inner", cfb::PatternBytes(900, 3)));
    const Bytes original = cfb::Build(root);
    REQUIRE(cfb::Package(original).DifatSectorCount() >= 1);
    const std::string path = sm::WriteScratch(scratch, "extended.msi", original);

    for(const std::size_t size : {1426u, 20000u})
    {
        INFO("signature of " << size << " bytes");
        const Bytes blob = cfb::PatternBytes(size, 11);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        const Bytes now = sm::ReadAll(path);
        CHECK(cfb::Package(now).DifatSectorCount() >= 1);
        RequireCanonical(original, now, "signed");
        RequireSignatureIs(path, blob);
        CHECK(cfb::Fingerprint(now) == cfb::Fingerprint(original));
    }
    REQUIRE(MsiSigner::StripSignature(path) == true);
    RequireCanonical(original, sm::ReadAll(path), "stripped");
}

TEST_CASE("a refused embed or strip leaves the file byte for byte unchanged", "[MsiLayouts][refusal]")
{
    seedtest::ScratchDir scratch("msi-layouts-refuse");
    const Bytes tiny = sm::LoadSample("tiny.msi");

    // Each damaged or unsupported package, and the text its refusal must carry.
    struct Case
    {
        std::string label;
        Bytes bytes;
        std::string text;
    };
    std::vector<Case> cases;
    {
        // Two streams with one name, in a package that holds a signature.
        Bytes bytes = sm::LoadSample("tiny-osslsig-small.msi");
        const cfb::Package package(bytes);
        std::vector<std::uint32_t> streams;
        for(std::uint32_t i = 1; i < package.Entries().size(); ++i)
        {
            const cfb::DirEntry &entry = package.Entries()[i];
            if(entry.type == cfb::kTypeStream && !cfb::IsSignatureName(entry.name))
            {
                streams.push_back(i);
            }
        }
        REQUIRE(streams.size() >= 2);
        const std::uint64_t first = package.EntryOffset(streams[0]);
        const std::uint64_t second = package.EntryOffset(streams[1]);
        for(std::uint64_t i = 0; i < 66; ++i)
        {
            bytes[second + i] = bytes[first + i];
        }
        cases.push_back({"two entries with one name", bytes, "two entries named"});
    }
    {
        // A stream whose chain is shorter than its size, and one whose chain is longer.
        cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
        root.Add(cfb::BuildNode::Stream(u"data", cfb::PatternBytes(5000, 1)));
        root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), cfb::PatternBytes(100, 2)));
        const Bytes built = cfb::Build(root);
        const cfb::Package package(built);
        std::uint32_t index = 0;
        for(std::uint32_t i = 1; i < package.Entries().size(); ++i)
        {
            if(package.Entries()[i].name == u"data")
            {
                index = i;
            }
        }
        REQUIRE(index != 0);
        for(const auto &change : {std::pair<std::uint64_t, std::string>{50000, "exceeds"},
                                  std::pair<std::uint64_t, std::string>{4200, "disagrees"}})
        {
            Bytes bytes = built;
            sm::PatchLE<std::uint64_t>(bytes, package.EntryOffset(index) + 120, change.first);
            cases.push_back({"stream size " + std::to_string(change.first), bytes, change.second});
        }
    }
    {
        // A sector size the library does not write.
        Bytes bytes = tiny;
        sm::PatchLE<std::uint16_t>(bytes, 30, 10);
        cases.push_back({"sector size 1024", bytes, "sector size exponent"});
    }
    {
        Bytes bytes = sm::Truncate(tiny, tiny.size() - 100);
        cases.push_back({"cut short", bytes, ""});
    }

    for(const Case &item : cases)
    {
        INFO(item.label);
        const std::string path = sm::WriteScratch(scratch, "refuse.msi", item.bytes);
        try
        {
            MsiSigner::EmbedSignature(path, cfb::PatternBytes(300, 1));
            FAIL("embed accepted " << item.label);
        }
        catch(const std::runtime_error &error)
        {
            const std::string message = error.what();
            INFO("embed message: " << message);
            CHECK(message.find(item.text) != std::string::npos);
        }
        sm::RequireUnchanged(path, item.bytes);

        try
        {
            MsiSigner::StripSignature(path);
            FAIL("strip accepted " << item.label);
        }
        catch(const std::runtime_error &error)
        {
            const std::string message = error.what();
            INFO("strip message: " << message);
            CHECK(message.find(item.text) != std::string::npos);
        }
        sm::RequireUnchanged(path, item.bytes);
    }
}

TEST_CASE("an empty signature is refused and the file is unchanged", "[MsiLayouts][refusal]")
{
    seedtest::ScratchDir scratch("msi-layouts-empty");
    for(const char *sample : {"tiny.msi", "tiny-osslsig-small.msi"})
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const std::string path = msi::CopySample(scratch, sample);
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, Bytes()), std::runtime_error);
        sm::RequireUnchanged(path, original);
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, Bytes(), true), std::runtime_error);
        sm::RequireUnchanged(path, original);
    }
}
