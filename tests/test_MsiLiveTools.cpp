// Installer packages the library writes, read by programs that are not part of
// this project.
//
//   osslsigncode   reads the signature back (extract-signature returns the
//                  embedded bytes) and calculates the same fingerprint as the
//                  one stored in the signature ("Current DigitalSignature"
//                  equals "Calculated DigitalSignature"), with none of the
//                  structure errors the earlier library caused ("Failed to get
//                  a next mini sector address"). Trust is not checked: the
//                  throw-away certificate is not trusted, so the lines
//                  looked at are the digests and the structure messages, not
//                  the exit status.
//   Wine           msi-open.exe (fixtures/src/msi_open.c, built with mingw)
//                  opens the package with StgOpenStorageEx, looks
//                  \005DigitalSignature up by name with IStorage::OpenStream
//                  and compares its bytes with the embedded signature. This is
//                  the lookup that returned STG_E_FILENOTFOUND for packages
//                  written by earlier versions. Wine's storage code is a
//                  reimplementation: this is evidence, not a statement about
//                  Windows.
//
// Every check begins with seedtest::RequireTool: a tool that is not installed
// fails the test, unless SEED_TOOLS_OPTIONAL=1, which prints "NOT RUN: <tool>
// not installed" and leaves that check out.
//
// The packages whose bytes are recorded in msi-reference.txt (signed-out lines)
// are the ones written here: setting SEED_MSI_WRITE_SIGNED to a folder makes the
// test below write each one there as <sample>--<blob>.msi, and
// fixtures/regenerate.sh --signed-out FOLDER lets osslsigncode read them before
// it records their SHA-256.
//
// A control for each program shows that the check can fail: osslsigncode run
// on the package an earlier version signed reports a structure error, and
// msi-open.exe run on it does not find the stream.

#include "MsiTestSupport.hpp"
#include "ToolSupport.hpp"

#include <libthe-seed/MsiSigner.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::msi::Bytes;
namespace sm = seedtest::malformed;
namespace cfb = seedtest::cfb;
namespace msi = seedtest::msi;

// Size of the DER structure at the start of a blob (a SEQUENCE).
std::size_t DerLength(const Bytes &blob)
{
    REQUIRE(blob.size() >= 2);
    REQUIRE(blob[0] == 0x30);
    if(blob[1] < 0x80)
    {
        return 2u + blob[1];
    }
    const std::size_t count = blob[1] & 0x7Fu;
    REQUIRE(count >= 1);
    REQUIRE(count <= 4);
    REQUIRE(blob.size() >= 2 + count);
    std::size_t length = 0;
    for(std::size_t i = 0; i < count; ++i)
    {
        length = (length << 8) | blob[2 + i];
    }
    return 2 + count + length;
}

struct NamedBlob
{
    std::string label;
    Bytes bytes;
};

// One blob of every size class the library places differently, for a sample:
// a real signature osslsigncode wrote that is smaller than the cut-off, a
// larger one (or, where osslsigncode wrote none for that package, the small
// one padded to 7,473 bytes), and the small one padded with zeros to sizes on
// both sides of the cut-off (the structure at the front is still a valid
// signature; the padding is part of the stream). Each holds the fingerprint of
// the sample's package, so osslsigncode finds the two digests equal.
std::vector<NamedBlob> Blobs(const std::string &sample)
{
    std::string small_from = "tiny-osslsig-small.msi";
    std::string large_from = "tiny-osslsig-large.msi";
    if(sample == "nested.msi" || sample == "nested-osslsig.msi")
    {
        small_from = "nested-osslsig.msi";
        large_from.clear();
    }
    else if(sample == "two-neighbours.msi")
    {
        small_from = "two-neighbours.msi";
        large_from.clear();
    }
    std::vector<NamedBlob> out;
    const Bytes small = msi::SampleSignature(small_from);
    out.push_back({"1444 bytes (mini stream)", small});
    for(const std::size_t size : {4095u, 4096u, 4097u, 20000u})
    {
        Bytes padded = small;
        padded.resize(size, 0);
        out.push_back({std::to_string(size) + " bytes (small signature padded)", padded});
    }
    Bytes large = large_from.empty() ? small : msi::SampleSignature(large_from);
    large.resize(7473, 0);
    out.push_back({"7473 bytes (ordinary sectors)", large});
    return out;
}

