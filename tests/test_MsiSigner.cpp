// MsiSigner against the contract of the library, on the installer samples.
//
// The cases named "MsiSigner::EmbedSignature and ExtractSignature round-trip"
// and "MsiSigner::EmbedSignature can replace existing signature" keep their
// names: tests/known-gaps.txt lists them until the writer that places a
// signature by its size exists. They run on every sample (unsigned, and signed
// by another tool) with signatures far below the 4,096-byte cut-off, which is
// where the earlier writer put them in the wrong place.
//
// Before and after listings come from the independent reader of
// CfbReference.hpp; the signature is found by searching the directory with the
// format's ordering.

#include "MsiTestSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
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

std::vector<std::string> AllSamples()
{
    std::vector<std::string> names = msi::kUnsignedSamples;
    names.insert(names.end(), msi::kSignedSamples.begin(), msi::kSignedSamples.end());
    return names;
}

} // namespace

TEST_CASE("MsiSigner::IsMsi detects CFBF files", "[MsiSigner]")
{
    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        CHECK(MsiSigner::IsMsi(seedtest::FixturePath(sample)) == true);
    }
}

TEST_CASE("MsiSigner::IsMsi rejects non-CFBF files", "[MsiSigner]")
{
    auto txtPath = seedtest::FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(txtPath));
    CHECK(MsiSigner::IsMsi(txtPath) == false);

    auto pePath = seedtest::FixturePath("tiny.exe");
    REQUIRE(std::filesystem::exists(pePath));
    CHECK(MsiSigner::IsMsi(pePath) == false);
}

TEST_CASE("MsiSigner::ComputeAuthenticodeDigest computes valid digest", "[MsiSigner]")
{
    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        auto result = MsiSigner::ComputeAuthenticodeDigest(seedtest::FixturePath(sample));

        // SHA-256, equal to the value osslsigncode calculated, and deterministic
        REQUIRE(result.digest.size() == 32);
        CHECK(msi::ToHexString(result.digest) == msi::RecordedFingerprint(sample));
        auto result2 = MsiSigner::ComputeAuthenticodeDigest(seedtest::FixturePath(sample));
        CHECK(result.digest == result2.digest);
    }
}

TEST_CASE("MsiSigner::HasEmbeddedSignature reports the samples", "[MsiSigner]")
{
    for(const std::string &sample : msi::kUnsignedSamples)
    {
        INFO("sample " << sample);
        CHECK(MsiSigner::HasEmbeddedSignature(seedtest::FixturePath(sample)) == false);
    }
    for(const std::string &sample : msi::kSignedSamples)
    {
        INFO("sample " << sample);
        CHECK(MsiSigner::HasEmbeddedSignature(seedtest::FixturePath(sample)) == true);
    }
}

TEST_CASE("MsiSigner::EmbedSignature and ExtractSignature round-trip", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        auto tempPath = msi::CopySample(scratch, sample);

        // Create a fake PKCS#7 blob (not valid CMS, just for round-trip testing)
        std::vector<std::uint8_t> fakePkcs7(128);
        for (size_t i = 0; i < fakePkcs7.size(); i++)
            fakePkcs7[i] = static_cast<std::uint8_t>(i & 0xFF);

        // Embed signature
        MsiSigner::EmbedSignature(tempPath, fakePkcs7);

        // Now should have embedded signature
        CHECK(MsiSigner::HasEmbeddedSignature(tempPath) == true);

        // Extract and verify round-trip
        auto extracted = MsiSigner::ExtractSignature(tempPath);
        REQUIRE(extracted.has_value());
        CHECK(extracted->size() == fakePkcs7.size());
        CHECK(*extracted == fakePkcs7);

        // The independent reader finds the same bytes by searching, and nothing else changed
        const Bytes written = sm::ReadAll(tempPath);
        const auto found = msi::FoundSignature(written);
        REQUIRE(found.has_value());
        CHECK(*found == fakePkcs7);
        msi::RequireSameContent(original, written);
    }
}

