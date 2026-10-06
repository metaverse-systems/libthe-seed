// Malformed and edge-case Linux program and library files, through
// DependencyLister (the only public way to read an ELF file).
//
// Test case names start with their origin: "ok:" for an unmodified or
// legal-but-unusual input with today's result, "review:" for the review's
// input, and "edge:" for an edge-case family.
//
// Malformed inputs are checked on the text DependencyLister reports in
// DependencyResult::errors for a directly named input: it starts with "ELF: "
// and names the structure at fault. The keywords the tests look for are:
//   ELF header                 the header does not fit in the file
//   program header table       table (or e_phnum entries of e_phentsize bytes) outside the file
//   program header entry size  e_phentsize smaller than a program header
//   PT_LOAD                    a load segment outside the file, or whose address range wraps
//   PT_DYNAMIC                 the dynamic segment outside the file
//   DT_STRTAB                  string table address not inside the file-backed part of a load segment
//   DT_NEEDED                  a name offset outside the string table, a name without NUL, a name
//                              over 4096 bytes, or more name bytes in total than the file holds

#include "MalformedInput.hpp"

#include <libthe-seed/DependencyLister.hpp>

#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

SEED_DEFINE_HEAP_COUNTER()

namespace {

using seedtest::malformed::Bytes;

constexpr std::uint16_t kPtLoad = 1;
constexpr std::uint16_t kPtDynamic = 2;
constexpr std::int64_t kDtNull = 0;
constexpr std::int64_t kDtNeeded = 1;
constexpr std::int64_t kDtStrtab = 5;
constexpr std::int64_t kDtStrsz = 10;
constexpr std::uint64_t kBase = 0x400000;

// A small ELF image: header, a program header table, a dynamic table and a
// string table, with one PT_LOAD covering the whole file and one PT_DYNAMIC.
// The offsets of every field are public so a test patches only the field it
// is about.
class ElfImage
{
public:
    bool is64 = true;
    bool little = true;
    bool with_dynamic = true;
    std::vector<std::string> needed = {"libfoo.so.1", "libbar.so.2"};
    bool with_strsz = false;

    Bytes bytes;

    std::uint64_t ehdr_size = 0;
    std::uint64_t phdr_size = 0;
    std::uint64_t dyn_size = 0;
    std::uint64_t phoff = 0;
    std::uint64_t phnum = 0;
    std::uint64_t dyn_offset = 0;
    std::uint64_t dyn_count = 0;
    std::uint64_t str_offset = 0;
    std::uint64_t str_size = 0;
    std::vector<std::uint64_t> name_offsets; // offsets inside the string table

    ElfImage &Build()
    {
        this->ehdr_size = this->is64 ? 64 : 52;
        this->phdr_size = this->is64 ? 56 : 32;
        this->dyn_size = this->is64 ? 16 : 8;
        this->phoff = this->ehdr_size;
        this->phnum = this->with_dynamic ? 2 : 1;

        std::string strtab(1, '\0');
        this->name_offsets.clear();
        for(const auto &name : this->needed)
        {
            this->name_offsets.push_back(strtab.size());
            strtab += name;
            strtab.push_back('\0');
        }
        this->str_size = strtab.size();

        this->dyn_offset = (this->phoff + this->phnum * this->phdr_size + 7) / 8 * 8;
        this->dyn_count = this->needed.size() + 1 + (this->with_strsz ? 1 : 0) + 1;
        std::uint64_t size = this->dyn_offset;
        if(this->with_dynamic)
        {
            size += this->dyn_count * this->dyn_size;
        }
        this->str_offset = size;
        size += this->str_size;

        this->bytes.assign(size, 0);
        this->bytes[0] = 0x7F;
        this->bytes[1] = 'E';
        this->bytes[2] = 'L';
        this->bytes[3] = 'F';
        this->bytes[4] = this->is64 ? 2 : 1;
        this->bytes[5] = this->little ? 1 : 2;
        this->bytes[6] = 1;
        this->Put(16, 3, 2); // e_type ET_DYN
        this->Put(18, 62, 2); // e_machine
        this->Put(20, 1, 4);  // e_version
        this->Put(this->PhoffField(), this->phoff, this->is64 ? 8 : 4);
        this->Put(this->is64 ? 52 : 40, this->ehdr_size, 2);
        this->Put(this->PhentsizeField(), this->phdr_size, 2);
        this->Put(this->PhnumField(), this->phnum, 2);

        // PT_LOAD over the whole file.
        this->SetPhdr(0, kPtLoad, 0, kBase, size, size);
        if(this->with_dynamic)
        {
            this->SetPhdr(1, kPtDynamic, this->dyn_offset, kBase + this->dyn_offset,
                          this->dyn_count * this->dyn_size, this->dyn_count * this->dyn_size);

            std::uint64_t index = 0;
            for(const auto name_offset : this->name_offsets)
            {
                this->SetDyn(index++, kDtNeeded, name_offset);
            }
            this->SetDyn(index++, kDtStrtab, kBase + this->str_offset);
            if(this->with_strsz)
            {
                this->SetDyn(index++, kDtStrsz, this->str_size);
            }
            this->SetDyn(index++, kDtNull, 0);
        }
        for(std::size_t i = 0; i < strtab.size(); ++i)
        {
            this->bytes[this->str_offset + i] = static_cast<std::uint8_t>(strtab[i]);
        }
        return *this;
    }

