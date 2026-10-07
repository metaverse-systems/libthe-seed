// The checker for signed Mac programs (MachOReference.hpp) and its recorded
// known answers (fixtures/macho-reference.txt).
//
// A signature check that was never shown to reject anything proves nothing, and
// one that was never shown to accept a real producer's output may be wrong
// about what a verifier computes. These tests show both:
//   - calibration: the signatures written by ld64.lld (a producer that shares no
//     code with the library) for arm64, x86-64 and the universal file are
//     accepted, with the values the independent Python tool recorded;
//   - negative control: flipping one byte before the signature makes exactly the
//     page that holds it fail, and no other;
//   - known answers: the facts recorded for every sample by check_pages.py and
//     llvm-lipo agree with the checker, and the programs the library signs with
//     the identity "test-identity", the CMS bytes 00 01 ... 3F and a capacity of
//     64 match the recorded length, SHA-256 and page hashes byte for byte.
//
// Setting SEED_MACHO_WRITE_SIGNED to a folder makes the known-answer test also
// write each signed program there (as signed-<sample>), so that the recording
// script can hash their pages with Python.

#include "MachOSigningSupport.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace mr = machoref;
namespace ms = machosupport;

namespace {

struct LldSample
{
    const char *name;
    std::size_t slices;
};

const LldSample lld_samples[] = {
    {"tiny-macho-arm64-adhoc", 1}, {"tiny-macho-x86_64-adhoc", 1}, {"tiny-macho-universal-adhoc", 2}};

// The text of one "== name" block of macho-reference.txt (without the header line).
std::vector<std::string> RecordedBlock(const std::string &title)
{
    std::ifstream in(seedtest::FixturePath("macho-reference.txt"));
    std::vector<std::string> lines;
    std::string line;
    bool inside = false;
    while(std::getline(in, line))
    {
        if(line.rfind("== ", 0) == 0)
        {
            inside = line == "== " + title;
            continue;
        }
        if(inside)
        {
            lines.push_back(line);
        }
    }
    return lines;
}

// "name=<number>" read from a recorded line; npos when absent.
std::uint64_t Field(const std::string &line, const std::string &name)
{
    const std::string key = name + "=";
    const std::size_t at = line.find(key);
    if(at == std::string::npos)
    {
        return UINT64_MAX;
    }
    return std::stoull(line.substr(at + key.size()), nullptr, 0);
}

struct Recorded
{
    std::vector<std::string> pages;                // in file order, over all slices
    std::vector<std::uint64_t> ncmds, sizeofcmds;  // per slice
    std::vector<std::uint64_t> linkedit_fileoff, linkedit_filesize, linkedit_vmsize;
    std::vector<std::uint64_t> dataoff, datasize, code_limit;  // per signed slice
    std::uint64_t length = UINT64_MAX;
    std::string sha256;
};

Recorded Parse(const std::vector<std::string> &lines)
{
    Recorded r;
    static const std::regex page_line("^\\s*page \\d+ ([0-9a-f]{64})$");
    static const std::regex sha_line("^sha256 ([0-9a-f]{64})$");
    for(const std::string &line : lines)
    {
        std::smatch m;
        if(std::regex_match(line, m, page_line))
        {
            r.pages.push_back(m[1]);
        }
        else if(std::regex_match(line, m, sha_line))
        {
            r.sha256 = m[1];
        }
        else if(line.rfind("file ", 0) == 0)
        {
            r.length = Field(line, "length");
        }
        else if(line.rfind("length ", 0) == 0)
        {
            r.length = std::stoull(line.substr(7));
        }
        else if(line.find("  header ") == 0)
        {
            r.ncmds.push_back(Field(line, "ncmds"));
            r.sizeofcmds.push_back(Field(line, "sizeofcmds"));
        }
        else if(line.find("  linkedit ") == 0)
        {
            r.linkedit_fileoff.push_back(Field(line, "fileoff"));
            r.linkedit_filesize.push_back(Field(line, "filesize"));
            r.linkedit_vmsize.push_back(Field(line, "vmsize"));
        }
        else if(line.find("  codesig ") == 0)
        {
            r.dataoff.push_back(Field(line, "dataoff"));
            r.datasize.push_back(Field(line, "datasize"));
        }
        else if(line.find("  codedirectory ") == 0)
        {
            r.code_limit.push_back(Field(line, "codeLimit"));
        }
    }
    return r;
}

std::vector<std::string> AllPageHashes(const mr::FileReport &report)
{
    std::vector<std::string> out;
    for(const mr::SliceReport &s : report.slices)
    {
        out.insert(out.end(), s.page_hashes.begin(), s.page_hashes.end());
    }
    return out;
}

// Flips one byte of the slice-relative offset and returns the checker's view.
mr::FileReport CheckWithFlip(mr::Bytes file, std::uint64_t absolute_offset)
{
    file.at(static_cast<std::size_t>(absolute_offset)) ^= 0x01;
    return mr::CheckFile(file);
}

} // namespace