TEST_CASE("MsiSigner::StripSignature removes embedded signature", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : AllSamples())
    {
        for(const std::size_t size : {64u, 5000u})
        {
            INFO("sample " << sample << " signature of " << size << " bytes");
            const Bytes original = sm::LoadSample(sample);
            auto tempPath = msi::CopySample(scratch, sample);

            // Embed a fake signature first
            std::vector<std::uint8_t> fakePkcs7(size, 0xAA);
            MsiSigner::EmbedSignature(tempPath, fakePkcs7);
            REQUIRE(MsiSigner::HasEmbeddedSignature(tempPath) == true);

            // Strip it: something was removed
            CHECK(MsiSigner::StripSignature(tempPath) == true);

            // Should be unsigned again
            CHECK(MsiSigner::HasEmbeddedSignature(tempPath) == false);

            // Extract should return nullopt
            auto extracted = MsiSigner::ExtractSignature(tempPath);
            CHECK_FALSE(extracted.has_value());

            const Bytes stripped = sm::ReadAll(tempPath);
            CHECK_FALSE(msi::FoundSignature(stripped).has_value());
            CHECK_FALSE(msi::HasExtendedStream(stripped));
            msi::RequireSameContent(original, stripped);
        }
    }
}

TEST_CASE("MsiSigner::StripSignature returns bool", "[MsiSigner]")
{
    static_assert(std::is_same_v<decltype(MsiSigner::StripSignature(std::declval<const std::string &>())), bool>);
    SUCCEED();
}

TEST_CASE("MsiSigner::StripSignature on a signature of another tool", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : msi::kSignedSamples)
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        auto tempPath = msi::CopySample(scratch, sample);
        REQUIRE(MsiSigner::HasEmbeddedSignature(tempPath) == true);

        CHECK(MsiSigner::StripSignature(tempPath) == true);
        CHECK(MsiSigner::HasEmbeddedSignature(tempPath) == false);
        CHECK_FALSE(MsiSigner::ExtractSignature(tempPath).has_value());

        const Bytes stripped = sm::ReadAll(tempPath);
        CHECK_FALSE(msi::FoundSignature(stripped).has_value());
        CHECK_FALSE(msi::HasExtendedStream(stripped));
        msi::RequireSameContent(original, stripped);
        CHECK(msi::ToHexString(MsiSigner::ComputeAuthenticodeDigest(tempPath).digest) == msi::RecordedFingerprint(sample));

        // A second strip finds nothing and leaves the file as it is
        CHECK(MsiSigner::StripSignature(tempPath) == false);
        sm::RequireUnchanged(tempPath, stripped);
    }
}

TEST_CASE("MsiSigner::StripSignature returns false and changes nothing for an unsigned package", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : {std::string("tiny.msi"), std::string("tiny-v4.msi"), std::string("nested.msi")})
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        auto tempPath = msi::CopySample(scratch, sample);
        CHECK(MsiSigner::StripSignature(tempPath) == false);
        sm::RequireUnchanged(tempPath, original);
    }
}

