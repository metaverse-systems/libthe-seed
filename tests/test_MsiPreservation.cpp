// What signing and stripping an installer package leaves alone.
//
// The packages signed by other tools (small, large and with an extended
// stream, nested storages, a signature entry with neighbours on both sides)
// are signed again, signed after stripping and stripped. The independent
// reader of CfbReference.hpp lists everything before and after (names, bytes,
// class identifiers, state bits, times); only the signature streams may
// differ. Then:
//
//   - the shape of the directory around the signature entry (a leaf, a child
//     of the root, with two neighbours, with a long chain on either side) is
//     generated, and every entry is found both by listing and by searching
//     with the format's ordering after every operation;
//   - replacing a signature held in the mini stream touches no other stream;
//   - the extended stream is dropped on replace and strip and never created;
//   - stripping a package without a signature leaves the file byte for byte
//     as it was and returns false; strip then sign gives the original
//     fingerprint;
//   - at least ten rounds of signing and stripping, with sizes on both sides
//     of the 4,096 byte cut-off, leave the content as it was, never grow the
//     file beyond what one signing needs, and leave no stale data;
//   - the package an earlier version (0.6.0) signed is repaired by replacing
//     the signature and by stripping it, and reading it never says it matches.

#include "MsiTestSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::msi::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
namespace msi = seedtest::msi;
using State = MsiSigner::SignatureState;

// The sizes of the blobs used: both sides of the cut-off.
const std::size_t kBlobSizes[] = {100, 4095, 4096, 5000};

std::string Hex(const Bytes &bytes)
{
    return msi::ToHexString(bytes);
}

// The fingerprint the library computes and the independent reader computes are the recorded one.
void RequireFingerprintIs(const std::string &path, const std::string &fingerprint)
{
    CHECK(Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest) == fingerprint);
    CHECK(cfb::Fingerprint(sm::ReadAll(path)) == fingerprint);
}

// The package after an operation: the independent reader parses it, finds
// every entry by listing and by searching, and sees only the wanted signature.
void RequireWellFormed(const Bytes &bytes, const std::optional<Bytes> &signature, const Bytes *original = nullptr)
{
    const cfb::Package package(bytes);
    if(original == nullptr)
    {
        msi::RequireAllFindable(package);
    }
    else
    {
        // What a search reached before, it reaches after, and now every entry is reached.
        msi::RequireStillFindable(cfb::Package(*original), package);
    }
    const auto found = package.FindSignature();
    if(signature.has_value())
    {
        REQUIRE(found.has_value());
        CHECK(package.ReadStream(*found) == *signature);
        CHECK(msi::SignaturePaths(bytes).size() == 1);
    }
    else
    {
        CHECK_FALSE(found.has_value());
        CHECK(msi::SignaturePaths(bytes).empty());
    }
    CHECK_FALSE(msi::HasExtendedStream(bytes));
}

// Replace (or add), strip, then add again, comparing the whole content each time.
void RequireOperationsKeepContent(const seedtest::ScratchDir &scratch, const std::string &sample, const Bytes &original,
                                  const std::string &fingerprint)
{
    for(const std::size_t size : kBlobSizes)
    {
        INFO("sample " << sample << " blob of " << size << " bytes");
        const std::string path = sm::WriteScratch(scratch, "ops.msi", original);
        const Bytes blob = cfb::PatternBytes(size, 41);

        // Replace (or add when there was no signature).
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        Bytes now = sm::ReadAll(path);
        msi::RequireSameContent(original, now);
        RequireWellFormed(now, blob, &original);
        RequireFingerprintIs(path, fingerprint);

        // Strip: only the signature streams are gone.
        CHECK(MsiSigner::StripSignature(path));
        const Bytes stripped = sm::ReadAll(path);
        msi::RequireSameContent(original, stripped);
        RequireWellFormed(stripped, std::nullopt, &original);
        RequireFingerprintIs(path, fingerprint);
        CHECK(cfb::Listing(cfb::Package(stripped), false) == cfb::Listing(cfb::Package(stripped), true));

        // Add again to the stripped package.
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        now = sm::ReadAll(path);
        msi::RequireSameContent(original, now);
        RequireWellFormed(now, blob, &original);
        RequireFingerprintIs(path, fingerprint);
    }
}

} // namespace