TEST_CASE("reference: the checker accepts the signatures written by ld64.lld", "[MachOReference]")
{
    for(const LldSample &sample : lld_samples)
    {
        INFO(sample.name);
        const mr::Bytes file = ms::LoadFixture(sample.name);
        const mr::FileReport report = mr::CheckFile(file);
        INFO("problems: " << ms::JoinProblems(report));
        CHECK(report.Ok());
        REQUIRE(report.slices.size() == sample.slices);
        for(const mr::SliceReport &s : report.slices)
        {
            CHECK(s.has_signature);
            CHECK(s.signature_commands == 1);
            CHECK(s.walked_commands == s.ncmds);
            CHECK(s.code_limit == s.dataoff);
            CHECK(s.dataoff % 16 == 0);
            CHECK(s.dataoff + s.datasize == s.size);
            CHECK(s.n_code_slots > 0);
            CHECK(s.page_ok.size() == s.n_code_slots);
            for(bool ok : s.page_ok)
            {
                CHECK(ok);
            }
        }
    }
}

TEST_CASE("reference: calibration facts of the ld64.lld signatures", "[MachOReference]")
{
    // Values recorded by the independent Python tool (macho-reference.txt) and
    // printed by llvm-otool: they are the same for the checker.
    const mr::FileReport arm = mr::CheckFile(ms::LoadFixture("tiny-macho-arm64-adhoc"));
    REQUIRE(arm.slices.size() == 1);
    CHECK(arm.form == 0);
    CHECK(arm.slices[0].dataoff == 16544);
    CHECK(arm.slices[0].datasize == 304);
    CHECK(arm.slices[0].code_limit == 16544);
    CHECK(arm.slices[0].n_code_slots == 5);
    CHECK(arm.slices[0].identifier == "tiny-macho-arm64-adhoc");

    const mr::FileReport x86 = mr::CheckFile(ms::LoadFixture("tiny-macho-x86_64-adhoc"));
    REQUIRE(x86.slices.size() == 1);
    CHECK(x86.slices[0].dataoff == 4256);
    CHECK(x86.slices[0].datasize == 208);
    CHECK(x86.slices[0].code_limit == 4256);
    CHECK(x86.slices[0].n_code_slots == 2);
    CHECK(x86.slices[0].identifier == "tiny-macho-x86_64-adhoc");

    const mr::FileReport fat = mr::CheckFile(ms::LoadFixture("tiny-macho-universal-adhoc"));
    REQUIRE(fat.slices.size() == 2);
    CHECK(fat.form == 1);
    CHECK(fat.slices[0].offset == 4096);
    CHECK(fat.slices[1].offset == 16384);
    CHECK(fat.slices[0].dataoff == 4256);
    CHECK(fat.slices[1].dataoff == 16544);
}

TEST_CASE("reference: one flipped byte makes exactly the covering page fail", "[MachOReference]")
{
    struct Flip
    {
        const char *sample;
        std::size_t slice;
        std::uint64_t at;  // offset inside the slice
        std::size_t page;  // the page that holds it
    };
    const Flip flips[] = {
        {"tiny-macho-x86_64-adhoc", 0, 3000, 0},
        {"tiny-macho-x86_64-adhoc", 0, 4096, 1},
        {"tiny-macho-x86_64-adhoc", 0, 4255, 1},  // the last byte covered
        {"tiny-macho-arm64-adhoc", 0, 4095, 0},   // the last byte of the first page
        {"tiny-macho-arm64-adhoc", 0, 8192 + 7, 2},
        {"tiny-macho-arm64-adhoc", 0, 16543, 4},
        {"tiny-macho-universal-adhoc", 0, 4100, 1},
        {"tiny-macho-universal-adhoc", 1, 12288 + 5, 3},
    };
    for(const Flip &flip : flips)
    {
        INFO(flip.sample << " slice " << flip.slice << " offset " << flip.at);
        const mr::Bytes file = ms::LoadFixture(flip.sample);
        const mr::FileReport clean = mr::CheckFile(file);
        REQUIRE(clean.Ok());
        const std::uint64_t absolute = clean.slices[flip.slice].offset + flip.at;
        const mr::FileReport bad = CheckWithFlip(file, absolute);
        CHECK_FALSE(bad.Ok());
        for(std::size_t s = 0; s < bad.slices.size(); ++s)
        {
            std::size_t failed = 0;
            for(std::size_t p = 0; p < bad.slices[s].page_ok.size(); ++p)
            {
                if(!bad.slices[s].page_ok[p])
                {
                    ++failed;
                    CHECK(s == flip.slice);
                    CHECK(p == flip.page);
                }
            }
            CHECK(failed == (s == flip.slice ? 1u : 0u));
        }
        // Nothing but the page hash is reported.
        for(const std::string &problem : bad.problems)
        {
            CHECK(problem.find("hash does not match") != std::string::npos);
        }
    }
}

