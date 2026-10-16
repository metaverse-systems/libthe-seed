// What a question about a file costs.
//
// Files of 256 MiB of each kind, signed and unsigned, are generated here and
// never checked in: a PE file whose last section is large, thin and universal
// Mach-O files with trailing data, and an installer package with a large
// stream. Each query (kind detection, signature presence, installer
// detection) is run once on each file. Bytes read are the growth of `rchar` in
// /proc/self/io around the call, which does not depend on how the library reads
// (so this program compiles and runs before and after the library changes how
// it reads), and heap growth comes from HeapCounter.hpp. For every call:
//
//   - at most 1 MiB is read (the installer's directory and allocation reads
//     included; the generated packages have small directories);
//   - the heap grows by less than 2 MiB.
//
// Every figure is printed. The files are removed when the test ends. The
// byte count needs /proc, so the checks run on Linux only.
//
// A second group of cases pins that the chain of an installer's mini stream is
// resolved once per package model, however many streams are read.

#include "HeapCounter.hpp"
#include "CfbReference.hpp"
#include "MalformedInput.hpp"
#include "MsiTestSupport.hpp"

#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MsiSigner.hpp>
#include <libthe-seed/PeSigner.hpp>

#include "internal/CfbReader.hpp"
#include "internal/FileIO.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace
{

namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
namespace in = the_seed::internal;
using sm::Bytes;

constexpr std::uint64_t kMebibyte = 1024 * 1024;
constexpr std::uint64_t kFileSize = 256 * kMebibyte;

#if defined(__linux__)

// The bytes the process has asked the kernel to read so far.
std::uint64_t BytesRead()
{
    std::ifstream stat("/proc/self/io");
    std::string key;
    std::uint64_t value = 0;
    while(stat >> key >> value)
    {
        if(key == "rchar:")
        {
            return value;
        }
    }
    FAIL("rchar is not in /proc/self/io");
    return 0;
}

struct Cost
{
    std::uint64_t read = 0;
    std::size_t heap = 0;
};

template <typename Query>
Cost Measure(Query &&query, int &answer)
{
    Cost cost;
    seedtest::heap::HeapGrowthScope heap;
    const std::uint64_t before = BytesRead();
    answer = query();
    cost.read = BytesRead() - before;
    cost.heap = heap.PeakGrowth();
    return cost;
}

struct NamedQuery
{
    std::string name;
    std::function<int()> run;
    int expected;
};

void CheckFile(const std::string &kind, const std::vector<NamedQuery> &queries)
{
    REQUIRE(seedtest::heap::HeapGrowthScope::Active());
    for(const NamedQuery &query : queries)
    {
        INFO(kind << ": " << query.name);
        int answer = -1;
        const Cost cost = Measure(query.run, answer);
        std::cout << "query cost: " << kind << ": " << query.name << ": read " << cost.read << " bytes, heap growth "
                  << cost.heap << " bytes" << std::endl;
        CHECK(answer == query.expected);
        CHECK(cost.read <= kMebibyte);
        CHECK(cost.heap < 2 * kMebibyte);
    }
}

std::vector<NamedQuery> MachOQueries(const std::string &path, bool signed_file, bool fat)
{
    using the_seed::MachOParser;
    using the_seed::MachOSigner;
    const int format = static_cast<int>(fat ? MachOParser::Format::Fat : MachOParser::Format::MachO64);
    return {
        {"DetectFormat", [path] { return static_cast<int>(MachOParser::DetectFormat(path)); }, format},
        {"IsMachO", [path] { return MachOParser::IsMachO(path) ? 1 : 0; }, 1},
        {"IsFatBinary", [path] { return MachOParser::IsFatBinary(path) ? 1 : 0; }, fat ? 1 : 0},
        {"MachOSigner::HasEmbeddedSignature", [path] { return MachOSigner::HasEmbeddedSignature(path) ? 1 : 0; },
         signed_file ? 1 : 0},
    };
}

// A sample with the file extended by zeros to 256 MiB (the extra bytes are
// not stored).
std::string ExtendedSample(const seedtest::ScratchDir &scratch, const std::string &sample, const std::string &name)
{
    const std::string path = scratch.File(name);
    std::filesystem::copy_file(seedtest::FixturePath(sample), path, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(path, kFileSize);
    return path;
}

// tiny.exe with its last section covering the rest of a 256 MiB file.
std::string LargePe(const seedtest::ScratchDir &scratch, const std::string &name)
{
    Bytes bytes = sm::LoadSample("tiny.exe");
    const std::uint64_t pe = sm::PeHeaderOffset(bytes);
    const std::uint16_t sections = sm::detail::GetLE<std::uint16_t>(bytes, pe + 6);
    const std::uint16_t optional_size = sm::detail::GetLE<std::uint16_t>(bytes, pe + 20);
    REQUIRE(sections > 0);
    const std::uint64_t last = pe + 24 + optional_size + 40ull * (sections - 1);
    const std::uint32_t raw_pointer = sm::detail::GetLE<std::uint32_t>(bytes, last + 20);
    sm::PatchLE<std::uint32_t>(bytes, last + 16, static_cast<std::uint32_t>(kFileSize - raw_pointer));
    const std::string path = sm::WriteScratch(scratch, name, bytes);
    std::filesystem::resize_file(path, kFileSize);
    return path;
}

// An installer package whose largest stream makes the file about 256 MiB.
Bytes LargeMsi(bool signed_file)
{
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry", cfb::shapes::Id(5));
    root.Add(cfb::BuildNode::Stream(u"alpha", cfb::PatternBytes(100, 1)));
    root.Add(cfb::BuildNode::Stream(u"beta", cfb::PatternBytes(5000, 3)));
    cfb::BuildNode &sub = root.Add(cfb::BuildNode::Storage(u"Sub", cfb::shapes::Id(6)));
    sub.Add(cfb::BuildNode::Stream(u"gamma", cfb::PatternBytes(300, 4)));
    for(std::size_t i = 0; i < 20; ++i)
    {
        const std::string text = "small-" + std::to_string(100 + i);
        root.Add(cfb::BuildNode::Stream(cfb::Name(text.begin(), text.end()),
                                        cfb::PatternBytes(30 + i * 7, static_cast<std::uint8_t>(i))));
    }
    if(signed_file)
    {
        root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), cfb::PatternBytes(1500, 9)));
    }
    root.Add(cfb::BuildNode::Stream(u"Cabinet", cfb::PatternBytes(kFileSize - 2 * kMebibyte, 2)));
    return cfb::Build(root);
}

