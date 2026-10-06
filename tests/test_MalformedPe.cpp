// Malformed and edge-case Windows program and library files, through
// DependencyLister and every PeSigner operation.
//
// Test case names start with their origin: "ok:" for an unmodified or
// legal-but-unusual input with today's result, "review:", "f14:" and "f18:"
// for the review's inputs, and "edge:" for an edge-case family.

#include "MalformedInput.hpp"

#include <libthe-seed/DependencyLister.hpp>
#include <libthe-seed/PeSigner.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if __has_include("internal/PeCertificate.hpp")
#include "internal/PeCertificate.hpp"
#define SEED_HAVE_PE_SIZE_CHECK 1
#endif

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;

// Layout of the unmodified samples (both are PE32+ images).
constexpr std::uint64_t kPeHeader = 128;                  // value of e_lfanew
constexpr std::uint64_t kNumberOfSectionsField = kPeHeader + 6;
constexpr std::uint64_t kOptionalHeader = kPeHeader + 24;
constexpr std::uint64_t kSectionTable = kOptionalHeader + 240;
constexpr std::uint64_t kChecksumField = kOptionalHeader + 64;
constexpr std::uint64_t kImportDirectoryField = kOptionalHeader + 120;
constexpr std::uint64_t kCertificateDirectoryField = kOptionalHeader + 144;

// tiny.exe: .idata raw data, the two import descriptors and the first DLL name.
constexpr std::uint64_t kTinyImportTable = 11264;
constexpr std::uint64_t kTinyFirstName = 12460;
constexpr std::uint64_t kTinyFileSize = 13824;
constexpr std::uint64_t kTinyIdataSection = 6;

const std::vector<std::uint8_t> kFakeSignature = [] {
    std::vector<std::uint8_t> signature(128);
    for(std::size_t i = 0; i < signature.size(); ++i)
    {
        signature[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    return signature;
}();

// Digests of the samples with the checksum field and certificate directory
// entry excluded, as recorded from the unfixed library.
const char *const kTinyDigest = "947e0a67a3a548c7bade62813f78c32740bda430684e59941f86dd1caa0066cb";
const char *const kTinyPlus3Digest =
    "e99d1f620053fe88324fb52c2b9ac5aeff10e053b4bc5d9e26938cacdec66ddf";
const char *const kTestDllDigest =
    "dc6cedf1a1df1ac30d44d44f71142132b6bfcbc324e360428c78228ccce66d05";

std::string ToHex(const std::vector<std::uint8_t> &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for(const auto byte : bytes)
    {
        text += digits[byte >> 4];
        text += digits[byte & 0xF];
    }
    return text;
}

// DependencyLister reports a malformed input in errors, never as an empty
// list. This wraps that report as an exception so RequireRejected can judge it.
void ListOrThrow(const std::string &path)
{
    DependencyLister lister;
    const auto result = lister.ListDependencies({path}, {});
    const auto error = result.errors.find(path);
    if(error != result.errors.end())
    {
        CHECK(result.dependencies.empty());
        throw std::runtime_error(error->second);
    }
}

struct Operation
{
    std::string name;
    std::function<void(const std::string &)> run;
};

Operation ListOp()
{
    return {"DependencyLister::ListDependencies", [](const std::string &path) { ListOrThrow(path); }};
}

std::vector<Operation> SignerOps()
{
    return {
        {"ComputeAuthenticodeDigest",
         [](const std::string &path) { (void)PeSigner::ComputeAuthenticodeDigest(path); }},
        {"ExtractSignature", [](const std::string &path) { (void)PeSigner::ExtractSignature(path); }},
        {"HasEmbeddedSignature",
         [](const std::string &path) { (void)PeSigner::HasEmbeddedSignature(path); }},
        {"StripSignature", [](const std::string &path) { PeSigner::StripSignature(path); }},
        {"EmbedSignature",
         [](const std::string &path) { PeSigner::EmbedSignature(path, kFakeSignature); }},
    };
}

std::vector<Operation> AllOps()
{
    auto ops = SignerOps();
    ops.insert(ops.begin(), ListOp());
    return ops;
}

bool Modifies(const Operation &op)
{
    return op.name == "StripSignature" || op.name == "EmbedSignature";
}

// Each operation runs on a fresh copy of the input. It must be rejected with
// "PE: <keyword>" within the time and heap limits, and a rejected embed or
// strip must leave the file unchanged.
void RequireAllRejected(const Bytes &input, const std::vector<Operation> &ops,
                        const std::string &keyword)
{
    seedtest::ScratchDir scratch("malformed-pe");
    for(const auto &op : ops)
    {
        DYNAMIC_SECTION(op.name)
        {
            const std::string path = seedtest::malformed::WriteScratch(scratch, "input.bin", input);
            seedtest::malformed::RequireRejected([&] { op.run(path); }, "PE", keyword, input.size(),
                                                 kFakeSignature.size());
            if(Modifies(op))
            {
                seedtest::malformed::RequireUnchanged(path, input);
            }
        }
    }
}

Bytes Sample(const std::string &name)
{
    return seedtest::malformed::LoadSample(name);
}

// A copy of tiny.exe that the library signed with the fake signature.
Bytes SignedTiny()
{
    seedtest::ScratchDir scratch("malformed-pe-signed");
    const std::string path = seedtest::malformed::WriteScratch(scratch, "signed.exe", Sample("tiny.exe"));
    PeSigner::EmbedSignature(path, kFakeSignature);
    return seedtest::malformed::ReadAll(path);
}

std::uint32_t CertificateOffset(const Bytes &bytes)
{
    return seedtest::malformed::detail::GetLE<std::uint32_t>(bytes, kCertificateDirectoryField);
}

std::uint32_t CertificateSize(const Bytes &bytes)
{
    return seedtest::malformed::detail::GetLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4);
}

std::uint64_t SectionField(std::uint64_t index, std::uint64_t field_offset)
{
    return kSectionTable + 40 * index + field_offset;
}

} // namespace