TEST_CASE("add replace and strip keep every other entry of packages another tool signed", "[MsiPreservation][keep]")
{
    seedtest::ScratchDir scratch("msi-keep-signed");
    for(const std::string &sample : msi::kSignedSamples)
    {
        const Bytes original = sm::LoadSample(sample);
        REQUIRE(msi::FoundSignature(original).has_value());
        RequireOperationsKeepContent(scratch, sample, original, msi::RecordedFingerprint(sample));
    }
}

TEST_CASE("add keeps every entry of the unsigned samples", "[MsiPreservation][keep]")
{
    seedtest::ScratchDir scratch("msi-keep-unsigned");
    for(const std::string &sample : msi::kUnsignedSamples)
    {
        const Bytes original = sm::LoadSample(sample);
        REQUIRE_FALSE(msi::FoundSignature(original).has_value());
        RequireOperationsKeepContent(scratch, sample, original, msi::RecordedFingerprint(sample));
    }
}

TEST_CASE("the signature streams are the only difference between before and after", "[MsiPreservation][keep]")
{
    // The listing with the signature streams included: replacing swaps one
    // stream for another, stripping removes it, adding inserts it.
    seedtest::ScratchDir scratch("msi-keep-listing");
    for(const std::string &sample : msi::kSignedSamples)
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const std::size_t before_streams = msi::SignaturePaths(original).size();
        REQUIRE(before_streams >= 1);
        const std::string path = msi::CopySample(scratch, sample);

        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(300, 2)));
        const Bytes replaced = sm::ReadAll(path);
        CHECK(cfb::CountBelowRoot(cfb::Package(replaced)) ==
              cfb::CountBelowRoot(cfb::Package(original)) - before_streams + 1);

        REQUIRE(MsiSigner::StripSignature(path));
        const Bytes stripped = sm::ReadAll(path);
        CHECK(cfb::CountBelowRoot(cfb::Package(stripped)) ==
              cfb::CountBelowRoot(cfb::Package(original)) - before_streams);

        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(300, 2)));
        CHECK(sm::ReadAll(path) == replaced);
    }
}

namespace {

// The shapes of the directory around the signature entry. The root holds
// `before` streams whose names order before the signature stream, `after` whose
// names order after it, one nested storage and the signature stream itself.
cfb::BuildNode ShapeRoot(std::size_t before, std::size_t after, const Bytes &signature)
{
    cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry", cfb::shapes::Id(9));
    for(std::size_t i = 0; i < before; ++i)
    {
        const std::string text = "b" + std::to_string(10 + i); // 3 characters, before the 17 of the signature
        root.Add(cfb::BuildNode::Stream(cfb::Name(text.begin(), text.end()),
                                        cfb::PatternBytes(i == 1 ? 5000 : 40 + i * 13, static_cast<std::uint8_t>(i))));
    }
    for(std::size_t i = 0; i < after; ++i)
    {
        const std::string text = "after-signature-" + std::to_string(10 + i); // 18 characters
        root.Add(cfb::BuildNode::Stream(cfb::Name(text.begin(), text.end()),
                                        cfb::PatternBytes(i == 2 ? 6000 : 70 + i * 11, static_cast<std::uint8_t>(i + 50))));
    }
    cfb::BuildNode &nested = root.Add(cfb::BuildNode::Storage(u"N", cfb::shapes::Id(4)));
    nested.Add(cfb::BuildNode::Stream(u"inner", cfb::PatternBytes(77, 5)));
    root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), signature));
    return root;
}

// The signature entry at the top of its side of the tree, a chain through the
// smaller names to its left and a chain through the larger names to its right.
cfb::Picker EitherSideChains()
{
    const cfb::Name wanted = cfb::SignatureName();
    return [wanted](const std::vector<cfb::Name> &sorted, std::size_t lo, std::size_t hi) {
        for(std::size_t i = lo; i < hi; ++i)
        {
            if(sorted[i] == wanted)
            {
                return i;
            }
        }
        // Left of the signature entry: only right neighbours; right of it: only left neighbours.
        for(std::size_t i = 0; i < sorted.size(); ++i)
        {
            if(sorted[i] == wanted)
            {
                return hi <= i ? lo : hi - 1;
            }
        }
        return (lo + hi) / 2;
    };
}

struct Picked
{
    const char *label;
    cfb::Picker picker;
};