#endif // __linux__

} // namespace

#if defined(__linux__)

TEST_CASE("a question about a PE file reads only its headers and certificate entry", "[QueryCost]")
{
    seedtest::ScratchDir scratch("query-cost");
    // Warm up anything the first call sets up for itself.
    (void)the_seed::PeSigner::HasEmbeddedSignature(seedtest::FixturePath("tiny.exe"));

    const std::string path = LargePe(scratch, "large.exe");
    CheckFile("PE 256 MiB, unsigned",
              {{"PeSigner::HasEmbeddedSignature", [path] { return the_seed::PeSigner::HasEmbeddedSignature(path) ? 1 : 0; },
                0}});

    the_seed::PeSigner::EmbedSignature(path, Bytes(2000, 0x30));
    REQUIRE(std::filesystem::file_size(path) > kFileSize);
    CheckFile("PE 256 MiB, signed (certificate at the end)",
              {{"PeSigner::HasEmbeddedSignature", [path] { return the_seed::PeSigner::HasEmbeddedSignature(path) ? 1 : 0; },
                1}});
}

TEST_CASE("a question about a Mach-O file reads only its headers and signature", "[QueryCost]")
{
    seedtest::ScratchDir scratch("query-cost");
    (void)the_seed::MachOParser::DetectFormat(seedtest::FixturePath("tiny-macho-x86_64"));

    CheckFile("thin Mach-O 256 MiB, unsigned",
              MachOQueries(ExtendedSample(scratch, "tiny-macho-x86_64", "thin"), false, false));
    CheckFile("thin Mach-O 256 MiB, signed",
              MachOQueries(ExtendedSample(scratch, "tiny-macho-x86_64-adhoc", "thin-signed"), true, false));
    CheckFile("fat Mach-O 256 MiB, unsigned",
              MachOQueries(ExtendedSample(scratch, "tiny-macho-universal", "fat"), false, true));
    CheckFile("fat Mach-O 256 MiB, signed",
              MachOQueries(ExtendedSample(scratch, "tiny-macho-universal-adhoc", "fat-signed"), true, true));
}

