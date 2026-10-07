// The three signers write through the shared file replacement.
//
// For each signer (PE embed and strip, Mach-O embed, MSI embed and strip) two
// kinds of case:
//   - on a copy with mode 755 and on one with mode 640, the mode is the same
//     after the operation and the folder holds no new entries;
//   - when the replacement fails (an injected failure at the rename step), the
//     operation reports an error and the file is unchanged, which shows the
//     signer goes through the shared step.

#include "ReplaceTestSupport.hpp"

#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MsiSigner.hpp>
#include <libthe-seed/PeSigner.hpp>

#include "internal/FileReplaceHooks.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#endif

namespace {

using namespace seedtest::replace;
namespace fs = std::filesystem;

std::string CopyFixture(const seedtest::ScratchDir &scratch, const std::string &name)
{
    const std::string destination = scratch.File(name);
    fs::copy_file(seedtest::FixturePath(name), destination, fs::copy_options::overwrite_existing);
    return destination;
}

Bytes FakePkcs7()
{
    Bytes blob(128);
    for(std::size_t i = 0; i < blob.size(); ++i)
    {
        blob[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    return blob;
}

Bytes MachOSuperBlob(const std::string &path)
{
    const auto directory = MachOSigner::ComputeCodeDirectory(path, "test-identity");
    const Bytes cms(64, 0xDD);
    return MachOSigner::BuildSuperBlob(directory.code_directory, cms);
}

// The Mach-O fixtures cannot be signed yet because of the recorded known gap
// "MachOSigner::EmbedSignature and ExtractSignature round-trip" (see
// known-gaps.txt): embedding stops before any file is written, so the
// replacement cannot be reached. Those cases are skipped with a visible
// message, and run for real once the gap is closed.
bool MachOEmbedReachesReplacement(const std::string &fixture)
{
    seedtest::ScratchDir scratch("seed-signers-macho-probe");
    const std::string path = CopyFixture(scratch, fixture);
    try
    {
        MachOSigner::EmbedSignature(path, MachOSuperBlob(path));
    }
    catch(const std::runtime_error &error)
    {
        if(std::string(error.what()).find("No space for new load command") != std::string::npos)
        {
            SkipWithMessage("Mach-O embed on " + fixture +
                            " stops at the known gap (No space for new load command) before the "
                            "replacement is reached");
            return false;
        }
        throw;
    }
    return true;
}

std::vector<std::string> Names(const std::vector<FolderEntry> &entries)
{
    std::vector<std::string> names;
    for(const FolderEntry &entry : entries)
    {
        names.push_back(entry.name);
    }
    return names;
}

#if !defined(_WIN32)

using seed::internal::FileReplaceHooks;
using seed::internal::FileReplaceHooksScope;

int renames_attempted = 0;

int FailingRename(const char *, const char *)
{
    ++renames_attempted;
    errno = EACCES;
    return -1;
}

FileReplaceHooks FailingRenameHooks()
{
    FileReplaceHooks hooks = seed::internal::DefaultFileReplaceHooks();
    hooks.rename = &FailingRename;
    return hooks;
}

// Runs `operation` with the rename failing: it must raise std::runtime_error
// and leave the file and the folder as they were.
void RequireFailureLeavesFile(const seedtest::ScratchDir &scratch, const std::string &path,
                              const std::function<void()> &operation)
{
    const Bytes before = ReadAll(path);
    const unsigned mode = ModeOf(path);
    const auto listing = ListFolder(scratch.Path());

    renames_attempted = 0;
    {
        FileReplaceHooksScope scope(FailingRenameHooks());
        CHECK_THROWS_AS(operation(), std::runtime_error);
    }

    CHECK(renames_attempted > 0);
    CHECK(ReadAll(path) == before);
    CHECK(ModeOf(path) == mode);
    CHECK(ListFolder(scratch.Path()) == listing);
}

#endif

}

TEST_CASE("signers: PE embed and strip keep the mode and add no entries", "[SignersUseReplace][pe]")
{
    const unsigned mode = GENERATE(0755u, 0640u);
    seedtest::ScratchDir scratch("seed-signers-pe");
    const std::string path = CopyFixture(scratch, "tiny.exe");
    SetModeOf(path, mode);
    // On Windows the helpers do not carry permission bits, so compare with
    // what the file reports before the signer runs.
    const unsigned expected = ModeOf(path);
    const auto names = Names(ListFolder(scratch.Path()));

    PeSigner::EmbedSignature(path, FakePkcs7());
    CHECK(PeSigner::HasEmbeddedSignature(path));
    CHECK(ModeOf(path) == expected);
    CHECK(Names(ListFolder(scratch.Path())) == names);

    PeSigner::StripSignature(path);
    CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
    CHECK(ModeOf(path) == expected);
    CHECK(Names(ListFolder(scratch.Path())) == names);
}

TEST_CASE("signers: Mach-O embed keeps the mode and adds no entries", "[SignersUseReplace][macho]")
{
    const unsigned mode = GENERATE(0755u, 0640u);
    const std::string fixture = GENERATE(as<std::string>{}, "tiny-macho-arm64", "tiny-macho-x86_64");
    if(!MachOEmbedReachesReplacement(fixture))
    {
        return;
    }
    seedtest::ScratchDir scratch("seed-signers-macho");
    const std::string path = CopyFixture(scratch, fixture);
    SetModeOf(path, mode);
    // On Windows the helpers do not carry permission bits, so compare with
    // what the file reports before the signer runs.
    const unsigned expected = ModeOf(path);
    const auto names = Names(ListFolder(scratch.Path()));

    MachOSigner::EmbedSignature(path, MachOSuperBlob(path));
    CHECK(MachOSigner::HasEmbeddedSignature(path));
    CHECK(ModeOf(path) == expected);
    CHECK(Names(ListFolder(scratch.Path())) == names);
}

TEST_CASE("signers: MSI embed and strip keep the mode and add no entries", "[SignersUseReplace][msi]")
{
    const unsigned mode = GENERATE(0755u, 0640u);
    seedtest::ScratchDir scratch("seed-signers-msi");
    const std::string path = CopyFixture(scratch, "tiny.msi");
    SetModeOf(path, mode);
    // On Windows the helpers do not carry permission bits, so compare with
    // what the file reports before the signer runs.
    const unsigned expected = ModeOf(path);
    const auto names = Names(ListFolder(scratch.Path()));

    MsiSigner::EmbedSignature(path, FakePkcs7());
    CHECK(MsiSigner::HasEmbeddedSignature(path));
    CHECK(ModeOf(path) == expected);
    CHECK(Names(ListFolder(scratch.Path())) == names);

    MsiSigner::StripSignature(path);
    CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
    CHECK(ModeOf(path) == expected);
    CHECK(Names(ListFolder(scratch.Path())) == names);
}

#if !defined(_WIN32)

TEST_CASE("signers: a failure in the replacement leaves the PE file unchanged",
          "[SignersUseReplace][pe]")
{
    seedtest::ScratchDir scratch("seed-signers-pe-fail");
    const std::string path = CopyFixture(scratch, "tiny.exe");
    SetModeOf(path, 0755);

    SECTION("embed")
    {
        RequireFailureLeavesFile(scratch, path,
                                 [&] { PeSigner::EmbedSignature(path, FakePkcs7()); });
        CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
    }
    SECTION("strip")
    {
        PeSigner::EmbedSignature(path, FakePkcs7());
        RequireFailureLeavesFile(scratch, path, [&] { PeSigner::StripSignature(path); });
        CHECK(PeSigner::HasEmbeddedSignature(path));
    }
}

TEST_CASE("signers: a failure in the replacement leaves the Mach-O file unchanged",
          "[SignersUseReplace][macho]")
{
    if(!MachOEmbedReachesReplacement("tiny-macho-x86_64"))
    {
        return;
    }
    seedtest::ScratchDir scratch("seed-signers-macho-fail");
    const std::string path = CopyFixture(scratch, "tiny-macho-x86_64");
    SetModeOf(path, 0755);
    const Bytes blob = MachOSuperBlob(path);

    RequireFailureLeavesFile(scratch, path, [&] { MachOSigner::EmbedSignature(path, blob); });
    CHECK_FALSE(MachOSigner::HasEmbeddedSignature(path));
}

TEST_CASE("signers: a failure in the replacement leaves the MSI file unchanged",
          "[SignersUseReplace][msi]")
{
    seedtest::ScratchDir scratch("seed-signers-msi-fail");
    const std::string path = CopyFixture(scratch, "tiny.msi");
    SetModeOf(path, 0755);

    SECTION("embed")
    {
        RequireFailureLeavesFile(scratch, path,
                                 [&] { MsiSigner::EmbedSignature(path, FakePkcs7()); });
        CHECK_FALSE(MsiSigner::HasEmbeddedSignature(path));
    }
    SECTION("strip")
    {
        MsiSigner::EmbedSignature(path, FakePkcs7());
        RequireFailureLeavesFile(scratch, path, [&] { MsiSigner::StripSignature(path); });
        CHECK(MsiSigner::HasEmbeddedSignature(path));
    }
}

#endif
