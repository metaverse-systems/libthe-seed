// The four states of MsiSigner::CheckSignature and the digest check of
// MsiSigner::EmbedSignature.
//
//   None        no signature stream, or one of size zero
//   Matches     a readable signature whose SHA-256 fingerprint equals the
//               fingerprint of the package
//   Mismatch    a readable signature whose fingerprint differs
//   Unreadable  a signature that cannot be read: not a signature structure,
//               cut short, or another digest algorithm
//
// The signatures used are the ones osslsigncode wrote for the samples
// (fixtures/tiny-osslsig-*.msi): their stored fingerprint is the recorded one
// of the package they were made for, so embedding one into the unsigned
// sibling gives a signature that matches, and changing a stream of the package
// afterwards makes it a mismatch. A wrong fingerprint is made by changing one
// byte of the fingerprint inside such a signature; the cryptography is never
// checked (and never needs to be).

#include "MsiTestSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::msi::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
namespace msi = seedtest::msi;
using State = MsiSigner::SignatureState;

// An unsigned sample, and a sample the other tool signed for its content.
struct Pair
{
    const char *unsigned_sample;
    const char *signed_sample;
};

const Pair kPairs[] = {
    {"tiny.msi", "tiny-osslsig-small.msi"},
    {"tiny-v4.msi", "tiny-osslsig-large.msi"},
    {"nested.msi", "nested-osslsig.msi"},
};

// Position of the 32 bytes of the package fingerprint inside a signature.
std::size_t FingerprintAt(const Bytes &blob, const std::string &fingerprint_hex)
{
    const Bytes needle = msi::FromHex(fingerprint_hex);
    const auto at = std::search(blob.begin(), blob.end(), needle.begin(), needle.end());
    REQUIRE(at != blob.end());
    return static_cast<std::size_t>(at - blob.begin());
}

// The same signature with one byte of the fingerprint changed.
Bytes WithWrongFingerprint(Bytes blob, const std::string &fingerprint_hex)
{
    blob[FingerprintAt(blob, fingerprint_hex) + 5] ^= 0x40;
    return blob;
}

// The same signature naming another digest algorithm (SHA-256 becomes SHA-512).
Bytes WithOtherAlgorithm(Bytes blob)
{
    const Bytes sha256 = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
    std::size_t changed = 0;
    for(auto at = std::search(blob.begin(), blob.end(), sha256.begin(), sha256.end()); at != blob.end();
        at = std::search(at + 1, blob.end(), sha256.begin(), sha256.end()))
    {
        *(at + 8) = 0x03;
        ++changed;
    }
    REQUIRE(changed > 0);
    return blob;
}

std::string Prefix(const std::string &hex)
{
    return hex.substr(0, 8);
}

} // namespace

TEST_CASE("the check reports no signature for an unsigned package", "[MsiCheckSignature][none]")
{
    seedtest::ScratchDir scratch("msi-check-none");
    for(const std::string &sample : msi::kUnsignedSamples)
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        const std::string path = msi::CopySample(scratch, sample);
        const MsiSigner::SignatureCheck check = MsiSigner::CheckSignature(path);
        CHECK(check.state == State::None);
        CHECK(check.stored_digest.empty());
        CHECK(msi::ToHexString(check.computed_digest) == msi::RecordedFingerprint(sample));
        sm::RequireUnchanged(path, original);
    }
}

TEST_CASE("the check reports no signature for a signature of size zero", "[MsiCheckSignature][none]")
{
    // An entry with a size of zero is an empty signature, not a damaged one.
    cfb::BuildNode root = cfb::ToBuildTree(cfb::Package(sm::LoadSample("tiny.msi")), true);
    root.Add(cfb::BuildNode::Stream(cfb::SignatureName(), Bytes()));
    seedtest::ScratchDir scratch("msi-check-zero");
    const Bytes bytes = cfb::Build(root);
    const std::string path = sm::WriteScratch(scratch, "zero.msi", bytes);
    const MsiSigner::SignatureCheck check = MsiSigner::CheckSignature(path);
    CHECK(check.state == State::None);
    CHECK(msi::ToHexString(check.computed_digest) == msi::RecordedFingerprint("tiny.msi"));
    sm::RequireUnchanged(path, bytes);
}