TEST_CASE("MsiSigner re-signing repairs a package signed by an earlier version", "[MsiSigner]")
{
    // The earlier version wrote the signature where no search by name reaches
    // it. The old signature is never read: it is dropped, and the new one is
    // placed at its sorted position and found by searching.
    seedtest::ScratchDir scratch;
    const Bytes original = sm::LoadSample("legacy-the-seed-0.6.0.msi");
    REQUIRE_FALSE(msi::FoundSignature(original).has_value());

    auto signedPath = msi::CopySample(scratch, "legacy-the-seed-0.6.0.msi", "signed.msi");
    const std::vector<std::uint8_t> blob = cfb::PatternBytes(1426, 4);
    MsiSigner::EmbedSignature(signedPath, blob);
    const Bytes repaired = sm::ReadAll(signedPath);
    const auto found = msi::FoundSignature(repaired);
    REQUIRE(found.has_value());
    CHECK(*found == blob);
    CHECK(MsiSigner::ExtractSignature(signedPath) == std::optional<Bytes>(blob));
    CHECK(msi::ToHexString(MsiSigner::ComputeAuthenticodeDigest(signedPath).digest) ==
          msi::RecordedFingerprint("legacy-the-seed-0.6.0.msi"));

    // Stripping removes it as well, whatever its chain looks like.
    auto strippedPath = msi::CopySample(scratch, "legacy-the-seed-0.6.0.msi", "stripped.msi");
    CHECK(MsiSigner::StripSignature(strippedPath) == true);
    CHECK_FALSE(MsiSigner::HasEmbeddedSignature(strippedPath));
    const Bytes stripped = sm::ReadAll(strippedPath);
    CHECK_FALSE(msi::HasExtendedStream(stripped));
    // It was made from tiny.msi: everything but the signature is its content.
    msi::RequireSameContent(sm::LoadSample("tiny.msi"), stripped);
}

TEST_CASE("MsiSigner::StripSignature drops the extended stream", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    const Bytes original = sm::LoadSample("tiny-osslsig-dse.msi");
    REQUIRE(msi::HasExtendedStream(original));
    auto tempPath = msi::CopySample(scratch, "tiny-osslsig-dse.msi");
    CHECK(MsiSigner::StripSignature(tempPath) == true);
    const Bytes stripped = sm::ReadAll(tempPath);
    CHECK_FALSE(msi::HasExtendedStream(stripped));
    CHECK(cfb::CountBelowRoot(cfb::Package(stripped)) == cfb::CountBelowRoot(cfb::Package(original)) - 2);
}

TEST_CASE("MsiSigner::EmbedSignature drops the extended stream and never creates one", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;

    auto signedPath = msi::CopySample(scratch, "tiny-osslsig-dse.msi");
    REQUIRE(msi::HasExtendedStream(sm::ReadAll(signedPath)));
    MsiSigner::EmbedSignature(signedPath, std::vector<std::uint8_t>(300, 0x5A));
    CHECK_FALSE(msi::HasExtendedStream(sm::ReadAll(signedPath)));

    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        auto path = msi::CopySample(scratch, sample);
        MsiSigner::EmbedSignature(path, std::vector<std::uint8_t>(300, 0x5B));
        CHECK_FALSE(msi::HasExtendedStream(sm::ReadAll(path)));
    }
}

TEST_CASE("MsiSigner throws on non-CFBF file", "[MsiSigner]")
{
    auto fixturePath = seedtest::FixturePath("plain.txt");
    REQUIRE(std::filesystem::exists(fixturePath));

    CHECK_THROWS_AS(MsiSigner::ComputeAuthenticodeDigest(fixturePath), std::runtime_error);
    CHECK_THROWS_AS(MsiSigner::HasEmbeddedSignature(fixturePath), std::runtime_error);
}

TEST_CASE("MsiSigner::ComputeAuthenticodeDigest is stable after embed", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        auto tempPath = msi::CopySample(scratch, sample);

        // Get digest before signing
        auto digestBefore = MsiSigner::ComputeAuthenticodeDigest(tempPath);

        // Embed a fake signature
        std::vector<std::uint8_t> fakePkcs7(128, 0xBB);
        MsiSigner::EmbedSignature(tempPath, fakePkcs7);

        // Get digest after signing — should remain the same because
        // the Authenticode digest excludes \x05DigitalSignature
        auto digestAfter = MsiSigner::ComputeAuthenticodeDigest(tempPath);
        CHECK(digestBefore.digest == digestAfter.digest);

        MsiSigner::StripSignature(tempPath);
        CHECK(MsiSigner::ComputeAuthenticodeDigest(tempPath).digest == digestBefore.digest);
    }
}