std::vector<Picked> Pickers()
{
    return {{"balanced", cfb::BalancedPicker()},
            {"left chain", cfb::LeftChainPicker()},
            {"right chain", cfb::RightChainPicker()},
            {"signature at the top", cfb::TopPicker(cfb::SignatureName())},
            {"chains on either side", EitherSideChains()}};
}

// What the independent reader sees around the signature entry.
std::set<std::string> Observe(const cfb::Package &package)
{
    std::set<std::string> seen;
    const auto found = package.FindSignature();
    REQUIRE(found.has_value());
    const cfb::DirEntry &entry = package.Entries()[*found];
    const bool has_left = entry.left != cfb::kNoStream;
    const bool has_right = entry.right != cfb::kNoStream;
    if(package.Entries()[0].child == *found)
    {
        seen.insert("child of the root");
    }
    if(!has_left && !has_right)
    {
        seen.insert("leaf");
    }
    else if(has_left && has_right)
    {
        seen.insert(package.Entries()[0].child == *found ? "root child with two neighbours" : "inner with two neighbours");
    }
    else
    {
        seen.insert("one neighbour");
    }
    // Long chains: walk right from the left neighbour and left from the right one.
    std::size_t left_chain = 0;
    for(std::uint32_t at = entry.left; at != cfb::kNoStream; at = package.Entries()[at].right)
    {
        ++left_chain;
    }
    std::size_t right_chain = 0;
    for(std::uint32_t at = entry.right; at != cfb::kNoStream; at = package.Entries()[at].left)
    {
        ++right_chain;
    }
    if(left_chain >= 10 && right_chain >= 10)
    {
        seen.insert("long chain on either side");
    }
    return seen;
}

} // namespace

TEST_CASE("every shape of the directory around the signature entry is kept and searchable", "[MsiPreservation][shapes]")
{
    seedtest::ScratchDir scratch("msi-keep-shapes");
    std::set<std::string> covered;
    const std::size_t counts[] = {0, 1, 2, 3, 6, 12};
    for(const unsigned version : {3u, 4u})
    {
        for(const Picked &picked : Pickers())
        {
            for(const std::size_t before : counts)
            {
                for(const std::size_t after : counts)
                {
                    INFO("version " << version << " " << picked.label << " with " << before << " names before and "
                                    << after << " after");
                    const Bytes first_blob = cfb::PatternBytes(100, 61);
                    cfb::BuildOptions options;
                    options.version = version;
                    options.picker = picked.picker;
                    const Bytes original = cfb::Build(ShapeRoot(before, after, first_blob), options);
                    const cfb::Package built(original);
                    msi::RequireAllFindable(built);
                    for(const std::string &seen : Observe(built))
                    {
                        covered.insert(seen);
                    }
                    const std::string fingerprint = cfb::Fingerprint(built);
                    const std::string path = sm::WriteScratch(scratch, "shape.msi", original);

                    // Replace, replace across the cut-off, strip, add.
                    for(const std::size_t size : {5000u, 100u})
                    {
                        const Bytes blob = cfb::PatternBytes(size, 62);
                        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                        const Bytes now = sm::ReadAll(path);
                        msi::RequireSameContent(original, now);
                        RequireWellFormed(now, blob);
                        CHECK(Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest) == fingerprint);
                        CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));
                    }
                    REQUIRE(MsiSigner::StripSignature(path));
                    const Bytes stripped = sm::ReadAll(path);
                    msi::RequireSameContent(original, stripped);
                    RequireWellFormed(stripped, std::nullopt);
                    const Bytes blob = cfb::PatternBytes(4096, 63);
                    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                    const Bytes added = sm::ReadAll(path);
                    msi::RequireSameContent(original, added);
                    RequireWellFormed(added, blob);
                    CHECK(Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest) == fingerprint);
                }
            }
        }
    }
    // The generated packages did reach every shape the case is about.
    INFO("shapes reached by the generated packages");
    CHECK(covered.count("leaf") == 1);
    CHECK(covered.count("child of the root") == 1);
    CHECK(covered.count("root child with two neighbours") == 1);
    CHECK(covered.count("inner with two neighbours") == 1);
    CHECK(covered.count("one neighbour") == 1);
    CHECK(covered.count("long chain on either side") == 1);
}