// The value after "<label> : " on a line of osslsigncode's output, lower case.
std::string Field(const std::string &output, const std::string &label)
{
    const std::size_t at = output.find(label);
    if(at == std::string::npos)
    {
        return "";
    }
    std::size_t colon = output.find(':', at);
    if(colon == std::string::npos)
    {
        return "";
    }
    ++colon;
    while(colon < output.size() && output[colon] == ' ')
    {
        ++colon;
    }
    std::string value;
    while(colon < output.size() && std::isxdigit(static_cast<unsigned char>(output[colon])))
    {
        value += static_cast<char>(std::tolower(static_cast<unsigned char>(output[colon])));
        ++colon;
    }
    return value;
}

const char *const kStructureErrors[] = {"Failed to get a next", "Corrupted", "data error", "Failed to extract",
                                        "Failed to read", "Failed to parse", "Unrecognized file"};

bool HasStructureError(const std::string &output)
{
    for(const char *marker : kStructureErrors)
    {
        if(output.find(marker) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// What osslsigncode reports for a package: the two digests and any structure error.
void RequireOsslsigncodeAccepts(const std::string &path, const Bytes &blob, const std::string &fingerprint,
                                const seedtest::ScratchDir &scratch)
{
    const std::string out = scratch.File("extracted.sig");
    std::remove(out.c_str());
    const seedtest::ToolRun extract = seedtest::RunCommand("osslsigncode extract-signature -in " +
                                                           seedtest::ShellQuote(path) + " -out " + seedtest::ShellQuote(out));
    INFO("extract-signature:\n" << extract.output);
    CHECK(extract.status == 0);
    CHECK_FALSE(HasStructureError(extract.output));
    // The signature structure at the front of the stream is returned byte for byte.
    const Bytes got = sm::ReadAll(out);
    const std::size_t der = DerLength(blob);
    REQUIRE(der <= blob.size());
    CHECK(got == Bytes(blob.begin(), blob.begin() + static_cast<std::ptrdiff_t>(der)));

    const seedtest::ToolRun verify = seedtest::RunCommand("osslsigncode verify -in " + seedtest::ShellQuote(path));
    INFO("verify:\n" << verify.output);
    CHECK_FALSE(HasStructureError(verify.output));
    const std::string current = Field(verify.output, "Current DigitalSignature");
    const std::string calculated = Field(verify.output, "Calculated DigitalSignature");
    CHECK(current.size() == 64);
    CHECK(calculated.size() == 64);
    CHECK(current == calculated);
    CHECK(calculated == fingerprint);
}

} // namespace

TEST_CASE("the packages recorded as known answers are written and read back", "[MsiLiveTools][recorded]")
{
    // A signed sample's own signature is embedded into a package with the same
    // fingerprint; no program is needed. Writing the files out is for the recording script.
    struct Pair
    {
        const char *sample;
        const char *blob;
    };
    const Pair pairs[] = {{"tiny.msi", "tiny-osslsig-small.msi"},
                          {"tiny.msi", "tiny-osslsig-large.msi"},
                          {"tiny-v4.msi", "tiny-osslsig-small.msi"},
                          {"tiny-v4.msi", "tiny-osslsig-large.msi"},
                          {"nested.msi", "nested-osslsig.msi"},
                          {"nested-osslsig.msi", "nested-osslsig.msi"},
                          {"tiny-osslsig-dse.msi", "tiny-osslsig-small.msi"},
                          {"tiny-osslsig-large.msi", "tiny-osslsig-small.msi"},
                          {"two-neighbours.msi", "two-neighbours.msi"}};
    const char *write_to = std::getenv("SEED_MSI_WRITE_SIGNED");
    seedtest::ScratchDir scratch("msi-live-recorded");
    for(const Pair &pair : pairs)
    {
        INFO("sample " << pair.sample << " with the signature of " << pair.blob);
        const Bytes blob = msi::SampleSignature(pair.blob);
        const std::string path = msi::CopySample(scratch, pair.sample);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob, true));
        CHECK(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob));
        CHECK(MsiSigner::CheckSignature(path).state == MsiSigner::SignatureState::Matches);
        CHECK(msi::ToHexString(MsiSigner::ComputeAuthenticodeDigest(path).digest) == msi::RecordedFingerprint(pair.sample));
        if(write_to != nullptr && write_to[0] != '\0')
        {
            std::filesystem::copy_file(path, std::filesystem::path(write_to) / (std::string(pair.sample) + "--" + pair.blob + ".msi"),
                                       std::filesystem::copy_options::overwrite_existing);
        }
    }
}

