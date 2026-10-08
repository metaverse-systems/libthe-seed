// Installer packages of tens of megabytes.
//
// A package of 7 MB (its allocation index needs more than 109 sectors, so the
// extended index is in use) and one of 50 MB, both with 512-byte sectors, are
// generated here by the independent builder of CfbReference.hpp and never
// checked in. Each is signed, signed again with a signature on the other side
// of the 4,096 byte cut-off, and stripped. After every step:
//
//   - the independent reader parses the result, finds the signature by
//     searching, and every other stream is as it was (names, bytes, class
//     identifiers, state bits, times) and the fingerprint is unchanged;
//   - the live heap grew by no more than 2.5 times the size of the file plus
//     4 MB while the library worked (HeapCounter.hpp);
//   - signing the 50 MB package took less than 30 seconds.
//
// With osslsigncode installed, it reads the signature back from the large
// package and calculates the same fingerprint as the independent reader.
//
// Packages outside what the library accepts are refused with the file left as
// it was, and the refusal itself stays inside the same memory bound. The format's own
// limit (4,294,967,290 sectors) needs a package of 2 terabytes and is not
// generated; the refusals tested here are a stream whose recorded size
// disagrees with its chain and headers whose extended index or directory lies outside the file.

#include "MsiTestSupport.hpp"
#include "ToolSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::msi::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
namespace msi = seedtest::msi;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kMegabyte = 1024 * 1024;

// Whether the heap counter is part of this program, and a note of what it saw.
struct Measured
{
    double seconds = 0;
    std::size_t growth = 0;
};

template <typename Work>
Measured Measure(Work work)
{
    seedtest::heap::HeapGrowthScope heap;
    const auto start = Clock::now();
    work();
    Measured out;
    out.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    out.growth = heap.PeakGrowth();
    return out;
}

void Report(const std::string &label, std::uint64_t file_size, const Measured &measured)
{
    std::cout << "LARGE " << label << ": file " << file_size << " bytes, " << measured.seconds
              << " s, heap grew by " << measured.growth << " bytes (bound " << (5 * file_size / 2 + 4 * kMegabyte)
              << ")" << std::endl;
}

void RequireWithinBound(const std::string &label, std::uint64_t file_size, const Measured &measured)
{
    Report(label, file_size, measured);
    REQUIRE(seedtest::heap::HeapGrowthScope::Active());
    INFO(label << ": the heap grew by " << measured.growth << " bytes for a file of " << file_size);
    CHECK(measured.growth <= 5 * file_size / 2 + 4 * kMegabyte);
}

// One big stream of the wanted size plus small ones in two storages.
cfb::BuildNode LargeRoot(std::size_t big)
{
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry", cfb::shapes::Id(5));
    root.Add(cfb::BuildNode::Stream(u"alpha", cfb::PatternBytes(100, 1)));
    root.Add(cfb::BuildNode::Stream(u"Cabinet", cfb::PatternBytes(big, 2)));
    root.Add(cfb::BuildNode::Stream(u"beta", cfb::PatternBytes(5000, 3)));
    cfb::BuildNode &sub = root.Add(cfb::BuildNode::Storage(u"Sub", cfb::shapes::Id(6)));
    sub.Add(cfb::BuildNode::Stream(u"gamma", cfb::PatternBytes(300, 4)));
    sub.Add(cfb::BuildNode::Stream(u"delta", cfb::PatternBytes(70000, 5)));
    for(std::size_t i = 0; i < 40; ++i)
    {
        const std::string text = "small-" + std::to_string(100 + i);
        root.Add(cfb::BuildNode::Stream(cfb::Name(text.begin(), text.end()), cfb::PatternBytes(30 + i * 7, static_cast<std::uint8_t>(i))));
    }
    return root;
}

struct Expectation
{
    const char *label;
    std::size_t big;
    double seconds; // limit for one signing
};

// What must hold of a package after an operation.
void RequireResult(const Bytes &after, const std::vector<cfb::ListedEntry> &listing, const std::string &fingerprint,
                   const std::optional<Bytes> &signature)
{
    const cfb::Package package(after);
    CHECK(package.SectorSize() == 512);
    CHECK(package.DifatSectorCount() > 0);
    const auto found = package.FindSignature();
    if(signature.has_value())
    {
        REQUIRE(found.has_value());
        CHECK(package.ReadStream(*found) == *signature);
        CHECK(package.InMiniStream(*found) == (signature->size() < 4096));
    }
    else
    {
        CHECK_FALSE(found.has_value());
    }
    CHECK(cfb::FirstDifference(listing, cfb::Listing(package, true)).empty());
    CHECK(cfb::Fingerprint(package) == fingerprint);
    CHECK_FALSE(msi::HasExtendedStream(after));
}