TEST_CASE("replacing a signature in the mini stream touches no other stream", "[MsiPreservation][mini]")
{
    seedtest::ScratchDir scratch("msi-keep-mini");
    for(const std::string &sample : {std::string("tiny-osslsig-small.msi"), std::string("tiny-osslsig-dse.msi"),
                                     std::string("nested-osslsig.msi"), std::string("two-neighbours.msi")})
    {
        const Bytes original = sm::LoadSample(sample);
        const cfb::Package before(original);
        const auto found = before.FindSignature();
        REQUIRE(found.has_value());
        REQUIRE(before.InMiniStream(*found));

        for(const std::size_t size : {1u, 64u, 1444u, 4095u, 4096u, 20000u})
        {
            INFO("sample " << sample << " blob of " << size << " bytes");
            const std::string path = msi::CopySample(scratch, sample);
            const Bytes blob = cfb::PatternBytes(size, 71);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
            const Bytes after_bytes = sm::ReadAll(path);
            const cfb::Package after(after_bytes);

            // Every stream other than the signature streams, below the cut-off or above it, reads the same.
            msi::RequireSameContent(original, after_bytes);
            CHECK(after.Entries()[0].size >= msi::MiniSectorsNeeded(after) * cfb::kMiniSectorSize);
            for(std::uint32_t index = 1; index < after.Entries().size(); ++index)
            {
                if(after.Entries()[index].type == cfb::kTypeStream)
                {
                    REQUIRE_NOTHROW(after.ReadStream(index));
                }
            }
            CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));
        }
    }
}

TEST_CASE("the extended stream is dropped on replace and strip and never created", "[MsiPreservation][extended]")
{
    seedtest::ScratchDir scratch("msi-keep-extended");
    const Bytes dse = sm::LoadSample("tiny-osslsig-dse.msi");
    REQUIRE(msi::HasExtendedStream(dse));

    SECTION("replace")
    {
        const std::string path = msi::CopySample(scratch, "tiny-osslsig-dse.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(1444, 8)));
        const Bytes now = sm::ReadAll(path);
        CHECK_FALSE(msi::HasExtendedStream(now));
        CHECK(cfb::CountBelowRoot(cfb::Package(now)) == cfb::CountBelowRoot(cfb::Package(dse)) - 1);
        msi::RequireSameContent(dse, now);
    }
    SECTION("replace with the check against the fingerprint")
    {
        // The signature osslsigncode made covers the extended stream, so its stored digest is not
        // the fingerprint; the one of the plain sample is.
        const std::string path = msi::CopySample(scratch, "tiny-osslsig-dse.msi");
        const Bytes blob = msi::SampleSignature("tiny-osslsig-small.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob, true));
        CHECK_FALSE(msi::HasExtendedStream(sm::ReadAll(path)));
        CHECK(MsiSigner::CheckSignature(path).state == State::Matches);
    }
    SECTION("strip")
    {
        const std::string path = msi::CopySample(scratch, "tiny-osslsig-dse.msi");
        REQUIRE(MsiSigner::StripSignature(path));
        const Bytes now = sm::ReadAll(path);
        CHECK_FALSE(msi::HasExtendedStream(now));
        CHECK(cfb::CountBelowRoot(cfb::Package(now)) == cfb::CountBelowRoot(cfb::Package(dse)) - 2);
        msi::RequireSameContent(dse, now);
    }
    SECTION("an extended stream with no signature beside it is dropped as well")
    {
        cfb::BuildNode root = cfb::BuildNode::Storage(u"Root Entry");
        root.Add(cfb::BuildNode::Stream(u"data", cfb::PatternBytes(300, 1)));
        root.Add(cfb::BuildNode::Stream(cfb::ExtendedSignatureName(), cfb::PatternBytes(32, 2)));
        const Bytes original = cfb::Build(root);
        const std::string path = sm::WriteScratch(scratch, "ex-only.msi", original);
        REQUIRE(MsiSigner::StripSignature(path));
        const Bytes now = sm::ReadAll(path);
        CHECK_FALSE(msi::HasExtendedStream(now));
        msi::RequireSameContent(original, now);
        CHECK_FALSE(MsiSigner::StripSignature(path));
    }
    SECTION("signing never creates one")
    {
        for(const std::string &sample : msi::kUnsignedSamples)
        {
            const std::string path = msi::CopySample(scratch, sample);
            for(const std::size_t size : {100u, 4096u, 9000u})
            {
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(size, 9)));
                CHECK_FALSE(msi::HasExtendedStream(sm::ReadAll(path)));
                CHECK(msi::SignaturePaths(sm::ReadAll(path)).size() == 1);
            }
        }
    }
}