    void Put(std::uint64_t offset, std::uint64_t value, std::size_t width)
    {
        REQUIRE(offset + width <= this->bytes.size());
        for(std::size_t i = 0; i < width; ++i)
        {
            const std::size_t shift = this->little ? i : width - 1 - i;
            this->bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * shift)) & 0xFF);
        }
    }

    std::uint64_t PhoffField() const { return this->is64 ? 32 : 28; }
    std::uint64_t PhentsizeField() const { return this->is64 ? 54 : 42; }
    std::uint64_t PhnumField() const { return this->is64 ? 56 : 44; }
    std::size_t Word() const { return this->is64 ? 8 : 4; }

    std::uint64_t PhdrOffset(std::uint64_t index) const
    {
        return this->phoff + index * this->phdr_size;
    }

    // Field offsets inside a program header.
    std::uint64_t PhdrOffsetField(std::uint64_t index) const
    {
        return this->PhdrOffset(index) + (this->is64 ? 8 : 4);
    }
    std::uint64_t PhdrVaddrField(std::uint64_t index) const
    {
        return this->PhdrOffset(index) + (this->is64 ? 16 : 8);
    }
    std::uint64_t PhdrFileszField(std::uint64_t index) const
    {
        return this->PhdrOffset(index) + (this->is64 ? 32 : 16);
    }
    std::uint64_t PhdrMemszField(std::uint64_t index) const
    {
        return this->PhdrOffset(index) + (this->is64 ? 40 : 20);
    }

    void SetPhdr(std::uint64_t index, std::uint32_t type, std::uint64_t offset,
                 std::uint64_t vaddr, std::uint64_t filesz, std::uint64_t memsz)
    {
        this->Put(this->PhdrOffset(index), type, 4);
        this->Put(this->PhdrOffsetField(index), offset, this->Word());
        this->Put(this->PhdrVaddrField(index), vaddr, this->Word());
        this->Put(this->PhdrFileszField(index), filesz, this->Word());
        this->Put(this->PhdrMemszField(index), memsz, this->Word());
    }

    std::uint64_t DynTagField(std::uint64_t index) const
    {
        return this->dyn_offset + index * this->dyn_size;
    }
    std::uint64_t DynValueField(std::uint64_t index) const
    {
        return this->DynTagField(index) + this->Word();
    }

    void SetDyn(std::uint64_t index, std::int64_t tag, std::uint64_t value)
    {
        this->Put(this->DynTagField(index), static_cast<std::uint64_t>(tag), this->Word());
        this->Put(this->DynValueField(index), value, this->Word());
    }

    // Index of the DT_STRTAB entry in the dynamic table.
    std::uint64_t StrtabIndex() const { return this->needed.size(); }
    std::uint64_t StrszIndex() const { return this->needed.size() + 1; }
};