// ---------------------------------------------------------------------------
// Well-formed inputs
// ---------------------------------------------------------------------------

TEST_CASE("ok: DependencyLister lists the imports of the PE samples", "[MalformedPe][ok]")
{
    for(const std::string name : {"tiny.exe", "test.dll"})
    {
        DYNAMIC_SECTION(name)
        {
            const std::string path = seedtest::FixturePath(name);
            DependencyLister lister;
            const auto result = lister.ListDependencies({path}, {});
            CHECK(result.errors.empty());
            REQUIRE(result.dependencies.size() == 2);
            REQUIRE(result.dependencies.count("KERNEL32.dll") == 1);
            REQUIRE(result.dependencies.count("msvcrt.dll") == 1);
            CHECK(result.dependencies.at("KERNEL32.dll") == std::vector<std::string>{path});
            CHECK(result.dependencies.at("msvcrt.dll") == std::vector<std::string>{path});
        }
    }
}

TEST_CASE("ok: PeSigner digest of the PE samples", "[MalformedPe][ok]")
{
    const auto tiny = PeSigner::ComputeAuthenticodeDigest(seedtest::FixturePath("tiny.exe"));
    CHECK(tiny.is_pe32_plus);
    CHECK(ToHex(tiny.digest) == kTinyDigest);

    const auto dll = PeSigner::ComputeAuthenticodeDigest(seedtest::FixturePath("test.dll"));
    CHECK(dll.is_pe32_plus);
    CHECK(ToHex(dll.digest) == kTestDllDigest);
}

TEST_CASE("ok: PeSigner presence, extraction and stripping on unsigned samples", "[MalformedPe][ok]")
{
    seedtest::ScratchDir scratch("malformed-pe-ok");
    for(const std::string name : {"tiny.exe", "test.dll"})
    {
        DYNAMIC_SECTION(name)
        {
            const Bytes original = Sample(name);
            const std::string path = seedtest::malformed::WriteScratch(scratch, name, original);
            CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
            CHECK_FALSE(PeSigner::ExtractSignature(path).has_value());
            PeSigner::StripSignature(path);
            seedtest::malformed::RequireUnchanged(path, original);
        }
    }
}