TEST_CASE("stripping a package without a signature leaves the file as it was and returns false", "[MsiPreservation][strip]")
{
    seedtest::ScratchDir scratch("msi-keep-strip-none");
    for(const std::string &sample : msi::kUnsignedSamples)
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const std::string path = msi::CopySample(scratch, sample);
        CHECK(MsiSigner::StripSignature(path) == false);
        sm::RequireUnchanged(path, original);
        CHECK(MsiSigner::StripSignature(path) == false);
        sm::RequireUnchanged(path, original);
    }
    // A package that was signed and stripped has none either.
    for(const std::string &sample : msi::kSignedSamples)
    {
        INFO("sample " << sample);
        const std::string path = msi::CopySample(scratch, sample);
        REQUIRE(MsiSigner::StripSignature(path) == true);
        const Bytes stripped = sm::ReadAll(path);
        CHECK(MsiSigner::StripSignature(path) == false);
        sm::RequireUnchanged(path, stripped);
    }
}

TEST_CASE("strip then sign gives the original fingerprint and the signature back", "[MsiPreservation][strip]")
{
    seedtest::ScratchDir scratch("msi-keep-strip-sign");
    for(const std::string &sample : msi::kSignedSamples)
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const std::string fingerprint = msi::RecordedFingerprint(sample);
        const Bytes blob = msi::SampleSignature(sample);
        const std::string path = msi::CopySample(scratch, sample);
        const std::string before = Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest);
        CHECK(before == fingerprint);

        REQUIRE(MsiSigner::StripSignature(path));
        CHECK(Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest) == fingerprint);
        CHECK(MsiSigner::CheckSignature(path).state == State::None);

        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        CHECK(Hex(MsiSigner::ComputeAuthenticodeDigest(path).digest) == fingerprint);
        CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));
        msi::RequireSameContent(original, sm::ReadAll(path));
        RequireWellFormed(sm::ReadAll(path), blob, &original);
        // The signature osslsigncode wrote holds this fingerprint, except where it covers an extended stream.
        const auto state = MsiSigner::CheckSignature(path).state;
        CHECK(state == (sample == "tiny-osslsig-dse.msi" ? State::Mismatch : State::Matches));
    }
}

namespace {

// Sizes alternate across the 4,096 byte cut-off; the largest is 50,000.
const std::size_t kRoundSizes[] = {100, 4096, 4095, 20000, 1, 4097, 50000, 4095, 4096, 99, 5000, 4094, 4097, 3};

} // namespace

TEST_CASE("many rounds of signing and stripping leave the content as it was and the size bounded", "[MsiPreservation][rounds]")
{
    seedtest::ScratchDir scratch("msi-rounds");
    static_assert(sizeof(kRoundSizes) / sizeof(kRoundSizes[0]) >= 10);
    for(const std::string &sample : {std::string("tiny.msi"), std::string("tiny-v4.msi"), std::string("nested.msi"),
                                     std::string("tiny-osslsig-large.msi"), std::string("tiny-osslsig-dse.msi"),
                                     std::string("two-neighbours.msi")})
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);

        // The package without a signature in the library's own layout, and what one signing with each blob gives.
        const std::string bare_path = msi::CopySample(scratch, sample, "bare.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(bare_path, cfb::PatternBytes(10, 1)));
        REQUIRE(MsiSigner::StripSignature(bare_path));
        const Bytes bare = sm::ReadAll(bare_path);
        msi::RequireSameContent(original, bare);

        std::size_t largest = 0;
        for(const std::size_t size : kRoundSizes)
        {
            largest = std::max(largest, size);
        }
        const std::string largest_path = sm::WriteScratch(scratch, "largest.msi", bare);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(largest_path, cfb::PatternBytes(largest, 2)));
        const std::size_t one_signing = sm::ReadAll(largest_path).size();

        SECTION("sign and strip every round")
        {
            const std::string path = msi::CopySample(scratch, sample);
            std::size_t round = 0;
            for(const std::size_t size : kRoundSizes)
            {
                INFO("round " << round++ << " blob of " << size << " bytes");
                const Bytes blob = cfb::PatternBytes(size, static_cast<std::uint8_t>(size));
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                const Bytes now = sm::ReadAll(path);
                CHECK(now.size() <= one_signing);
                msi::RequireSameContent(original, now);
                RequireWellFormed(now, blob, &original);
                CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));

                // Nothing is left from the earlier rounds: the same as signing the bare package once.
                const std::string direct = sm::WriteScratch(scratch, "direct.msi", bare);
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(direct, blob));
                CHECK(now == sm::ReadAll(direct));

                REQUIRE(MsiSigner::StripSignature(path));
                CHECK(sm::ReadAll(path) == bare);
            }
            msi::RequireSameContent(original, sm::ReadAll(path));
            CHECK(sm::ReadAll(path).size() <= bare.size());
        }

        SECTION("sign again every round and strip at the end")
        {
            const std::string path = msi::CopySample(scratch, sample);
            std::size_t round = 0;
            Bytes blob;
            for(const std::size_t size : kRoundSizes)
            {
                INFO("round " << round++ << " blob of " << size << " bytes");
                blob = cfb::PatternBytes(size, static_cast<std::uint8_t>(size + 7));
                REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
                CHECK(sm::ReadAll(path).size() <= one_signing);
            }
            const Bytes last = sm::ReadAll(path);
            const std::string direct = sm::WriteScratch(scratch, "direct.msi", bare);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(direct, blob));
            CHECK(last == sm::ReadAll(direct));
            msi::RequireSameContent(original, last);
            REQUIRE(MsiSigner::StripSignature(path));
            CHECK(sm::ReadAll(path) == bare);
        }
    }
}