ElfImage MakeImage(bool is64, bool little = true, std::vector<std::string> needed = {"libfoo.so.1", "libbar.so.2"},
                   bool with_strsz = false)
{
    ElfImage image;
    image.is64 = is64;
    image.little = little;
    image.needed = std::move(needed);
    image.with_strsz = with_strsz;
    image.Build();
    return image;
}

std::string WriteImage(const seedtest::ScratchDir &scratch, const Bytes &bytes)
{
    return seedtest::malformed::WriteScratch(scratch, "input.elf", bytes);
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

void RequireListRejected(const Bytes &input, const std::string &keyword)
{
    seedtest::ScratchDir scratch("malformed-elf");
    const std::string path = WriteImage(scratch, input);
    seedtest::malformed::RequireRejected([&] { ListOrThrow(path); }, "ELF", keyword,
                                         input.size());
}

// For inputs that the rules do not reject for certain: the call finishes within
// the time and heap limits and either lists dependencies or is rejected the
// way RequireRejected describes (any keyword).
void RequireBoundedListing(const Bytes &input)
{
    seedtest::ScratchDir scratch("malformed-elf");
    const std::string path = WriteImage(scratch, input);
    const auto outcome = seedtest::malformed::detail::Run([&] { ListOrThrow(path); });
    if(outcome.threw)
    {
        seedtest::malformed::detail::CheckRejection(outcome, "ELF", "");
    }
    seedtest::malformed::detail::CheckLimits(outcome, input.size(), 0);
}

DependencyResult ListOne(const std::string &path)
{
    DependencyLister lister;
    return lister.ListDependencies({path}, {});
}

// Both widths for the cases whose rule is the same in the 32-bit and 64-bit
// path.
template <typename F>
void ForBothWidths(F &&body)
{
    for(const bool is64 : {true, false})
    {
        DYNAMIC_SECTION(std::string(is64 ? "ELF64" : "ELF32"))
        {
            body(is64);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Well-formed inputs
// ---------------------------------------------------------------------------

TEST_CASE("ok: DependencyLister lists the dependencies of the library under test", "[MalformedElf][ok]")
{
    const std::string path = seedtest::LibraryPath();
    const auto result = ListOne(path);
    CHECK(result.errors.empty());
    // Present in every build variant (sanitizer builds add more).
    for(const std::string name : {"libecs-cpp.so.2", "libstdc++.so.6", "libgcc_s.so.1", "libc.so.6"})
    {
        INFO(name);
        REQUIRE(result.dependencies.count(name) == 1);
        CHECK(result.dependencies.at(name) == std::vector<std::string>{path});
    }
    for(const auto &entry : result.dependencies)
    {
        CHECK_FALSE(entry.first.empty());
    }
}

TEST_CASE("ok: DependencyLister lists a 64-bit little-endian ELF image", "[MalformedElf][ok]")
{
    seedtest::ScratchDir scratch("malformed-elf-ok");
    const ElfImage image = MakeImage(true);
    const std::string path = WriteImage(scratch, image.bytes);
    const auto result = ListOne(path);
    CHECK(result.errors.empty());
    CHECK(result.dependencies == std::map<std::string, std::vector<std::string>>{
                                     {"libbar.so.2", {path}}, {"libfoo.so.1", {path}}});
}

TEST_CASE("ok: DependencyLister lists a 32-bit ELF image with DT_STRSZ", "[MalformedElf][ok]")
{
    seedtest::ScratchDir scratch("malformed-elf-ok");
    for(const bool little : {true, false})
    {
        DYNAMIC_SECTION(std::string(little ? "little-endian" : "big-endian"))
        {
            const ElfImage image = MakeImage(false, little, {"libm.so.6", "libc.so.6", "libdl.so.2"}, true);
            const std::string path = WriteImage(scratch, image.bytes);
            const auto result = ListOne(path);
            CHECK(result.errors.empty());
            CHECK(result.dependencies ==
                  std::map<std::string, std::vector<std::string>>{
                      {"libc.so.6", {path}}, {"libdl.so.2", {path}}, {"libm.so.6", {path}}});
        }
    }
}

TEST_CASE("ok: ELF image with no PT_DYNAMIC has no dependencies", "[MalformedElf][ok]")
{
    seedtest::ScratchDir scratch("malformed-elf-ok");
    for(const bool is64 : {true, false})
    {
        DYNAMIC_SECTION(std::string(is64 ? "ELF64" : "ELF32"))
        {
            ElfImage image;
            image.is64 = is64;
            image.with_dynamic = false;
            image.needed = {};
            image.Build();
            const std::string path = WriteImage(scratch, image.bytes);
            const auto result = ListOne(path);
            CHECK(result.errors.empty());
            CHECK(result.dependencies.empty());
        }
    }
}

TEST_CASE("ok: ELF image whose PT_DYNAMIC has no file data has no dependencies", "[MalformedElf][ok]")
{
    seedtest::ScratchDir scratch("malformed-elf-ok");
    ElfImage image = MakeImage(true);
    image.Put(image.PhdrFileszField(1), 0, 8);
    const std::string path = WriteImage(scratch, image.bytes);
    const auto result = ListOne(path);
    CHECK(result.errors.empty());
    CHECK(result.dependencies.empty());
}

// ---------------------------------------------------------------------------
// The review's input
// ---------------------------------------------------------------------------

TEST_CASE("review: ELF64 e_phoff near 2^64", "[MalformedElf][review]")
{
    Bytes bytes = seedtest::malformed::LoadLibraryUnderTest();
    seedtest::malformed::PatchLE<std::uint64_t>(bytes, seedtest::malformed::kElf64PhoffField,
                                                std::numeric_limits<std::uint64_t>::max() - 7);
    RequireListRejected(bytes, "program header table");
}

// ---------------------------------------------------------------------------
// Bounded work
// ---------------------------------------------------------------------------

TEST_CASE("edge: ELF e_phoff near the largest value", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        const std::uint64_t value = is64 ? std::numeric_limits<std::uint64_t>::max() - 7
                                         : std::numeric_limits<std::uint32_t>::max() - 7;
        image.Put(image.PhoffField(), value, image.Word());
        RequireListRejected(image.bytes, "program header table");
    });
}

TEST_CASE("edge: ELF e_phnum 65535 in a small file", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        image.Put(image.PhnumField(), 65535, 2);
        RequireListRejected(image.bytes, "program header table");
    });
}

