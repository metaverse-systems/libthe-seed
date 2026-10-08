#pragma once

// Helpers shared by the signer tests: file reads and writes, the fixed CMS
// bytes of the recorded known answers, and the full set of expectations a
// finished (signed) program must meet, judged by the independent checker in
// MachOReference.hpp and not by library code.

#include "MachOReference.hpp"
#include "TestPaths.hpp"

#include <libthe-seed/MachOSigner.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace machosupport {

namespace mr = machoref;
using mr::Bytes;

// The identity, capacity and CMS bytes of the recorded known answers.
inline const char *KnownIdentity()
{
    return "test-identity";
}

constexpr std::uint32_t KnownCapacity = 64;

// 00 01 02 ... (n - 1), wrapping after FF.
inline Bytes Counting(std::size_t n)
{
    Bytes b(n);
    for(std::size_t i = 0; i < n; ++i)
    {
        b[i] = static_cast<std::uint8_t>(i);
    }
    return b;
}

inline Bytes ReadAll(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void WriteAll(const std::string &path, const Bytes &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline Bytes LoadFixture(const std::string &name)
{
    return ReadAll(seedtest::FixturePath(name));
}

// Copies a sample into the scratch folder (under the same name, or `as`).
inline std::string CopyFixture(const seedtest::ScratchDir &scratch, const std::string &name,
                               const std::string &as = "")
{
    const std::string target = scratch.File(as.empty() ? name : as);
    std::filesystem::copy_file(seedtest::FixturePath(name), target,
                               std::filesystem::copy_options::overwrite_existing);
    return target;
}

// A scratch file holding the given bytes.
inline std::string WriteScratch(const seedtest::ScratchDir &scratch, const std::string &name,
                                const Bytes &bytes)
{
    const std::string target = scratch.File(name);
    WriteAll(target, bytes);
    return target;
}

inline std::uint64_t Align16(std::uint64_t value)
{
    return (value + 15) / 16 * 16;
}

inline std::uint64_t AlignUp(std::uint64_t value, std::uint64_t unit)
{
    return (value + unit - 1) / unit * unit;
}

// The memory page the link-edit size is rounded to: 16384 for arm64, 4096 for
// x86-64 (the CPU type of the slice).
inline std::uint64_t LinkeditPage(std::uint64_t cputype)
{
    return cputype == 0x0100000C ? 16384 : 4096;
}

// The unsigned samples that can be signed (each has room for the command).
inline const std::vector<std::string> &SignableSamples()
{
    static const std::vector<std::string> names = {
        "tiny-macho-x86_64", "tiny-macho-arm64", "tiny-macho-universal",
        "tiny-macho-universal64", "tiny-macho-dylib-arm64", "tiny-macho-x86_64-exactfit"};
    return names;
}

// One CMS per slice: the counting bytes, `size` long.
inline std::vector<Bytes> CountingCms(std::size_t slices, std::size_t size = KnownCapacity)
{
    return std::vector<Bytes>(slices, Counting(size));
}

// Prepares and completes with the identity, capacity and CMS of the known
// answers. Returns what PrepareSignature returned.
inline MachOSigner::PreparedSignature SignWithKnownInputs(const std::string &path)
{
    MachOSigner::PreparedSignature prepared =
        MachOSigner::PrepareSignature(path, KnownIdentity(), KnownCapacity);
    MachOSigner::CompleteSignature(path, prepared, CountingCms(prepared.slices.size()));
    return prepared;
}

// The text of the std::runtime_error a callable throws; fails the test when it
// throws something else or nothing.
template <typename Fn>
std::string ErrorOf(Fn &&fn)
{
    try
    {
        fn();
    }
    catch(const std::runtime_error &error)
    {
        return error.what();
    }
    catch(...)
    {
        FAIL("threw something other than std::runtime_error");
    }
    FAIL("expected std::runtime_error, nothing was thrown");
    return {};
}

inline std::string JoinProblems(const mr::FileReport &report)
{
    std::string out;
    for(const std::string &p : report.problems)
    {
        out += p + "; ";
    }
    return out;
}

// The statements every finished program must satisfy. `original` is the
// unsigned file as it was before signing, `finished` the file after
// CompleteSignature, `prepared` what PrepareSignature returned and `cms` the
// CMS bytes handed to CompleteSignature (one per slice, in order).
inline void ExpectFinished(const Bytes &original, const Bytes &finished,
                           const MachOSigner::PreparedSignature &prepared, const std::vector<Bytes> &cms)
{
    const mr::FileReport before = mr::CheckFile(original);
    const mr::FileReport after = mr::CheckFile(finished);
    INFO("problems: " << JoinProblems(after));
    CHECK(after.Ok());
    CHECK(after.form == before.form);
    REQUIRE(after.slices.size() == before.slices.size());
    REQUIRE(after.slices.size() == prepared.slices.size());
    REQUIRE(cms.size() == prepared.slices.size());
    for(std::size_t i = 0; i < after.slices.size(); ++i)
    {
        INFO("slice " << i);
        const mr::SliceReport &o = before.slices[i];
        const mr::SliceReport &s = after.slices[i];
        const MachOSigner::PreparedSlice &p = prepared.slices[i];
        const Bytes blob = MachOSigner::BuildSuperBlob(p.code_directory, cms[i]);
        const Bytes reserved_blob = MachOSigner::BuildSuperBlob(p.code_directory, Bytes(prepared.cms_capacity));

        CHECK(s.cputype == p.cpu_type);
        CHECK(s.cpusubtype == p.cpu_subtype);
        CHECK(s.cputype == o.cputype);
        // One command more, 16 bytes more, none of size zero.
        CHECK(s.signature_commands == 1);
        CHECK(s.ncmds == o.ncmds + 1);
        CHECK(s.sizeofcmds == o.sizeofcmds + 16);
        CHECK(s.walked_commands == s.ncmds);
        // Where the data starts and ends.
        CHECK(s.has_signature);
        CHECK(s.dataoff % 16 == 0);
        CHECK(s.dataoff == Align16(o.size));
        CHECK(s.datasize == Align16(reserved_blob.size()));
        CHECK(s.dataoff + s.datasize == s.size);
        // The link-edit segment ends where the slice ends.
        CHECK(s.linkedit_fileoff == o.linkedit_fileoff);
        CHECK(s.linkedit_fileoff + s.linkedit_filesize == s.size);
        CHECK(s.linkedit_vmsize == AlignUp(s.linkedit_filesize, LinkeditPage(s.cputype)));
        // The CodeDirectory covers exactly the bytes before the data.
        CHECK(s.code_limit == s.dataoff);
        CHECK(s.n_code_slots == (s.code_limit + 4095) / 4096);
        CHECK(s.page_ok.size() == s.n_code_slots);
        for(std::size_t page = 0; page < s.page_ok.size(); ++page)
        {
            INFO("page " << page);
            CHECK(s.page_ok[page]);
        }
        CHECK(s.requirements_ok);
        CHECK(s.identifier == prepared.identity);
        // The SuperBlob, then zero bytes up to the reserved size.
        CHECK(s.superblob_length == blob.size());
        REQUIRE(mr::Fits(finished, s.offset + s.dataoff, blob.size()));
        const Bytes written(finished.begin() + static_cast<std::ptrdiff_t>(s.offset + s.dataoff),
                            finished.begin() + static_cast<std::ptrdiff_t>(s.offset + s.dataoff + blob.size()));
        CHECK(written == blob);
        // What was returned by PrepareSignature is what is in the file.
        CHECK(p.cd_hash == mr::Sha256(p.code_directory.data(), p.code_directory.size()));
        const auto stored = MachOSigner::ExtractCodeDirectoryFromSuperBlob(written);
        REQUIRE(stored.has_value());
        CHECK(*stored == p.code_directory);
    }
    if(after.form == 0)
    {
        CHECK(finished.size() == after.slices[0].size);
    }
}

} // namespace machosupport