TEST_CASE("the package an earlier version signed is repaired by replace and by strip", "[MsiPreservation][legacy]")
{
    seedtest::ScratchDir scratch("msi-legacy");
    const std::string sample = "legacy-the-seed-0.6.0.msi";
    const Bytes original = sm::LoadSample(sample);
    const Bytes unsigned_original = sm::LoadSample("tiny.msi");
    const std::string fingerprint = msi::RecordedFingerprint(sample);

    SECTION("reading it never says it matches")
    {
        const std::string path = msi::CopySample(scratch, sample);
        MsiSigner::SignatureCheck check;
        REQUIRE_NOTHROW(check = MsiSigner::CheckSignature(path));
        CHECK(check.state != State::Matches);
        CHECK((check.state == State::Unreadable || check.state == State::Mismatch));
        CHECK(Hex(check.computed_digest) == fingerprint);
        sm::RequireUnchanged(path, original);
    }
    SECTION("replacing the signature repairs it")
    {
        for(const std::size_t size : {1426u, 100u, 5000u})
        {
            INFO("blob of " << size << " bytes");
            const std::string path = msi::CopySample(scratch, sample);
            const Bytes blob = cfb::PatternBytes(size, 81);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
            const Bytes repaired = sm::ReadAll(path);
            // The independent reader parses it, finds the entry by searching and sees the same bytes.
            RequireWellFormed(repaired, blob, &original);
            msi::RequireSameContent(unsigned_original, repaired);
            RequireFingerprintIs(path, fingerprint);
            CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));
            // Placement follows the size alone.
            const cfb::Package package(repaired);
            CHECK(package.InMiniStream(*package.FindSignature()) == (size < 4096));
            // It is the package that signing the unsigned original gives.
            const std::string direct = msi::CopySample(scratch, "tiny.msi", "direct.msi");
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(direct, blob));
            CHECK(repaired == sm::ReadAll(direct));
        }
    }
    SECTION("replacing it with a signature that fits the package is read as a match")
    {
        const std::string path = msi::CopySample(scratch, sample);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, msi::SampleSignature("tiny-osslsig-small.msi"), true));
        CHECK(MsiSigner::CheckSignature(path).state == State::Matches);
    }
    SECTION("stripping it removes what the reader cannot reach")
    {
        const std::string path = msi::CopySample(scratch, sample);
        REQUIRE(MsiSigner::StripSignature(path));
        const Bytes stripped = sm::ReadAll(path);
        RequireWellFormed(stripped, std::nullopt, &original);
        msi::RequireSameContent(unsigned_original, stripped);
        RequireFingerprintIs(path, fingerprint);
        CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
        CHECK(MsiSigner::CheckSignature(path).state == State::None);
        CHECK_FALSE(MsiSigner::StripSignature(path));
        // Signing the repaired package once more works.
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, cfb::PatternBytes(200, 3)));
        RequireWellFormed(sm::ReadAll(path), cfb::PatternBytes(200, 3), &original);
    }
}