TEST_CASE("ok: PeSigner embed, extract and strip round trip", "[MalformedPe][ok]")
{
    seedtest::ScratchDir scratch("malformed-pe-ok");
    for(const std::string name : {"tiny.exe", "test.dll"})
    {
        DYNAMIC_SECTION(name)
        {
            const Bytes original = Sample(name);
            const std::string path = seedtest::malformed::WriteScratch(scratch, name, original);
            const auto unsigned_digest = PeSigner::ComputeAuthenticodeDigest(path).digest;

            PeSigner::EmbedSignature(path, kFakeSignature);
            CHECK(PeSigner::HasEmbeddedSignature(path));
            const auto extracted = PeSigner::ExtractSignature(path);
            REQUIRE(extracted.has_value());
            CHECK(*extracted == kFakeSignature);

            // The certificate table is 8-byte aligned and holds the 8-byte
            // header and the signature; the digest ignores it.
            const Bytes signed_bytes = seedtest::malformed::ReadAll(path);
            const std::uint64_t table = (original.size() + 7) / 8 * 8;
            CHECK(CertificateOffset(signed_bytes) == table);
            CHECK(CertificateSize(signed_bytes) == 136);
            CHECK(signed_bytes.size() == table + 136);
            CHECK(PeSigner::ComputeAuthenticodeDigest(path).digest == unsigned_digest);

            PeSigner::StripSignature(path);
            CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
            CHECK_FALSE(PeSigner::ExtractSignature(path).has_value());
            CHECK(seedtest::malformed::ReadAll(path).size() == table);
            CHECK(PeSigner::ComputeAuthenticodeDigest(path).digest == unsigned_digest);
        }
    }
}

TEST_CASE("ok: PE with 3 bytes appended", "[MalformedPe][ok]")
{
    seedtest::ScratchDir scratch("malformed-pe-ok");
    Bytes bytes = Sample("tiny.exe");
    bytes.insert(bytes.end(), {1, 2, 3});
    const std::string path = seedtest::malformed::WriteScratch(scratch, "plus3.exe", bytes);

    DependencyLister lister;
    const auto result = lister.ListDependencies({path}, {});
    CHECK(result.errors.empty());
    CHECK(result.dependencies.size() == 2);

    CHECK(ToHex(PeSigner::ComputeAuthenticodeDigest(path).digest) == kTinyPlus3Digest);
    CHECK_FALSE(PeSigner::HasEmbeddedSignature(path));
    CHECK_FALSE(PeSigner::ExtractSignature(path).has_value());

    PeSigner::EmbedSignature(path, kFakeSignature);
    const auto extracted = PeSigner::ExtractSignature(path);
    REQUIRE(extracted.has_value());
    CHECK(*extracted == kFakeSignature);
    CHECK(PeSigner::HasEmbeddedSignature(path));
}

// ---------------------------------------------------------------------------
// The review's inputs and findings 14 and 18
// ---------------------------------------------------------------------------

TEST_CASE("review: PE e_lfanew -4", "[MalformedPe][review]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::int32_t>(bytes, seedtest::malformed::kPeLfanewField, -4);
    RequireAllRejected(bytes, AllOps(), "e_lfanew");
}

TEST_CASE("f14: PE WIN_CERTIFICATE length 0", "[MalformedPe][f14]")
{
    Bytes bytes = SignedTiny();
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, CertificateOffset(bytes), 0);
    RequireAllRejected(bytes,
                       {SignerOps()[1], SignerOps()[2], SignerOps()[3]},
                       "WIN_CERTIFICATE length");
}