TEST_CASE("edge: ELF e_phentsize 0", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        image.Put(image.PhentsizeField(), 0, 2);
        RequireListRejected(image.bytes, "program header entry size");
    });
}

TEST_CASE("edge: ELF DT_NEEDED name offset 2^64 - 1", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        const std::uint64_t value = is64 ? std::numeric_limits<std::uint64_t>::max()
                                         : std::numeric_limits<std::uint32_t>::max();
        image.Put(image.DynValueField(0), value, image.Word());
        RequireListRejected(image.bytes, "DT_NEEDED");
    });
}

TEST_CASE("edge: ELF p_vaddr + p_memsz wraps", "[MalformedElf][edge]")
{
    // The load segment starts 256 bytes below 2^64 and claims 4096 bytes, so
    // its end wraps to a small number.
    ElfImage image = MakeImage(true);
    image.Put(image.PhdrVaddrField(0), std::numeric_limits<std::uint64_t>::max() - 255, 8);
    image.Put(image.PhdrMemszField(0), 4096, 8);
    image.Put(image.DynValueField(image.StrtabIndex()),
              std::numeric_limits<std::uint64_t>::max() - 255 + image.str_offset, 8);
    RequireListRejected(image.bytes, "PT_LOAD");
}

TEST_CASE("edge: ELF PT_LOAD file data past the end", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        image.Put(image.PhdrFileszField(0), image.bytes.size() + 1, image.Word());
        RequireListRejected(image.bytes, "PT_LOAD");
    });
}

