// Every prefix of every sample, of a BuildSuperBlob output and of two small
// in-code ELF images, through every operation that applies to the format.
//
// A prefix may still be acceptable to an operation that does not need the
// missing part; then the answer must be exactly the answer for the whole
// file. Otherwise the operation must be rejected with std::runtime_error whose
// message starts with the format name, within the time and heap limits. A
// rejected embed or strip must leave the file as it was. Operations whose
// answer depends on every byte of the file (the code directory hashes) must
// only be rejected cleanly or finish, because a shorter file has a different
// answer by definition.
//
// Trying every length of every sample takes minutes under the sanitizer, so by
// default the lengths are every length up to 4,096, every 61st length after
// that, and the last 64 lengths. Setting the environment variable
// SEED_EXHAUSTIVE=1 runs every length.

#include "MalformedInput.hpp"

#include "fuzz/FuzzTargets.hpp"

#include <libthe-seed/DependencyLister.hpp>
#include <libthe-seed/MachOParser.hpp>
#include <libthe-seed/MachOSigner.hpp>
#include <libthe-seed/MsiSigner.hpp>
#include <libthe-seed/PeSigner.hpp>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;
using seedtest::malformed::LoadSample;
using seedtest::malformed::Truncate;
using seedtest::malformed::WriteScratch;

bool Exhaustive()
{
    const char *value = std::getenv("SEED_EXHAUSTIVE");
    return value != nullptr && std::string(value) == "1";
}

std::vector<std::size_t> Lengths(std::size_t size)
{
    std::vector<std::size_t> lengths;
    if(Exhaustive())
    {
        for(std::size_t length = 0; length <= size; ++length)
        {
            lengths.push_back(length);
        }
        return lengths;
    }
    for(std::size_t length = 0; length <= size; ++length)
    {
        if(length <= 4096 || length % 61 == 0 || length + 64 > size)
        {
            lengths.push_back(length);
        }
    }
    return lengths;
}

// Operation results are compared as byte strings.
using Result = Bytes;

void Append(Result &result, const std::string &text)
{
    result.insert(result.end(), text.begin(), text.end());
    result.push_back(0);
}

void Append(Result &result, const Bytes &bytes)
{
    result.insert(result.end(), bytes.begin(), bytes.end());
    result.push_back(1);
}

Result OptionalBytes(const std::optional<Bytes> &value)
{
    Result result;
    result.push_back(value.has_value() ? 1 : 0);
    if(value.has_value())
    {
        Append(result, *value);
    }
    return result;
}

// A lister answer that carries an error is a rejection. A file too short to
// have a recognisable format is turned away by the lister itself, before any
// parser runs, with a message that names no format; it is given the format of
// the sample so the rejection can be checked like the others.
Result ListerResult(const std::string &path, const std::string &format)
{
    DependencyLister lister;
    const DependencyResult answer = lister.ListDependencies({path}, {});
    if(!answer.errors.empty())
    {
        const std::string &message = answer.errors.begin()->second;
        if(message == "File is too small to determine binary format" ||
           message == "Unsupported binary format")
        {
            throw std::runtime_error(format + ": " + message);
        }
        throw std::runtime_error(message);
    }
    Result result;
    for(const auto &entry : answer.dependencies)
    {
        Append(result, entry.first);
        for(const auto &name : entry.second)
        {
            Append(result, name);
        }
    }
    return result;
}

struct ReadOperation
{
    std::string name;
    std::string format;
    std::function<Result(const std::string &)> run;
    bool same_as_full; // false: only a clean rejection or a normal finish is required
};

struct ChangeOperation
{
    std::string name;
    std::string format;
    std::function<void(const std::string &)> run;
};

void Exercise(const std::string &name, const std::string &format, std::size_t length,
              std::size_t input_size, const std::function<void()> &call)
{
    INFO(name << " on a prefix of " << length << " bytes");
    const auto outcome = seedtest::malformed::detail::Run(call);
    if(outcome.threw)
    {
        seedtest::malformed::detail::CheckRejection(outcome, format, "");
    }
    seedtest::malformed::detail::CheckLimits(outcome, input_size, 4096);
}