TEST_CASE("the check reports an unreadable signature for a package an old version signed", "[MsiCheckSignature][unreadable]")
{
    // The signature stream of this sample is in the wrong kind of sectors for
    // its size, so it cannot be read by size: a state, not an exception.
    seedtest::ScratchDir scratch("msi-check-legacy");
    const Bytes original = sm::LoadSample("legacy-the-seed-0.6.0.msi");
    const std::string path = msi::CopySample(scratch, "legacy-the-seed-0.6.0.msi");
    MsiSigner::SignatureCheck check;
    REQUIRE_NOTHROW(check = MsiSigner::CheckSignature(path));
    CHECK(check.state == State::Unreadable);
    CHECK_FALSE(check.detail.empty());
    CHECK(msi::ToHexString(check.computed_digest) == msi::RecordedFingerprint("legacy-the-seed-0.6.0.msi"));
    sm::RequireUnchanged(path, original);
}

TEST_CASE("the check reports a match for a signature that holds the fingerprint of the package", "[MsiCheckSignature][matches]")
{
    seedtest::ScratchDir scratch("msi-check-match");
    SECTION("the signatures osslsigncode wrote")
    {
        for(const Pair &pair : kPairs)
        {
            INFO("sample " << pair.signed_sample);
            const std::string path = msi::CopySample(scratch, pair.signed_sample);
            const MsiSigner::SignatureCheck check = MsiSigner::CheckSignature(path);
            CHECK(check.state == State::Matches);
            CHECK(msi::ToHexString(check.stored_digest) == msi::RecordedFingerprint(pair.signed_sample));
            CHECK(check.stored_digest == check.computed_digest);
        }
        const std::string path = msi::CopySample(scratch, "two-neighbours.msi");
        CHECK(MsiSigner::CheckSignature(path).state == State::Matches);
    }
    SECTION("the same signature embedded by the library")
    {
        for(const Pair &pair : kPairs)
        {
            INFO("package " << pair.unsigned_sample << " with the signature of " << pair.signed_sample);
            const Bytes blob = msi::SampleSignature(pair.signed_sample);
            const std::string path = msi::CopySample(scratch, pair.unsigned_sample);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
            const MsiSigner::SignatureCheck check = MsiSigner::CheckSignature(path);
            CHECK(check.state == State::Matches);
            CHECK(msi::ToHexString(check.stored_digest) == msi::RecordedFingerprint(pair.unsigned_sample));
            CHECK(check.stored_digest == check.computed_digest);
            CHECK(check.computed_digest == MsiSigner::ComputeAuthenticodeDigest(path).digest);
        }
    }
}

TEST_CASE("the check reports a mismatch for a signature with another fingerprint", "[MsiCheckSignature][mismatch]")
{
    seedtest::ScratchDir scratch("msi-check-mismatch");
    for(const Pair &pair : kPairs)
    {
        INFO("package " << pair.unsigned_sample);
        const std::string recorded = msi::RecordedFingerprint(pair.unsigned_sample);
        const Bytes blob = WithWrongFingerprint(msi::SampleSignature(pair.signed_sample), recorded);
        const std::string path = msi::CopySample(scratch, pair.unsigned_sample);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        const MsiSigner::SignatureCheck check = MsiSigner::CheckSignature(path);
        CHECK(check.state == State::Mismatch);
        CHECK(check.stored_digest.size() == 32);
        CHECK(check.stored_digest != check.computed_digest);
        CHECK(msi::ToHexString(check.computed_digest) == recorded);
        INFO("detail: " << check.detail);
        CHECK(check.detail.find("does not match") != std::string::npos);
        CHECK(check.detail.find(Prefix(msi::ToHexString(check.stored_digest))) != std::string::npos);
        CHECK(check.detail.find(Prefix(recorded)) != std::string::npos);
    }
}