TEST_CASE("edge: ELF DT_STRTAB pointing into the dynamic table itself", "[MalformedElf][edge]")
{
    // The table's own bytes are read as the string table. Whatever the library
    // decides, the work stays bounded.
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        image.Put(image.DynValueField(image.StrtabIndex()), kBase + image.dyn_offset, image.Word());
        RequireBoundedListing(image.bytes);
    });
}

TEST_CASE("edge: ELF DT_STRTAB maps only into the memory-only part of a segment", "[MalformedElf][edge]")
{
    // The load segment's file data ends where the string table starts; its
    // memory size still covers the table. Bytes of the string table exist in
    // the file, but not in the segment's file-backed part.
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        image.Put(image.PhdrFileszField(0), image.str_offset, image.Word());
        RequireListRejected(image.bytes, "DT_STRTAB");
    });
}

TEST_CASE("edge: ELF PT_DYNAMIC past the end", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        SECTION("size runs past the end")
        {
            ElfImage image = MakeImage(is64);
            image.Put(image.PhdrFileszField(1), image.bytes.size(), image.Word());
            RequireListRejected(image.bytes, "PT_DYNAMIC");
        }
        SECTION("offset near the largest value")
        {
            ElfImage image = MakeImage(is64);
            const std::uint64_t value = is64 ? std::numeric_limits<std::uint64_t>::max() - 15
                                             : std::numeric_limits<std::uint32_t>::max() - 15;
            image.Put(image.PhdrOffsetField(1), value, image.Word());
            RequireListRejected(image.bytes, "PT_DYNAMIC");
        }
    });
}

TEST_CASE("edge: ELF string table past the end", "[MalformedElf][edge]")
{
    // The file ends before the string table, but the load segment still
    // claims all of it.
    ForBothWidths([](bool is64) {
        const ElfImage image = MakeImage(is64);
        RequireListRejected(seedtest::malformed::Truncate(image.bytes, image.str_offset), "PT_LOAD");
    });
}

TEST_CASE("edge: ELF DT_STRTAB at the end of the file-backed data", "[MalformedElf][edge]")
{
    // The address is one past the last file-backed byte: the table is empty,
    // so no name offset can be valid.
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        image.Put(image.DynValueField(image.StrtabIndex()), kBase + image.bytes.size(), image.Word());
        RequireListRejected(image.bytes, "DT_STRTAB");
    });
}

TEST_CASE("edge: ELF DT_NEEDED name with no NUL before the end of the string table", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64);
        for(std::uint64_t i = image.str_offset + 1; i < image.bytes.size(); ++i)
        {
            image.bytes[i] = 'A';
        }
        RequireListRejected(image.bytes, "DT_NEEDED");
    });
}

TEST_CASE("edge: ELF DT_STRSZ larger than the segment", "[MalformedElf][edge]")
{
    // The string table view ends at the end of the segment's file data or at
    // DT_STRSZ, whichever is first, so a larger DT_STRSZ changes nothing.
    ForBothWidths([](bool is64) {
        SECTION("names inside the segment are listed as before")
        {
            seedtest::ScratchDir scratch("malformed-elf-strsz");
            ElfImage image = MakeImage(is64, true, {"libfoo.so.1", "libbar.so.2"}, true);
            image.Put(image.DynValueField(image.StrszIndex()), 0x10000000, image.Word());
            const std::string path = WriteImage(scratch, image.bytes);
            const auto result = ListOne(path);
            CHECK(result.errors.empty());
            CHECK(result.dependencies == std::map<std::string, std::vector<std::string>>{
                                             {"libbar.so.2", {path}}, {"libfoo.so.1", {path}}});
        }
        SECTION("a name that runs to the end of the segment is rejected")
        {
            ElfImage image = MakeImage(is64, true, {"libfoo.so.1"}, true);
            image.Put(image.DynValueField(image.StrszIndex()), 0x10000000, image.Word());
            for(std::uint64_t i = image.str_offset + 1; i < image.bytes.size(); ++i)
            {
                image.bytes[i] = 'A';
            }
            RequireListRejected(image.bytes, "DT_NEEDED");
        }
    });
}