void SweepFile(const Bytes &full, const std::vector<ReadOperation> &reads,
               const std::vector<ChangeOperation> &changes)
{
    seedtest::ScratchDir scratch("seed-truncated");
    const std::string path = scratch.File("sample.bin");
    const std::string work = scratch.File("work.bin");

    WriteScratch(scratch, "sample.bin", full);
    // The answer for the whole file, when the file as a whole is accepted. The
    // genuine Mach-O samples are misread today (the byte-order misreading described in test_MalformedMachO.cpp),
    // so an operation can reject a whole sample; then its prefixes are only
    // held to a clean rejection or a normal finish.
    std::vector<std::optional<Result>> whole;
    for(const auto &operation : reads)
    {
        try
        {
            whole.push_back(operation.run(path));
        }
        catch(const std::runtime_error &)
        {
            whole.push_back(std::nullopt);
        }
    }

    for(const std::size_t length : Lengths(full.size()))
    {
        const Bytes prefix = Truncate(full, length);
        WriteScratch(scratch, "sample.bin", prefix);

        for(std::size_t i = 0; i < reads.size(); ++i)
        {
            const ReadOperation &operation = reads[i];
            INFO(operation.name << " on a prefix of " << length << " bytes");
            if(operation.same_as_full && whole[i].has_value())
            {
                seedtest::malformed::RequireRejectedOrSameAsBefore(
                    [&] { return operation.run(path); }, operation.format, *whole[i], length);
            }
            else
            {
                Exercise(operation.name, operation.format, length, length,
                         [&] { (void)operation.run(path); });
            }
        }

        for(const auto &operation : changes)
        {
            WriteScratch(scratch, "work.bin", prefix);
            const auto outcome = seedtest::malformed::detail::Run([&] { operation.run(work); });
            INFO(operation.name << " on a prefix of " << length << " bytes");
            if(outcome.threw)
            {
                seedtest::malformed::detail::CheckRejection(outcome, operation.format, "");
                seedtest::malformed::RequireUnchanged(work, prefix);
            }
            seedtest::malformed::detail::CheckLimits(outcome, length, 4096);
        }
    }
}

// ---------------------------------------------------------------------------
// Operations per format
// ---------------------------------------------------------------------------

std::vector<ReadOperation> ElfReads()
{
    return {{"DependencyLister (ELF)", "ELF",
             [](const std::string &path) { return ListerResult(path, "ELF"); }, true}};
}

std::vector<ReadOperation> PeReads()
{
    return {
        {"DependencyLister (PE)", "PE",
         [](const std::string &path) { return ListerResult(path, "PE"); }, true},
        {"PeSigner::ComputeAuthenticodeDigest", "PE",
         [](const std::string &path) {
             const auto digest = PeSigner::ComputeAuthenticodeDigest(path);
             Result result = digest.digest;
             result.push_back(digest.is_pe32_plus ? 1 : 0);
             return result;
         },
         false},
        {"PeSigner::HasEmbeddedSignature", "PE",
         [](const std::string &path) {
             return Result{static_cast<std::uint8_t>(PeSigner::HasEmbeddedSignature(path))};
         },
         true},
        {"PeSigner::ExtractSignature", "PE",
         [](const std::string &path) { return OptionalBytes(PeSigner::ExtractSignature(path)); },
         true},
    };
}

std::vector<ChangeOperation> PeChanges()
{
    return {
        {"PeSigner::EmbedSignature", "PE",
         [](const std::string &path) { PeSigner::EmbedSignature(path, Bytes(64, 0x5A)); }},
        {"PeSigner::StripSignature", "PE",
         [](const std::string &path) { PeSigner::StripSignature(path); }},
    };
}

std::vector<ReadOperation> MachOReads()
{
    return {
        {"MachOParser::DetectFormat", "Mach-O",
         [](const std::string &path) {
             return Result{static_cast<std::uint8_t>(MachOParser::DetectFormat(path))};
         },
         false},
        {"MachOParser::IsMachO", "Mach-O",
         [](const std::string &path) { return Result{static_cast<std::uint8_t>(MachOParser::IsMachO(path))}; },
         false},
        {"MachOParser::IsFatBinary", "Mach-O",
         [](const std::string &path) {
             return Result{static_cast<std::uint8_t>(MachOParser::IsFatBinary(path))};
         },
         false},
        {"MachOParser::GetArchSlices", "Mach-O",
         [](const std::string &path) {
             Result result;
             for(const auto &slice : MachOParser::GetArchSlices(path))
             {
                 Append(result, std::to_string(slice.cpu_type) + "/" +
                                    std::to_string(slice.cpu_subtype) + "/" +
                                    std::to_string(slice.offset) + "/" +
                                    std::to_string(slice.size));
             }
             return result;
         },
         false},
        {"MachOParser::ListDependencies", "Mach-O",
         [](const std::string &path) {
             Result result;
             for(const auto &name : MachOParser::ListDependencies(path))
             {
                 Append(result, name);
             }
             return result;
         },
         true},
        {"MachOSigner::ComputeCodeDirectory", "Mach-O",
         [](const std::string &path) {
             const auto answer = MachOSigner::ComputeCodeDirectory(path, "sweep");
             Result result = answer.code_directory;
             result.insert(result.end(), answer.cd_hash.begin(), answer.cd_hash.end());
             return result;
         },
         false},
        {"MachOSigner::HasEmbeddedSignature", "Mach-O",
         [](const std::string &path) {
             return Result{static_cast<std::uint8_t>(MachOSigner::HasEmbeddedSignature(path))};
         },
         false},
        {"MachOSigner::ExtractSignature", "Mach-O",
         [](const std::string &path) { return OptionalBytes(MachOSigner::ExtractSignature(path)); },
         false},
    };
}