TEST_CASE("f14: PE WIN_CERTIFICATE length 4", "[MalformedPe][f14]")
{
    Bytes bytes = SignedTiny();
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, CertificateOffset(bytes), 4);
    RequireAllRejected(bytes,
                       {SignerOps()[1], SignerOps()[2], SignerOps()[3]},
                       "WIN_CERTIFICATE length");
}

TEST_CASE("f14: PE WIN_CERTIFICATE length larger than the table", "[MalformedPe][f14]")
{
    Bytes bytes = SignedTiny();
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, CertificateOffset(bytes),
                                                CertificateSize(bytes) + 8);
    RequireAllRejected(bytes,
                       {SignerOps()[1], SignerOps()[2], SignerOps()[3]},
                       "WIN_CERTIFICATE length");
}

TEST_CASE("f18: PE partial digest", "[MalformedPe][f18]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField,
                                                static_cast<std::uint32_t>(bytes.size() - 8));
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4, 4096);
    RequireAllRejected(bytes, SignerOps(), "certificate table");
}

// ---------------------------------------------------------------------------
// Bounded work
// ---------------------------------------------------------------------------

TEST_CASE("edge: PE NumberOfSections 65535 in a small file", "[MalformedPe][edge]")
{
    Bytes bytes = seedtest::malformed::Truncate(Sample("tiny.exe"), 1024);
    seedtest::malformed::PatchLE<std::uint16_t>(bytes, kNumberOfSectionsField, 65535);
    RequireAllRejected(bytes, {ListOp()}, "section table");
}

TEST_CASE("edge: PE NumberOfSections 65535 in a full file", "[MalformedPe][edge]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint16_t>(bytes, kNumberOfSectionsField, 65535);
    RequireAllRejected(bytes, {ListOp()}, "section table");
}

TEST_CASE("edge: PE section VirtualAddress + size wraps 32 bits", "[MalformedPe][edge]")
{
    // The import table's section starts 16 bytes below 2^32 and claims 256
    // bytes, so a 32-bit sum wraps to a small number and the section would be
    // skipped. Computed in 64 bits it contains the import RVA, whose raw data
    // lies far past the end of the file.
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, SectionField(kTinyIdataSection, 8), 0x100);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, SectionField(kTinyIdataSection, 12),
                                                0xFFFFFFF0u);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, SectionField(kTinyIdataSection, 16), 0x100);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, SectionField(kTinyIdataSection, 20),
                                                0x7FFFFF00u);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kImportDirectoryField, 0xFFFFFFF8u);
    RequireAllRejected(bytes, {ListOp()}, "RVA");
}

namespace {

// Replaces the import table of tiny.exe with `count` descriptors that all
// name the string at `name_rva`, followed by a terminating all-zero descriptor.
Bytes TinyWithRepeatedImport(std::uint32_t count, std::uint32_t name_rva)
{
    Bytes bytes = Sample("tiny.exe");
    for(std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint64_t at = kTinyImportTable + 20ull * i;
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at, 1);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 4, 0);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 8, 0);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 12, name_rva);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 16, 1);
    }
    const std::uint64_t end = kTinyImportTable + 20ull * count;
    for(std::uint64_t i = 0; i < 20; ++i)
    {
        bytes[end + i] = 0;
    }
    return bytes;
}

// Writes `length` bytes of 'A' and a NUL into .text raw data starting at file
// offset `offset`; returns the RVA of that offset.
std::uint32_t PlantLongName(Bytes &bytes, std::uint64_t offset, std::uint64_t length)
{
    for(std::uint64_t i = 0; i < length; ++i)
    {
        bytes[offset + i] = 'A';
    }
    bytes[offset + length] = 0;
    // .text: raw data at 1024, virtual address 4096.
    return static_cast<std::uint32_t>(4096 + (offset - 1024));
}

} // namespace