TEST_CASE("a question about an installer package reads only its tables", "[QueryCost]")
{
    seedtest::ScratchDir scratch("query-cost");
    (void)the_seed::MsiSigner::HasEmbeddedSignature(seedtest::FixturePath("tiny.msi"));

    for(const bool signed_file : {false, true})
    {
        const std::string path = sm::WriteScratch(scratch, "large.msi", LargeMsi(signed_file));
        REQUIRE(std::filesystem::file_size(path) >= 250 * kMebibyte);
        const int expected = signed_file ? 1 : 0;
        CheckFile(signed_file ? "MSI 256 MiB, signed" : "MSI 256 MiB, unsigned",
                  {{"MsiSigner::HasEmbeddedSignature", [path] { return the_seed::MsiSigner::HasEmbeddedSignature(path) ? 1 : 0; },
                    expected},
                   {"MsiSigner::IsMsi", [path] { return the_seed::MsiSigner::IsMsi(path) ? 1 : 0; }, 1}});
        std::filesystem::remove(path);
    }
}

#endif

TEST_CASE("the mini stream's chain is resolved once per package model", "[QueryCost]")
{
    // Many small streams, all in the mini stream.
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry", cfb::shapes::Id(5));
    std::vector<std::u16string> names;
    for(std::size_t i = 0; i < 300; ++i)
    {
        const std::string text = "stream-" + std::to_string(1000 + i);
        cfb::Name name(text.begin(), text.end());
        names.push_back(name);
        root.Add(cfb::BuildNode::Stream(name, cfb::PatternBytes(40 + (i * 13) % 3000, static_cast<std::uint8_t>(i))));
    }
    const Bytes package = cfb::Build(root);
    seedtest::ScratchDir scratch("query-cost");
    const std::string path = sm::WriteScratch(scratch, "many.msi", package);

    SECTION("several streams of one model")
    {
        in::PackageModel model = in::CfbReader::Parse(package);
        in::ResetMiniStreamResolveCount();
        std::size_t read = 0;
        for(const std::u16string &name : names)
        {
            const in::CfbNode *node = in::PackageModel::FindByEnumeration(model.Root(), name);
            REQUIRE(node != nullptr);
            read += model.ReadStream(*node).size();
        }
        CHECK(read > 300 * 40);
        CHECK(in::MiniStreamResolveCount() == 1);

        // A second model resolves its own chain once.
        in::PackageModel other = in::CfbReader::Parse(package);
        const in::CfbNode *node = in::PackageModel::FindByEnumeration(other.Root(), names[7]);
        REQUIRE(node != nullptr);
        (void)other.ReadStream(*node);
        CHECK(in::MiniStreamResolveCount() == 2);
    }

    SECTION("one operation that reads every stream")
    {
        in::ResetMiniStreamResolveCount();
        const auto digest = the_seed::MsiSigner::ComputeAuthenticodeDigest(path);
        CHECK_FALSE(digest.digest.empty());
        CHECK(in::MiniStreamResolveCount() == 1);
    }
}