void RunLarge(const Expectation &expect)
{
    INFO(expect.label);
    seedtest::ScratchDir scratch("msi-large");
    std::string fingerprint;
    std::vector<cfb::ListedEntry> listing;
    std::uint64_t original_size = 0;
    const std::string path = scratch.File("large.msi");
    {
        const Bytes original = cfb::Build(LargeRoot(expect.big));
        original_size = original.size();
        const cfb::Package package(original);
        CHECK(package.SectorSize() == 512);
        CHECK(package.FatSectorCount() > 109);
        CHECK(package.DifatSectorCount() > 0);
        fingerprint = cfb::Fingerprint(package);
        listing = cfb::Listing(package, true);
        sm::WriteScratch(scratch, "large.msi", original);
    }
    std::cout << "LARGE " << expect.label << ": built " << original_size << " bytes in " << listing.size()
              << " entries" << std::endl;

    // The fingerprint, read from the file.
    {
        std::string digest;
        const Measured measured = Measure([&] { digest = msi::ToHexString(MsiSigner::ComputeAuthenticodeDigest(path).digest); });
        RequireWithinBound(std::string(expect.label) + " fingerprint", original_size, measured);
        CHECK(digest == fingerprint);
    }
    {
        MsiSigner::SignatureCheck check;
        const Measured measured = Measure([&] { check = MsiSigner::CheckSignature(path); });
        RequireWithinBound(std::string(expect.label) + " check", original_size, measured);
        CHECK(check.state == MsiSigner::SignatureState::None);
    }

    // Sign with a signature below the cut-off (a real one that osslsigncode wrote).
    const Bytes first = msi::SampleSignature("tiny-osslsig-small.msi");
    {
        const Measured measured = Measure([&] { MsiSigner::EmbedSignature(path, first); });
        RequireWithinBound(std::string(expect.label) + " sign", original_size, measured);
        CHECK(measured.seconds < expect.seconds);
        RequireResult(sm::ReadAll(path), listing, fingerprint, first);
        CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(first));
    }
    const Bytes signed_once = sm::ReadAll(path);

    // Live reader: osslsigncode reads the signature and calculates the fingerprint of the large package.
    if(seedtest::RequireTool("osslsigncode"))
    {
        const std::string out = scratch.File("extracted.sig");
        const seedtest::ToolRun extract = seedtest::RunCommand("osslsigncode extract-signature -in " +
                                                               seedtest::ShellQuote(path) + " -out " + seedtest::ShellQuote(out));
        INFO(extract.output);
        CHECK(extract.status == 0);
        CHECK(sm::ReadAll(out) == first);
        const seedtest::ToolRun verify = seedtest::RunCommand("osslsigncode verify -in " + seedtest::ShellQuote(path));
        INFO(verify.output);
        CHECK(verify.output.find("Failed to get a next") == std::string::npos);
        CHECK(verify.output.find("Corrupted") == std::string::npos);
        CHECK(verify.output.find("data error") == std::string::npos);
        const std::string needle = "Calculated DigitalSignature      : ";
        const std::size_t at = verify.output.find(needle);
        REQUIRE(at != std::string::npos);
        std::string calculated = verify.output.substr(at + needle.size(), 64);
        for(char &c : calculated)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        CHECK(calculated == fingerprint);
    }

    // Sign again with a signature above the cut-off, and once more with the same one.
    const Bytes second = cfb::PatternBytes(6000, 17);
    for(int round = 0; round < 2; ++round)
    {
        INFO("second signing, round " << round);
        const Measured measured = Measure([&] { MsiSigner::EmbedSignature(path, second); });
        RequireWithinBound(std::string(expect.label) + " sign again", original_size, measured);
        CHECK(measured.seconds < expect.seconds);
        RequireResult(sm::ReadAll(path), listing, fingerprint, second);
    }

    // Back below the cut-off: the package is what signing the bare one once gives.
    {
        const Measured measured = Measure([&] { MsiSigner::EmbedSignature(path, first); });
        RequireWithinBound(std::string(expect.label) + " sign back", original_size, measured);
        CHECK(sm::ReadAll(path) == signed_once);
    }

    // With the digest check (the signature holds another fingerprint, so it is refused and nothing changes).
    {
        const Bytes before = sm::ReadAll(path);
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, first, true), std::runtime_error);
        sm::RequireUnchanged(path, before);
    }

    // Strip.
    {
        bool removed = false;
        const Measured measured = Measure([&] { removed = MsiSigner::StripSignature(path); });
        RequireWithinBound(std::string(expect.label) + " strip", original_size, measured);
        CHECK(removed);
        RequireResult(sm::ReadAll(path), listing, fingerprint, std::nullopt);
        CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
    }
    {
        const Bytes stripped = sm::ReadAll(path);
        CHECK_FALSE(MsiSigner::StripSignature(path));
        sm::RequireUnchanged(path, stripped);
    }
}

} // namespace