TEST_CASE("reference: the recorded answers of every sample agree with the checker", "[MachOReference]")
{
    const std::vector<std::string> samples = {
        "tiny-macho-x86_64", "tiny-macho-arm64", "tiny-macho-universal", "tiny-macho-arm64-adhoc",
        "tiny-macho-x86_64-adhoc", "tiny-macho-universal-adhoc", "tiny-macho-x86_64-nospace",
        "tiny-macho-x86_64-exactfit", "tiny-macho-universal64", "tiny-macho-dylib-arm64"};
    for(const std::string &name : samples)
    {
        INFO(name);
        const std::vector<std::string> lines = RecordedBlock(name);
        REQUIRE_FALSE(lines.empty());
        const Recorded rec = Parse(lines);
        const mr::Bytes file = ms::LoadFixture(name);
        const mr::FileReport report = mr::CheckFile(file);
        CHECK(report.Ok());
        CHECK(rec.length == file.size());
        REQUIRE(report.slices.size() == rec.ncmds.size());
        for(std::size_t i = 0; i < report.slices.size(); ++i)
        {
            INFO("slice " << i);
            const mr::SliceReport &s = report.slices[i];
            CHECK(rec.ncmds[i] == s.ncmds);
            CHECK(rec.sizeofcmds[i] == s.sizeofcmds);
            CHECK(rec.linkedit_fileoff[i] == s.linkedit_fileoff);
            CHECK(rec.linkedit_filesize[i] == s.linkedit_filesize);
            CHECK(rec.linkedit_vmsize[i] == s.linkedit_vmsize);
        }
        // Signed samples: the Python tool's signature facts and page hashes.
        std::size_t signed_slices = 0;
        for(const mr::SliceReport &s : report.slices)
        {
            signed_slices += s.has_signature ? 1 : 0;
        }
        REQUIRE(rec.dataoff.size() == signed_slices);
        std::size_t k = 0;
        for(const mr::SliceReport &s : report.slices)
        {
            if(!s.has_signature)
            {
                continue;
            }
            CHECK(rec.dataoff[k] == s.dataoff);
            CHECK(rec.datasize[k] == s.datasize);
            CHECK(rec.code_limit[k] == s.code_limit);
            ++k;
        }
        CHECK(rec.pages == AllPageHashes(report));
    }
}

TEST_CASE("reference: the recorded input hashes match the samples", "[MachOReference]")
{
    std::ifstream in(seedtest::FixturePath("macho-reference.txt"));
    static const std::regex sample_line("^# sample (\\S+) ([0-9a-f]{64})$");
    std::string line;
    std::size_t seen = 0;
    while(std::getline(in, line))
    {
        std::smatch m;
        if(std::regex_match(line, m, sample_line))
        {
            const mr::Bytes file = ms::LoadFixture(m[1]);
            INFO(m[1]);
            CHECK(mr::Hex(mr::Sha256(file.data(), file.size())) == m[2].str());
            ++seen;
        }
    }
    CHECK(seen >= 10);
}

TEST_CASE("reference: programs signed with the known inputs match the recorded answers", "[MachOReference]")
{
    const char *write_to = std::getenv("SEED_MACHO_WRITE_SIGNED");
    for(const std::string &name : ms::SignableSamples())
    {
        INFO(name);
        seedtest::ScratchDir scratch;
        const std::string path = ms::CopyFixture(scratch, name);
        const mr::Bytes original = ms::ReadAll(path);

        const MachOSigner::PreparedSignature prepared = ms::SignWithKnownInputs(path);
        const mr::Bytes finished = ms::ReadAll(path);
        if(write_to != nullptr && write_to[0] != '\0')
        {
            ms::WriteAll((std::filesystem::path(write_to) / ("signed-" + name)).string(), finished);
        }

        // The checker accepts it (all page hashes, sizes and fields).
        ms::ExpectFinished(original, finished, prepared, ms::CountingCms(prepared.slices.size()));

        // Byte for byte against the recording made with Python.
        const std::vector<std::string> lines = RecordedBlock("signed " + name);
        if(lines.empty())
        {
            // Not FAIL: the other samples are still signed and written, so one
            // recording run can produce all of them.
            FAIL_CHECK("no recorded known answer for the signed " << name
                       << " in macho-reference.txt; record it with regenerate.sh --reference");
            continue;
        }
        const Recorded rec = Parse(lines);
        CHECK(rec.length == finished.size());
        CHECK(rec.sha256 == mr::Hex(mr::Sha256(finished.data(), finished.size())));
        CHECK(rec.pages == AllPageHashes(mr::CheckFile(finished)));
        CHECK_FALSE(rec.pages.empty());
    }
}