std::vector<ChangeOperation> MachOChanges()
{
    return {
        {"MachOSigner::EmbedSignature", "Mach-O",
         [](const std::string &path) { MachOSigner::EmbedSignature(path, seedfuzz::SuperBlobSeed()); }},
    };
}

std::vector<ReadOperation> MsiReads()
{
    return {
        {"MsiSigner::IsMsi", "MSI",
         [](const std::string &path) { return Result{static_cast<std::uint8_t>(MsiSigner::IsMsi(path))}; },
         false},
        {"MsiSigner::ComputeAuthenticodeDigest", "MSI",
         [](const std::string &path) { return MsiSigner::ComputeAuthenticodeDigest(path).digest; },
         false},
        {"MsiSigner::HasEmbeddedSignature", "MSI",
         [](const std::string &path) {
             return Result{static_cast<std::uint8_t>(MsiSigner::HasEmbeddedSignature(path))};
         },
         false},
        {"MsiSigner::ExtractSignature", "MSI",
         [](const std::string &path) { return OptionalBytes(MsiSigner::ExtractSignature(path)); },
         false},
    };
}

std::vector<ChangeOperation> MsiChanges()
{
    return {
        {"MsiSigner::EmbedSignature", "MSI",
         [](const std::string &path) { MsiSigner::EmbedSignature(path, Bytes(1500, 0x5A)); }},
        {"MsiSigner::StripSignature", "MSI",
         [](const std::string &path) { MsiSigner::StripSignature(path); }},
    };
}

} // namespace

TEST_CASE("truncated: ELF64 little-endian image", "[TruncatedSamples]")
{
    SweepFile(seedfuzz::ElfSeed(true, true), ElfReads(), {});
}

TEST_CASE("truncated: ELF32 big-endian image", "[TruncatedSamples]")
{
    SweepFile(seedfuzz::ElfSeed(false, false), ElfReads(), {});
}

TEST_CASE("truncated: tiny.exe", "[TruncatedSamples]")
{
    SweepFile(LoadSample("tiny.exe"), PeReads(), PeChanges());
}

TEST_CASE("truncated: test.dll", "[TruncatedSamples]")
{
    SweepFile(LoadSample("test.dll"), PeReads(), PeChanges());
}

TEST_CASE("truncated: tiny-macho-x86_64", "[TruncatedSamples]")
{
    SweepFile(LoadSample("tiny-macho-x86_64"), MachOReads(), MachOChanges());
}

TEST_CASE("truncated: tiny-macho-arm64", "[TruncatedSamples]")
{
    SweepFile(LoadSample("tiny-macho-arm64"), MachOReads(), MachOChanges());
}

TEST_CASE("truncated: tiny-macho-universal", "[TruncatedSamples]")
{
    SweepFile(LoadSample("tiny-macho-universal"), MachOReads(), MachOChanges());
}

TEST_CASE("truncated: tiny.msi", "[TruncatedSamples]")
{
    SweepFile(LoadSample("tiny.msi"), MsiReads(), MsiChanges());
}

TEST_CASE("truncated: BuildSuperBlob output", "[TruncatedSamples]")
{
    const Bytes blob = seedfuzz::SuperBlobSeed();
    const std::string format = "Mach-O code signature";
    const Result whole_cms = OptionalBytes(MachOSigner::ExtractCmsFromSuperBlob(blob));
    const Result whole_cd = OptionalBytes(MachOSigner::ExtractCodeDirectoryFromSuperBlob(blob));

    for(const std::size_t length : Lengths(blob.size()))
    {
        const Bytes prefix = Truncate(blob, length);
        INFO("prefix of " << length << " bytes");
        seedtest::malformed::RequireRejectedOrSameAsBefore(
            [&] { return OptionalBytes(MachOSigner::ExtractCmsFromSuperBlob(prefix)); }, format,
            whole_cms, length);
        seedtest::malformed::RequireRejectedOrSameAsBefore(
            [&] { return OptionalBytes(MachOSigner::ExtractCodeDirectoryFromSuperBlob(prefix)); },
            format, whole_cd, length);
    }
}