TEST_CASE("the check reports a mismatch when a stream is changed after signing", "[MsiCheckSignature][mismatch]")
{
    seedtest::ScratchDir scratch("msi-check-tamper");
    for(const Pair &pair : kPairs)
    {
        INFO("package " << pair.unsigned_sample);
        const std::string path = msi::CopySample(scratch, pair.unsigned_sample);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, msi::SampleSignature(pair.signed_sample)));
        REQUIRE(MsiSigner::CheckSignature(path).state == State::Matches);

        // Change one byte of the first non-empty stream of the package (not the signature).
        Bytes bytes = sm::ReadAll(path);
        const cfb::Package package(bytes);
        std::uint64_t at = 0;
        for(std::uint32_t index = 1; index < package.Entries().size() && at == 0; ++index)
        {
            const cfb::DirEntry &entry = package.Entries()[index];
            if(entry.type == cfb::kTypeStream && entry.size > 0 && !cfb::IsSignatureName(entry.name))
            {
                at = package.StreamExtents(index).front().offset;
            }
        }
        REQUIRE(at != 0);
        bytes[at] ^= 0x01;
        const std::string tampered = sm::WriteScratch(scratch, "tampered.msi", bytes);

        const MsiSigner::SignatureCheck check = MsiSigner::CheckSignature(tampered);
        CHECK(check.state == State::Mismatch);
        CHECK(msi::ToHexString(check.stored_digest) == msi::RecordedFingerprint(pair.unsigned_sample));
        CHECK(check.stored_digest != check.computed_digest);
        CHECK(msi::ToHexString(check.computed_digest) == cfb::Fingerprint(bytes));
        CHECK(check.detail.find("does not match") != std::string::npos);
        // Present and readable, but not a match: the signature is still there.
        CHECK(MsiSigner::HasEmbeddedSignature(tampered));
        sm::RequireUnchanged(tampered, bytes);
    }
}