TEST_CASE("a 7 MB package with an extended index is signed and stripped within the bound", "[MsiLarge][seven]")
{
    RunLarge({"7 MB", 7 * kMegabyte - 30 * 1024, 30.0});
}

TEST_CASE("a 50 MB package is signed in under 30 seconds within the memory bound", "[MsiLarge][fifty]")
{
    RunLarge({"50 MB", 50 * kMegabyte - 100 * 1024, 30.0});
}

TEST_CASE("a refusal on a large package leaves the file as it was and stays within the memory bound", "[MsiLarge][refusal]")
{
    seedtest::ScratchDir scratch("msi-large-refuse");
    const Bytes built = cfb::Build(LargeRoot(7 * kMegabyte - 30 * 1024));
    const cfb::Package package(built);
    REQUIRE(package.DifatSectorCount() > 0);

    struct Case
    {
        std::string label;
        Bytes bytes;
        std::string text;
    };
    std::vector<Case> cases;
    {
        // The recorded size of a stream larger than its chain, and one smaller.
        std::uint32_t index = 0;
        for(std::uint32_t i = 1; i < package.Entries().size(); ++i)
        {
            if(package.Entries()[i].name == u"Cabinet")
            {
                index = i;
            }
        }
        REQUIRE(index != 0);
        {
            Bytes bytes = built;
            sm::PatchLE<std::uint64_t>(bytes, package.EntryOffset(index) + 120, package.Entries()[index].size + 100000);
            cases.push_back({"stream size above its chain", bytes, "exceeds"});
        }
        {
            Bytes bytes = built;
            sm::PatchLE<std::uint64_t>(bytes, package.EntryOffset(index) + 120, package.Entries()[index].size / 2);
            cases.push_back({"stream size far below its chain", bytes, "disagrees"});
        }
    }
    {
        // The extended index starts outside the file.
        Bytes bytes = built;
        sm::PatchLE<std::uint32_t>(bytes, 68, 0x00FFFFFFu);
        cases.push_back({"extended index beyond the file", bytes, ""});
    }
    {
        // The directory starts outside the file.
        Bytes bytes = built;
        sm::PatchLE<std::uint32_t>(bytes, 48, 0x00FFFFFFu); // first directory sector far outside the file
        cases.push_back({"directory beyond the file", bytes, ""});
    }
    {
        Bytes bytes = sm::Truncate(built, built.size() - 1000000);
        cases.push_back({"file cut short", bytes, ""});
    }

    for(const Case &item : cases)
    {
        INFO(item.label);
        const std::string path = sm::WriteScratch(scratch, "refuse.msi", item.bytes);
        const std::uint64_t size = item.bytes.size();
        for(const bool embed : {true, false})
        {
            // Stripping a package that holds no signature has nothing to rebuild, so a stream whose
            // chain is never read may go unnoticed: it then returns false and leaves the file alone.
            bool refused = false;
            try
            {
                if(embed)
                {
                    MsiSigner::EmbedSignature(path, cfb::PatternBytes(300, 1));
                }
                else
                {
                    const bool removed = MsiSigner::StripSignature(path);
                    CHECK_FALSE(removed);
                }
            }
            catch(const std::runtime_error &error)
            {
                refused = true;
                const std::string message = error.what();
                INFO((embed ? "embed" : "strip") << " message: " << message);
                CHECK_FALSE(message.empty());
                CHECK(message.find(item.text) != std::string::npos);
            }
            if(embed)
            {
                CHECK(refused);
            }
            sm::RequireUnchanged(path, item.bytes);
        }
        // The refusal is made inside the same memory bound.
        const Measured measured = Measure([&] {
            try
            {
                MsiSigner::EmbedSignature(path, cfb::PatternBytes(300, 1));
            }
            catch(const std::runtime_error &)
            {
            }
        });
        RequireWithinBound("refusal of " + item.label, size, measured);
    }
}