TEST_CASE("osslsigncode reads the signature the library writes in every size class", "[MsiLiveTools][osslsigncode]")
{
    if(!seedtest::RequireTool("osslsigncode"))
    {
        return;
    }
    seedtest::ScratchDir scratch("msi-live-ossl");
    for(const std::string &sample : {std::string("tiny.msi"), std::string("tiny-v4.msi"), std::string("nested.msi"),
                                     std::string("tiny-osslsig-dse.msi"), std::string("two-neighbours.msi"),
                                     std::string("nested-osslsig.msi")})
    {
        for(const NamedBlob &blob : Blobs(sample))
        {
            INFO("sample " << sample << " signature of " << blob.label);
            const std::string path = msi::CopySample(scratch, sample);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob.bytes));
            REQUIRE(MsiSigner::ExtractSignature(path) == std::optional<Bytes>(blob.bytes));
            RequireOsslsigncodeAccepts(path, blob.bytes, msi::RecordedFingerprint(sample), scratch);
        }
    }
}

TEST_CASE("osslsigncode reads the signature after replacing and after strip then sign", "[MsiLiveTools][osslsigncode]")
{
    if(!seedtest::RequireTool("osslsigncode"))
    {
        return;
    }
    seedtest::ScratchDir scratch("msi-live-ossl-ops");
    const std::string sample = "tiny-osslsig-large.msi";
    const std::vector<NamedBlob> blobs = Blobs(sample);
    const std::string path = msi::CopySample(scratch, sample);
    // Replace across the cut-off in both directions, then strip and sign again.
    for(const std::size_t index : {0u, 5u, 1u, 3u, 0u})
    {
        INFO("signature of " << blobs[index].label);
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blobs[index].bytes));
        RequireOsslsigncodeAccepts(path, blobs[index].bytes, msi::RecordedFingerprint(sample), scratch);
    }
    REQUIRE(MsiSigner::StripSignature(path));
    {
        const seedtest::ToolRun verify = seedtest::RunCommand("osslsigncode verify -in " + seedtest::ShellQuote(path));
        INFO(verify.output);
        CHECK(verify.output.find("no signature") != std::string::npos);
    }
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blobs[0].bytes));
    RequireOsslsigncodeAccepts(path, blobs[0].bytes, msi::RecordedFingerprint(sample), scratch);
}

TEST_CASE("osslsigncode is checked on packages it wrote and on the one an earlier version wrote", "[MsiLiveTools][osslsigncode]")
{
    if(!seedtest::RequireTool("osslsigncode"))
    {
        return;
    }
    // Controls: the check passes for the package osslsigncode wrote itself and
    // reports a structure error for the legacy one, so it can tell them apart.
    seedtest::ScratchDir scratch("msi-live-ossl-control");
    for(const std::string &sample : {std::string("tiny-osslsig-small.msi"), std::string("tiny-osslsig-large.msi"),
                                     std::string("nested-osslsig.msi")})
    {
        INFO("sample " << sample);
        const std::string path = msi::CopySample(scratch, sample);
        RequireOsslsigncodeAccepts(path, msi::SampleSignature(sample), msi::RecordedFingerprint(sample), scratch);
    }
    {
        const std::string path = msi::CopySample(scratch, "legacy-the-seed-0.6.0.msi");
        const seedtest::ToolRun verify = seedtest::RunCommand("osslsigncode verify -in " + seedtest::ShellQuote(path));
        INFO(verify.output);
        CHECK(HasStructureError(verify.output));
        // After the library replaced the signature the same program reads it.
        const Bytes blob = msi::SampleSignature("tiny-osslsig-small.msi");
        REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
        RequireOsslsigncodeAccepts(path, blob, msi::RecordedFingerprint("legacy-the-seed-0.6.0.msi"), scratch);
    }
}

namespace {

// Runs msi-open.exe under Wine on a package and the bytes it should find.
seedtest::ToolRun RunProbe(const seedtest::ScratchDir &scratch, const std::string &package, const Bytes &expected)
{
    const std::string expected_path = sm::WriteScratch(scratch, "expected.bin", expected);
    // One Wine configuration folder for the whole program: making it takes several seconds.
    static const seedtest::ScratchDir prefix_folder("msi-live-wineprefix");
    const std::string prefix = prefix_folder.File("prefix");
    return seedtest::RunCommand("timeout 180 env WINEPREFIX=" + seedtest::ShellQuote(prefix) +
                                " WINEDEBUG=-all wine " + seedtest::ShellQuote(seedtest::FixturePath("msi-open.exe")) + " " +
                                seedtest::ShellQuote(package) + " " + seedtest::ShellQuote(expected_path));
}

} // namespace