TEST_CASE("the check reports an unreadable signature", "[MsiCheckSignature][unreadable]")
{
    seedtest::ScratchDir scratch("msi-check-unreadable");
    const Bytes good = msi::SampleSignature("tiny-osslsig-small.msi");
    struct Item
    {
        std::string label;
        Bytes blob;
    };
    std::vector<Item> items;
    items.push_back({"bytes that are not a signature", cfb::PatternBytes(300, 17)});
    items.push_back({"a single byte", Bytes{0x30}});
    items.push_back({"a signature cut short", Bytes(good.begin(), good.begin() + 100)});
    items.push_back({"a signature without its last bytes", Bytes(good.begin(), good.end() - 10)});
    items.push_back({"a signature for another digest algorithm", WithOtherAlgorithm(good)});
    items.push_back({"a large block of garbage", cfb::PatternBytes(6000, 18)});

    for(const Item &item : items)
    {
        INFO(item.label);
        const std::string path = msi::CopySample(scratch, "tiny.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, item.blob));
        const Bytes written = sm::ReadAll(path);
        MsiSigner::SignatureCheck check;
        REQUIRE_NOTHROW(check = MsiSigner::CheckSignature(path));
        CHECK(check.state == State::Unreadable);
        CHECK(check.stored_digest.empty());
        CHECK(msi::ToHexString(check.computed_digest) == msi::RecordedFingerprint("tiny.msi"));
        CHECK_FALSE(check.detail.empty());
        sm::RequireUnchanged(path, written);
    }
}

TEST_CASE("the check reports an unreadable signature whose chain disagrees with its size", "[MsiCheckSignature][unreadable]")
{
    // The size of a signature in the directory is changed so that the chain
    // no longer covers it: a bad signature is a state, not an exception.
    seedtest::ScratchDir scratch("msi-check-chain");
    const Bytes original = sm::LoadSample("tiny-osslsig-large.msi");
    const cfb::Package package(original);
    const auto found = package.FindSignature();
    REQUIRE(found.has_value());
    for(const std::uint64_t size : {std::uint64_t{20000}, std::uint64_t{3000}})
    {
        INFO("recorded size " << size);
        Bytes bytes = original;
        sm::PatchLE<std::uint64_t>(bytes, package.EntryOffset(*found) + 120, size);
        const std::string path = sm::WriteScratch(scratch, "chain.msi", bytes);
        MsiSigner::SignatureCheck check;
        REQUIRE_NOTHROW(check = MsiSigner::CheckSignature(path));
        CHECK(check.state == State::Unreadable);
        CHECK_FALSE(check.detail.empty());
        sm::RequireUnchanged(path, bytes);
    }
}

TEST_CASE("the check throws only for a package that cannot be read", "[MsiCheckSignature][package]")
{
    seedtest::ScratchDir scratch("msi-check-package");
    CHECK_THROWS_AS(MsiSigner::CheckSignature(seedtest::FixturePath("plain.txt")), std::runtime_error);
    CHECK_THROWS_AS(MsiSigner::CheckSignature(scratch.File("absent.msi")), std::runtime_error);

    const Bytes tiny = sm::LoadSample("tiny.msi");
    const std::string cut = sm::WriteScratch(scratch, "cut.msi", sm::Truncate(tiny, tiny.size() - 200));
    CHECK_THROWS_AS(MsiSigner::CheckSignature(cut), std::runtime_error);

    // Two entries with one name: the package has no fingerprint.
    Bytes twins = tiny;
    const std::uint64_t first = sm::CfbDirectoryEntryOffset(twins, 1);
    const std::uint64_t second = sm::CfbDirectoryEntryOffset(twins, 2);
    for(std::uint64_t i = 0; i < 66; ++i)
    {
        twins[second + i] = twins[first + i];
    }
    const std::string path = sm::WriteScratch(scratch, "twins.msi", twins);
    CHECK_THROWS_AS(MsiSigner::CheckSignature(path), std::runtime_error);
    sm::RequireUnchanged(path, twins);
}

TEST_CASE("embedding with the digest check refuses a signature with another fingerprint", "[MsiCheckSignature][embed]")
{
    seedtest::ScratchDir scratch("msi-check-refuse");
    for(const Pair &pair : kPairs)
    {
        INFO("package " << pair.unsigned_sample);
        const std::string recorded = msi::RecordedFingerprint(pair.unsigned_sample);
        const Bytes wrong = WithWrongFingerprint(msi::SampleSignature(pair.signed_sample), recorded);
        const Bytes original = sm::LoadSample(pair.unsigned_sample);
        const std::string path = msi::CopySample(scratch, pair.unsigned_sample);

        try
        {
            MsiSigner::EmbedSignature(path, wrong, true);
            FAIL("a signature with another fingerprint was embedded");
        }
        catch(const std::runtime_error &error)
        {
            const std::string message = error.what();
            INFO("message: " << message);
            CHECK(message.find(path) != std::string::npos);
            CHECK(message.find("signature does not match the package contents") != std::string::npos);
            CHECK(message.find("stored ") != std::string::npos);
            CHECK(message.find("computed " + Prefix(recorded)) != std::string::npos);
        }
        sm::RequireUnchanged(path, original);

        // The same refusal when the package already holds a signature: it stays as it was.
        const Bytes good = msi::SampleSignature(pair.signed_sample);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, good, true));
        const Bytes signed_bytes = sm::ReadAll(path);
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, wrong, true), std::runtime_error);
        sm::RequireUnchanged(path, signed_bytes);
    }
}

TEST_CASE("embedding with the digest check refuses a signature it cannot read", "[MsiCheckSignature][embed]")
{
    seedtest::ScratchDir scratch("msi-check-refuse-unreadable");
    const Bytes original = sm::LoadSample("tiny.msi");
    const std::string path = msi::CopySample(scratch, "tiny.msi");
    for(const Bytes &blob : {cfb::PatternBytes(300, 3), Bytes{0x30, 0x03, 0x02, 0x01, 0x00}, WithOtherAlgorithm(msi::SampleSignature("tiny-osslsig-small.msi"))})
    {
        INFO("blob of " << blob.size() << " bytes");
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, blob, true), std::runtime_error);
        sm::RequireUnchanged(path, original);
    }
}