TEST_CASE("MsiSigner::EmbedSignature can replace existing signature", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        const Bytes original = sm::LoadSample(sample);
        auto tempPath = msi::CopySample(scratch, sample);

        // Embed first signature
        std::vector<std::uint8_t> sig1(64, 0x11);
        MsiSigner::EmbedSignature(tempPath, sig1);
        REQUIRE(MsiSigner::HasEmbeddedSignature(tempPath) == true);

        auto ext1 = MsiSigner::ExtractSignature(tempPath);
        REQUIRE(ext1.has_value());
        CHECK(*ext1 == sig1);

        // Embed replacement signature
        std::vector<std::uint8_t> sig2(256, 0x22);
        MsiSigner::EmbedSignature(tempPath, sig2);

        auto ext2 = MsiSigner::ExtractSignature(tempPath);
        REQUIRE(ext2.has_value());
        CHECK(ext2->size() == sig2.size());
        CHECK(*ext2 == sig2);

        // Only one signature entry remains and everything else is as it was
        const Bytes written = sm::ReadAll(tempPath);
        CHECK(cfb::CountBelowRoot(cfb::Package(written)) == msi::Content(original).size());
        msi::RequireSameContent(original, written);
    }
}

TEST_CASE("MsiSigner::EmbedSignature output is deterministic", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;
    for(const std::string &sample : AllSamples())
    {
        INFO("sample " << sample);
        const std::vector<std::uint8_t> blob = cfb::PatternBytes(1444, 2);
        auto one = msi::CopySample(scratch, sample, "one.msi");
        auto two = msi::CopySample(scratch, sample, "two.msi");
        MsiSigner::EmbedSignature(one, blob);
        MsiSigner::EmbedSignature(two, blob);
        CHECK(sm::ReadAll(one) == sm::ReadAll(two));

        // Signing again gives the same bytes; so does stripping first and signing the bare package
        const Bytes once = sm::ReadAll(one);
        MsiSigner::EmbedSignature(one, blob);
        CHECK(sm::ReadAll(one) == once);
        MsiSigner::StripSignature(two);
        MsiSigner::EmbedSignature(two, blob);
        CHECK(sm::ReadAll(two) == once);
    }
}

TEST_CASE("MsiSigner refusals leave the file byte-identical", "[MsiSigner]")
{
    seedtest::ScratchDir scratch;

    SECTION("an empty signature")
    {
        for(const std::string &sample : AllSamples())
        {
            INFO("sample " << sample);
            const Bytes original = sm::LoadSample(sample);
            auto path = msi::CopySample(scratch, sample);
            CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, std::vector<std::uint8_t>()), std::runtime_error);
            sm::RequireUnchanged(path, original);
        }
    }
    SECTION("a file that is not a package")
    {
        const Bytes original = sm::LoadSample("plain.txt");
        auto path = msi::CopySample(scratch, "plain.txt");
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, std::vector<std::uint8_t>(100, 1)), std::runtime_error);
        sm::RequireUnchanged(path, original);
        CHECK_THROWS_AS(MsiSigner::StripSignature(path), std::runtime_error);
        sm::RequireUnchanged(path, original);
    }
    SECTION("a package cut short")
    {
        const Bytes tiny = sm::LoadSample("tiny.msi");
        const Bytes cut = sm::Truncate(tiny, tiny.size() - 300);
        auto path = sm::WriteScratch(scratch, "cut.msi", cut);
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, std::vector<std::uint8_t>(100, 1)), std::runtime_error);
        sm::RequireUnchanged(path, cut);
        CHECK_THROWS_AS(MsiSigner::StripSignature(path), std::runtime_error);
        sm::RequireUnchanged(path, cut);
    }
    SECTION("a file that does not exist")
    {
        auto path = scratch.File("absent.msi");
        CHECK_THROWS_AS(MsiSigner::EmbedSignature(path, std::vector<std::uint8_t>(100, 1)), std::runtime_error);
        CHECK_THROWS_AS(MsiSigner::StripSignature(path), std::runtime_error);
        CHECK_FALSE(std::filesystem::exists(path));
    }
}