TEST_CASE("Wine finds the signature stream by name in packages the library writes", "[MsiLiveTools][wine]")
{
    if(!seedtest::RequireTool("wine"))
    {
        return;
    }
    seedtest::ScratchDir scratch("msi-live-wine");

    struct Pick
    {
        std::string sample;
        std::vector<std::size_t> blob_indexes;
    };
    // Every size class on the plain sample, then the other package shapes
    // (version 4, nested storages, neighbours on both sides, a package another
    // tool signed with a large signature and one with an extended stream).
    const std::vector<Pick> picks = {{"tiny.msi", {0, 2, 3, 5}},
                                     {"tiny-v4.msi", {2}},
                                     {"nested.msi", {4}},
                                     {"two-neighbours.msi", {0, 5}},
                                     {"tiny-osslsig-large.msi", {0}},
                                     {"tiny-osslsig-dse.msi", {3}}};
    for(const Pick &pick : picks)
    {
        for(const std::size_t index : pick.blob_indexes)
        {
            const NamedBlob blob = Blobs(pick.sample)[index];
            INFO("sample " << pick.sample << " signature of " << blob.label);
            const std::string path = msi::CopySample(scratch, pick.sample);
            REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob.bytes));
            const seedtest::ToolRun run = RunProbe(scratch, path, blob.bytes);
            INFO("msi-open.exe:\n" << run.output);
            CHECK(run.status == 0);
            CHECK(run.output.find("OpenStream hr=0x00000000") != std::string::npos);
            CHECK(run.output.find("equal") != std::string::npos);
            CHECK(run.output.find("0x80030002") == std::string::npos);
        }
    }
}

TEST_CASE("Wine finds the signature stream after strip then sign and in a package another tool signed", "[MsiLiveTools][wine]")
{
    if(!seedtest::RequireTool("wine"))
    {
        return;
    }
    seedtest::ScratchDir scratch("msi-live-wine-ops");
    const std::vector<NamedBlob> blobs = Blobs("two-neighbours.msi");
    const std::string path = msi::CopySample(scratch, "two-neighbours.msi");
    // The package as osslsigncode made it (control: the probe finds that signature).
    {
        const seedtest::ToolRun run = RunProbe(scratch, path, msi::SampleSignature("two-neighbours.msi"));
        INFO(run.output);
        CHECK(run.status == 0);
    }
    REQUIRE(MsiSigner::StripSignature(path));
    {
        // Nothing to find after strip: the probe reports the stream missing.
        const seedtest::ToolRun run = RunProbe(scratch, path, blobs[0].bytes);
        INFO(run.output);
        CHECK(run.status == 2);
    }
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blobs[2].bytes));
    {
        const seedtest::ToolRun run = RunProbe(scratch, path, blobs[2].bytes);
        INFO(run.output);
        CHECK(run.status == 0);
    }
}

TEST_CASE("Wine does not find the signature of a package an earlier version signed until it is signed again", "[MsiLiveTools][wine]")
{
    // The control for the probe: it reports the stream as missing (the review's
    // STG_E_FILENOTFOUND) for the legacy sample and finds it after the library
    // replaced the signature.
    if(!seedtest::RequireTool("wine"))
    {
        return;
    }
    seedtest::ScratchDir scratch("msi-live-wine-legacy");
    const std::string path = msi::CopySample(scratch, "legacy-the-seed-0.6.0.msi");
    {
        const seedtest::ToolRun run = RunProbe(scratch, path, Bytes(100, 1));
        INFO(run.output);
        CHECK(run.status == 2);
        CHECK(run.output.find("0x80030002") != std::string::npos);
    }
    const Bytes blob = cfb::PatternBytes(1426, 4);
    REQUIRE_NOTHROW(MsiSigner::EmbedSignature(path, blob));
    {
        const seedtest::ToolRun run = RunProbe(scratch, path, blob);
        INFO(run.output);
        CHECK(run.status == 0);
        CHECK(run.output.find("equal") != std::string::npos);
    }
}