TEST_CASE("embedding with the digest check accepts a signature that matches", "[MsiCheckSignature][embed]")
{
    seedtest::ScratchDir scratch("msi-check-accept");
    for(const Pair &pair : kPairs)
    {
        INFO("package " << pair.unsigned_sample);
        const Bytes blob = msi::SampleSignature(pair.signed_sample);
        const std::string checked = msi::CopySample(scratch, pair.unsigned_sample, "checked.msi");
        const std::string plain = msi::CopySample(scratch, pair.unsigned_sample, "plain.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(checked, blob, true));
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(plain, blob));
        // The check changes nothing about what is written.
        CHECK(sm::ReadAll(checked) == sm::ReadAll(plain));
        CHECK(MsiSigner::CheckSignature(checked).state == State::Matches);
        const auto extracted = MsiSigner::ExtractSignature(checked);
        REQUIRE(extracted.has_value());
        CHECK(*extracted == blob);
    }
}

TEST_CASE("embedding with two arguments accepts any non-empty signature", "[MsiCheckSignature][embed]")
{
    seedtest::ScratchDir scratch("msi-check-two");
    const std::string path = msi::CopySample(scratch, "tiny.msi");
    const Bytes garbage = cfb::PatternBytes(500, 8);
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, garbage));
    const auto extracted = MsiSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    CHECK(*extracted == garbage);
    CHECK(MsiSigner::CheckSignature(path).state == State::Unreadable);

    // A signature with another fingerprint is accepted without the check, and reported as a mismatch.
    const Bytes wrong = WithWrongFingerprint(msi::SampleSignature("tiny-osslsig-small.msi"), msi::RecordedFingerprint("tiny.msi"));
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, wrong));
    CHECK(MsiSigner::CheckSignature(path).state == State::Mismatch);
    // Explicitly turned off, the same.
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, wrong, false));
    CHECK(MsiSigner::CheckSignature(path).state == State::Mismatch);
}

TEST_CASE("replacing a signature made by another tool is checked as well", "[MsiCheckSignature][embed]")
{
    // The signature to replace is never read: a damaged one does not stop the
    // replacement, and the new one is checked against the package.
    seedtest::ScratchDir scratch("msi-check-replace");
    for(const std::string &sample : {std::string("tiny-osslsig-small.msi"), std::string("tiny-osslsig-large.msi"),
                                     std::string("tiny-osslsig-dse.msi")})
    {
        INFO("sample " << sample);
        const std::string path = msi::CopySample(scratch, sample);
        const Bytes original = sm::ReadAll(path);
        const Bytes wrong = WithWrongFingerprint(msi::SampleSignature("tiny-osslsig-small.msi"), msi::RecordedFingerprint("tiny.msi"));
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, wrong, true), std::runtime_error);
        sm::RequireUnchanged(path, original);

        const Bytes good = msi::SampleSignature("tiny-osslsig-small.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, good, true));
        CHECK(MsiSigner::CheckSignature(path).state == State::Matches);
        CHECK_FALSE(msi::HasExtendedStream(sm::ReadAll(path)));
    }
}

TEST_CASE("the check carries the stored and computed fingerprints as bytes", "[MsiCheckSignature][types]")
{
    static_assert(std::is_same_v<decltype(MsiSigner::CheckSignature(std::declval<const std::string &>())),
                                 MsiSigner::SignatureCheck>);
    static_assert(std::is_same_v<decltype(MsiSigner::SignatureCheck::stored_digest), std::vector<std::uint8_t>>);
    static_assert(std::is_same_v<decltype(MsiSigner::SignatureCheck::computed_digest), std::vector<std::uint8_t>>);
    static_assert(std::is_same_v<decltype(MsiSigner::SignatureCheck::detail), std::string>);
    static_assert(std::is_enum_v<MsiSigner::SignatureState>);
    const MsiSigner::SignatureCheck fresh;
    CHECK(fresh.state == State::None);
    CHECK(fresh.stored_digest.empty());
    CHECK(fresh.computed_digest.empty());
    CHECK(fresh.detail.empty());
}