TEST_CASE("edge: ELF DT_NEEDED name beyond DT_STRSZ", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64, true, {"libfoo.so.1", "libbar.so.2"}, true);
        // The second name starts at 13; a size of 8 ends inside the first.
        image.Put(image.DynValueField(image.StrszIndex()), 8, image.Word());
        RequireListRejected(image.bytes, "DT_NEEDED");
    });
}

TEST_CASE("edge: ELF DT_NEEDED name longer than 4096 bytes", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64, true, {std::string(5000, 'a')});
        RequireListRejected(image.bytes, "DT_NEEDED");
    });
}

TEST_CASE("edge: ELF many DT_NEEDED entries naming one long string", "[MalformedElf][edge]")
{
    // 2000 entries name one 3000 byte string: 6 million name bytes from a file
    // of about 40 thousand bytes. The total returned name bytes are bounded by
    // the file size, so the listing must be rejected rather than built.
    ForBothWidths([](bool is64) {
        ElfImage image = MakeImage(is64, true, std::vector<std::string>(2000, std::string(3000, 'a')));
        // MakeImage stored each name separately; point every entry at the first
        // and drop the rest of the string table.
        for(std::uint64_t i = 0; i < 2000; ++i)
        {
            image.Put(image.DynValueField(i), image.name_offsets[0], image.Word());
        }
        const std::uint64_t keep = image.name_offsets[1] + 1;
        Bytes small(image.bytes.begin(), image.bytes.begin() + static_cast<std::ptrdiff_t>(image.str_offset + keep));
        image.bytes = small;
        image.Put(image.PhdrFileszField(0), image.bytes.size(), image.Word());
        image.Put(image.PhdrMemszField(0), image.bytes.size(), image.Word());
        RequireListRejected(image.bytes, "DT_NEEDED");
    });
}

// ---------------------------------------------------------------------------
// Short and truncated files
// ---------------------------------------------------------------------------

TEST_CASE("edge: ELF zero-length file", "[MalformedElf][edge]")
{
    // The lister reports the input in errors and does not answer with an empty
    // list. The format is not known for an empty file, so no message prefix is
    // required.
    seedtest::ScratchDir scratch("malformed-elf-empty");
    const std::string path = seedtest::malformed::WriteScratch(scratch, "empty.elf", Bytes{});
    const auto result = ListOne(path);
    REQUIRE(result.errors.count(path) == 1);
    CHECK_FALSE(result.errors.at(path).empty());
    CHECK(result.errors.at(path).find("internal error (") == std::string::npos);
    CHECK(result.dependencies.empty());
}

TEST_CASE("edge: ELF truncated inside the ELF header", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        const ElfImage image = MakeImage(is64);
        RequireListRejected(seedtest::malformed::Truncate(image.bytes, is64 ? 40 : 30), "ELF header");
    });
}

TEST_CASE("edge: ELF truncated to the identification bytes", "[MalformedElf][edge]")
{
    // Four bytes are enough to be recognized as ELF and too few for a header.
    const ElfImage image = MakeImage(true);
    RequireListRejected(seedtest::malformed::Truncate(image.bytes, 4), "ELF header");
}

TEST_CASE("edge: ELF truncated inside the program header table", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        const ElfImage image = MakeImage(is64);
        RequireListRejected(seedtest::malformed::Truncate(image.bytes, image.PhdrOffset(1) + 10),
                            "program header table");
    });
}

TEST_CASE("edge: ELF truncated inside the dynamic table", "[MalformedElf][edge]")
{
    // The load segment covers the whole file, so it no longer fits.
    ForBothWidths([](bool is64) {
        const ElfImage image = MakeImage(is64);
        RequireListRejected(seedtest::malformed::Truncate(image.bytes, image.dyn_offset + image.dyn_size + 4),
                            "PT_LOAD");
    });
}

TEST_CASE("edge: ELF truncated inside a name", "[MalformedElf][edge]")
{
    ForBothWidths([](bool is64) {
        const ElfImage image = MakeImage(is64);
        RequireListRejected(seedtest::malformed::Truncate(image.bytes, image.str_offset + 3), "PT_LOAD");
    });
}