TEST_CASE("edge: PE import descriptor table running into itself", "[MalformedPe][edge]")
{
    // Every descriptor from the start of the import table to the end of the
    // file is non-zero, so there is no terminator; the walk must stop at the
    // end of the file.
    Bytes bytes = Sample("tiny.exe");
    const std::uint32_t count = static_cast<std::uint32_t>((bytes.size() - kTinyImportTable) / 20);
    for(std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint64_t at = kTinyImportTable + 20ull * i;
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at, 1);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 4, 0);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 8, 0);
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 12, 0); // "MZ\x90"
        seedtest::malformed::PatchLE<std::uint32_t>(bytes, at + 16, 1);
    }
    // Keep the tail non-zero to the very end.
    for(std::uint64_t i = kTinyImportTable + 20ull * count; i < bytes.size(); ++i)
    {
        bytes[i] = 0xFF;
    }
    RequireAllRejected(bytes, {ListOp()}, "import descriptor");
}

TEST_CASE("edge: PE import descriptor table cut inside a descriptor", "[MalformedPe][edge]")
{
    const Bytes bytes = seedtest::malformed::Truncate(Sample("tiny.exe"), kTinyImportTable + 30);
    RequireAllRejected(bytes, {ListOp()}, "import descriptor");
}

TEST_CASE("edge: PE many import descriptors naming one long string", "[MalformedPe][edge]")
{
    // 64 descriptors name a 4000 byte string: 256000 name bytes from a
    // 13824 byte file. The total returned name bytes are bounded by the file
    // size, so the listing must be rejected rather than built.
    Bytes input = TinyWithRepeatedImport(64, 0);
    const std::uint32_t name_rva = PlantLongName(input, 2000, 4000);
    for(std::uint32_t i = 0; i < 64; ++i)
    {
        seedtest::malformed::PatchLE<std::uint32_t>(input, kTinyImportTable + 20ull * i + 12,
                                                    name_rva);
    }
    RequireAllRejected(input, {ListOp()}, "DLL name");
}

TEST_CASE("edge: PE DLL name longer than 4096 bytes", "[MalformedPe][edge]")
{
    Bytes bytes = Sample("tiny.exe");
    const std::uint32_t name_rva = PlantLongName(bytes, 2000, 5000);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kTinyImportTable + 12, name_rva);
    RequireAllRejected(bytes, {ListOp()}, "DLL name");
}

TEST_CASE("edge: PE e_lfanew -1", "[MalformedPe][edge]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::int32_t>(bytes, seedtest::malformed::kPeLfanewField, -1);
    RequireAllRejected(bytes, AllOps(), "e_lfanew");
}

TEST_CASE("edge: PE e_lfanew INT32_MIN", "[MalformedPe][edge]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::int32_t>(bytes, seedtest::malformed::kPeLfanewField,
                                               std::numeric_limits<std::int32_t>::min());
    RequireAllRejected(bytes, AllOps(), "e_lfanew");
}

// ---------------------------------------------------------------------------
// Structures past the end, unterminated text, inconsistent values
// ---------------------------------------------------------------------------

TEST_CASE("edge: PE section table past the end", "[MalformedPe][edge]")
{
    // The section table needs 752 bytes; keep 700.
    const Bytes bytes = seedtest::malformed::Truncate(Sample("tiny.exe"), 700);
    RequireAllRejected(bytes, {ListOp()}, "section table");
}

TEST_CASE("edge: PE import name past the end", "[MalformedPe][edge]")
{
    // A name RVA that no section contains.
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kTinyImportTable + 12, 0x00FFFF00u);
    RequireAllRejected(bytes, {ListOp()}, "DLL name");
}

TEST_CASE("edge: PE DLL name with no NUL before the end of the file", "[MalformedPe][edge]")
{
    // The first name points into .reloc, whose last bytes are all non-zero.
    Bytes bytes = Sample("tiny.exe");
    for(std::uint64_t i = 13312 + 500; i < bytes.size(); ++i)
    {
        bytes[i] = 'A';
    }
    // .reloc: raw data at 13312, virtual address 40960.
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kTinyImportTable + 12, 40960 + 500);
    RequireAllRejected(bytes, {ListOp()}, "DLL name");
}

