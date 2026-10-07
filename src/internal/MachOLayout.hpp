#pragma once

// The layout of a Mac program, read once from its bytes.
//
// A file is classified by its first four bytes as written, not by a host
// integer, and every field is read with an explicit byte order (little for a
// program, big for a universal file's table), so the result does not depend on
// the host. Every read goes through BoundedBytes, so a damaged file is
// rejected with a std::runtime_error whose message starts with "Mach-O: " and
// nothing is read outside the file.
//
// Only 64-bit little-endian programs are interpreted. A big-endian or 32-bit
// program is recognised and reported as declined, without looking past its
// first twelve bytes.

#include "BoundedBytes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seed::internal
{

enum class MachOKind
{
    Thin64Little,
    Thin32Little,
    Thin64Big,
    Thin32Big,
    Fat32,
    Fat64,
    Unknown
};

// Classifies the first four bytes. Unknown for anything else, including a file
// shorter than four bytes. A universal magic is only a candidate: it is also
// the magic of a Java class file, which ParseMachOContainer tells apart.
MachOKind ClassifyMachOMagic(const ByteSpan &file);

enum class SliceSupport
{
    Supported,
    BigEndian,
    ThirtyTwoBit,
    ThirtyTwoBitBigEndian
};

// "big-endian", "32-bit" or "32-bit big-endian"; empty for Supported.
std::string DeclineKind(SliceSupport support);

// "<kind> Mac programs are not supported (supported: 64-bit little-endian arm64
// and x86-64, alone or in a universal file)"; callers put the path in front.
std::string DeclineText(SliceSupport support);

// "arm64", "x86_64", or "cpu 0x%X" of the CPU type for any other.
std::string ArchName(std::uint32_t cputype);

struct LoadCommandEntry
{
    std::uint64_t offset; // within the slice
    std::uint32_t cmd;
    std::uint32_t cmdsize;
};

struct SegmentLayout
{
    std::uint64_t command_offset; // within the slice
    std::string name;
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
};

struct CodeSignatureLayout
{
    std::uint64_t command_offset; // within the slice
    std::uint32_t dataoff;
    std::uint32_t datasize;
};

// One program, parsed once. For a declined slice only base, size, cputype,
// cpusubtype and support are set.
struct SliceLayout
{
    std::uint64_t base = 0; // offset of the slice in the file
    std::uint64_t size = 0;
    SliceSupport support = SliceSupport::Supported;
    std::uint32_t cputype = 0;
    std::uint32_t cpusubtype = 0;
    std::uint32_t filetype = 0;
    std::uint32_t ncmds = 0;
    std::uint32_t sizeofcmds = 0;
    std::uint64_t header_end = 0; // header size + sizeofcmds
    std::vector<LoadCommandEntry> commands;
    std::vector<SegmentLayout> segments;
    std::optional<SegmentLayout> text;
    std::optional<SegmentLayout> linkedit;
    // Lowest file offset of a section with data (not zero-fill) or of a segment
    // other than __TEXT that has data; the slice size when there is none.
    std::uint64_t first_content = 0;
    std::optional<CodeSignatureLayout> codesig;
    std::vector<std::string> dylibs; // names of the five reference kinds, file order, each once

    // Bytes between the end of the load commands and the first content.
    std::uint64_t FreeHeaderSpace() const
    {
        return this->first_content > this->header_end ? this->first_content - this->header_end : 0;
    }
};

enum class ContainerForm
{
    Thin,
    Fat32,
    Fat64
};

struct ContainerEntry
{
    std::uint32_t cputype = 0;
    std::uint32_t cpusubtype = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t align = 0; // log2
    SliceLayout slice;
};

struct MachOContainer
{
    ContainerForm form = ContainerForm::Thin;
    std::vector<ContainerEntry> entries; // table order; a thin file has one entry at offset 0
};

// The largest slice count a universal table may claim.
inline constexpr std::uint64_t kMaxFatArchCount = 256;

// Parses the program that occupies [base, base + size) of the file. `scope`
// names the range in messages (for example "fat slice 1").
SliceLayout ParseMachOSlice(const ByteSpan &file, std::uint64_t base, std::uint64_t size,
                            std::string_view scope = "the file");

// Parses a thin or universal file. Rejects a file that is not a Mach-O file,
// including a Java class file (same first four bytes as a universal file), a
// table that does not fit, a slice outside the file, inside the table or
// overlapping another, and any damaged slice.
MachOContainer ParseMachOContainer(const ByteSpan &file);

} // namespace seed::internal