TEST_CASE("edge: PE certificate table overlapping the headers", "[MalformedPe][edge]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField, 100);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4, 64);
    RequireAllRejected(bytes, SignerOps(), "certificate table");
}

TEST_CASE("edge: PE certificate address + size beyond 2^32", "[MalformedPe][edge]")
{
    Bytes bytes = Sample("tiny.exe");
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField, 0xFFFFFFF0u);
    seedtest::malformed::PatchLE<std::uint32_t>(bytes, kCertificateDirectoryField + 4, 0x100);
    RequireAllRejected(bytes, SignerOps(), "certificate table");
}

TEST_CASE("edge: PE zero-length file", "[MalformedPe][edge]")
{
    const Bytes empty;
    // The signers reject it; the lister reports the input in errors and does
    // not answer with an empty list.
    RequireAllRejected(empty, SignerOps(), "DOS header");

    seedtest::ScratchDir scratch("malformed-pe-empty");
    const std::string path = seedtest::malformed::WriteScratch(scratch, "empty.exe", empty);
    DependencyLister lister;
    const auto result = lister.ListDependencies({path}, {});
    REQUIRE(result.errors.count(path) == 1);
    CHECK_FALSE(result.errors.at(path).empty());
    CHECK(result.errors.at(path).find("internal error (") == std::string::npos);
    CHECK(result.dependencies.empty());
}

TEST_CASE("edge: PE truncated inside the DOS header", "[MalformedPe][edge]")
{
    RequireAllRejected(seedtest::malformed::Truncate(Sample("tiny.exe"), 40), AllOps(),
                       "DOS header");
}

TEST_CASE("edge: PE truncated inside the PE header", "[MalformedPe][edge]")
{
    // e_lfanew is 128; the COFF header and optional header start at 132 and
    // 152.
    RequireAllRejected(seedtest::malformed::Truncate(Sample("tiny.exe"), 140), AllOps(),
                       "PE header");
}

TEST_CASE("edge: PE truncated inside the section table", "[MalformedPe][edge]")
{
    RequireAllRejected(seedtest::malformed::Truncate(Sample("tiny.exe"), 500), {ListOp()},
                       "section table");
}

TEST_CASE("edge: PE truncated inside an import name", "[MalformedPe][edge]")
{
    // The first name "KERNEL32.dll" starts at 12460; keep two of its bytes.
    RequireAllRejected(seedtest::malformed::Truncate(Sample("tiny.exe"), kTinyFirstName + 2),
                       {ListOp()}, "DLL name");
}

TEST_CASE("edge: PE truncated inside the certificate table", "[MalformedPe][edge]")
{
    const Bytes signed_bytes = SignedTiny();
    RequireAllRejected(seedtest::malformed::Truncate(signed_bytes, signed_bytes.size() - 20),
                       SignerOps(), "certificate table");
}

TEST_CASE("edge: PE oversized signature size check", "[MalformedPe][edge]")
{
#ifdef SEED_HAVE_PE_SIZE_CHECK
    // The check takes a length, so no UINT32_MAX buffer is allocated.
    const std::uint64_t largest_signature = std::numeric_limits<std::uint32_t>::max() - 8;
    CHECK_NOTHROW(seed::internal::CheckPeSignatureSize(0));
    CHECK_NOTHROW(seed::internal::CheckPeSignatureSize(largest_signature));
    seedtest::malformed::RequireRejected(
        [&] { seed::internal::CheckPeSignatureSize(largest_signature + 1); }, "PE", "signature");
    seedtest::malformed::RequireRejected(
        [&] { seed::internal::CheckPeSignatureSize(std::numeric_limits<std::uint32_t>::max()); },
        "PE", "signature");
#else
    SKIP("the signature size check (src/internal/PeCertificate.hpp, "
         "seed::internal::CheckPeSignatureSize) does not exist yet");
#endif
}
