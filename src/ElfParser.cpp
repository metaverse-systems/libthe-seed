#include "ElfParser.hpp"

#include "ByteSwap.hpp"
#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using seed::internal::ByteOrder;
using seed::internal::ByteSpan;
using seed::internal::CheckedAdd;
using seed::internal::CheckedMultiply;
using seed::internal::RangeFits;
using seed::internal::ThrowMalformed;

constexpr const char *kFormat = "ELF";
constexpr std::uint64_t kMaxNameLength = 4096;

// A PT_LOAD segment whose file data has been checked to lie inside the file.
struct LoadSegment
{
    std::uint64_t vaddr;
    std::uint64_t filesz;
    std::uint64_t offset;
};

// Maps a virtual address to a file offset through the file-backed part of a
// segment only. Returns the segment too, so the caller can bound the data.
const LoadSegment *FindFileBackedSegment(std::uint64_t virtual_address, const std::vector<LoadSegment> &segments)
{
    for(const auto &segment : segments)
    {
        if(virtual_address >= segment.vaddr && virtual_address - segment.vaddr < segment.filesz)
        {
            return &segment;
        }
    }

    return nullptr;
}

// Shared by the 32-bit and 64-bit layouts; the types differ only in field width.
template <typename Ehdr, typename Phdr, typename Dyn>
std::vector<std::string> ParseElf(const ByteSpan &file, bool file_is_little_endian)
{
    const ByteOrder order = file_is_little_endian ? ByteOrder::Little : ByteOrder::Big;

    const auto header = file.Read<Ehdr>(0, order, "ELF header");

    const std::uint64_t phoff = ByteSwapIfNeeded(header.e_phoff, file_is_little_endian);
    const std::uint16_t phentsize = ByteSwapIfNeeded(header.e_phentsize, file_is_little_endian);
    const std::uint16_t phnum = ByteSwapIfNeeded(header.e_phnum, file_is_little_endian);

    if(phentsize < sizeof(Phdr))
    {
        ThrowMalformed(kFormat, "program header entry size " + std::to_string(phentsize) +
                                    " is smaller than the " + std::to_string(sizeof(Phdr)) +
                                    " bytes of a program header");
    }

    // The whole table must fit before any entry is read.
    const std::uint64_t table_size =
        CheckedMultiply(phnum, phentsize, kFormat, "program header table size");
    const ByteSpan table = file.Sub(phoff, table_size, "program header table");

    std::optional<std::uint64_t> dynamic_offset;
    std::uint64_t dynamic_size = 0;
    std::vector<LoadSegment> load_segments;

    for(std::uint16_t index = 0; index < phnum; ++index)
    {
        const auto phdr =
            table.Read<Phdr>(static_cast<std::uint64_t>(index) * phentsize, order, "program header");

        const std::uint32_t p_type = ByteSwapIfNeeded(phdr.p_type, file_is_little_endian);
        const std::uint64_t p_offset = ByteSwapIfNeeded(phdr.p_offset, file_is_little_endian);
        const std::uint64_t p_vaddr = ByteSwapIfNeeded(phdr.p_vaddr, file_is_little_endian);
        const std::uint64_t p_memsz = ByteSwapIfNeeded(phdr.p_memsz, file_is_little_endian);
        const std::uint64_t p_filesz = ByteSwapIfNeeded(phdr.p_filesz, file_is_little_endian);

        if(p_type == PT_LOAD)
        {
            if(!RangeFits(p_offset, p_filesz, file.Size()))
            {
                ThrowMalformed(kFormat, "PT_LOAD segment " + std::to_string(index) + " file data (offset " +
                                            std::to_string(p_offset) + ", size " + std::to_string(p_filesz) +
                                            ") extends past the end of the file (" +
                                            std::to_string(file.Size()) + " bytes)");
            }
            CheckedAdd(p_vaddr, p_memsz, kFormat,
                       "PT_LOAD segment " + std::to_string(index) + " virtual address range");
            load_segments.push_back({p_vaddr, p_filesz, p_offset});
        }

        if(p_type == PT_DYNAMIC)
        {
            dynamic_offset = p_offset;
            dynamic_size = p_filesz;
        }
    }

    // No dynamic section: nothing to list (a static program).
    if(!dynamic_offset.has_value() || dynamic_size == 0)
    {
        return {};
    }

    const ByteSpan dynamic = file.Sub(*dynamic_offset, dynamic_size, "PT_DYNAMIC segment");

    std::vector<std::uint64_t> needed_offsets;
    std::optional<std::uint64_t> string_table_va;
    std::optional<std::uint64_t> string_table_size;

    const std::uint64_t entry_count = dynamic.Size() / sizeof(Dyn);
    for(std::uint64_t index = 0; index < entry_count; ++index)
    {
        const auto dyn = dynamic.Read<Dyn>(index * sizeof(Dyn), order, "dynamic entry");
        const auto tag = ByteSwapIfNeeded(dyn.d_tag, file_is_little_endian);

        if(tag == DT_NULL)
        {
            break;
        }

        if(tag == DT_NEEDED)
        {
            needed_offsets.push_back(ByteSwapIfNeeded(dyn.d_un.d_val, file_is_little_endian));
        }
        else if(tag == DT_STRTAB)
        {
            string_table_va = ByteSwapIfNeeded(dyn.d_un.d_ptr, file_is_little_endian);
        }
        else if(tag == DT_STRSZ)
        {
            string_table_size = ByteSwapIfNeeded(dyn.d_un.d_val, file_is_little_endian);
        }
    }

    if(!string_table_va.has_value())
    {
        ThrowMalformed(kFormat, "DT_STRTAB is missing from the dynamic section");
    }

    const LoadSegment *segment = FindFileBackedSegment(*string_table_va, load_segments);
    if(segment == nullptr)
    {
        ThrowMalformed(kFormat, "DT_STRTAB address " + std::to_string(*string_table_va) +
                                    " is not inside the file data of any PT_LOAD segment");
    }

    // The string table runs to the end of the segment's file data, or to
    // DT_STRSZ when that is smaller.
    const std::uint64_t within_segment = *string_table_va - segment->vaddr;
    std::uint64_t string_table_length = segment->filesz - within_segment;
    if(string_table_size.has_value() && *string_table_size < string_table_length)
    {
        string_table_length = *string_table_size;
    }
    const ByteSpan string_table =
        file.Sub(segment->offset + within_segment, string_table_length, "string table");

    std::vector<std::string> dependencies;
    dependencies.reserve(needed_offsets.size());
    std::uint64_t total_name_bytes = 0;

    for(const auto needed_offset : needed_offsets)
    {
        dependencies.push_back(string_table.CString(needed_offset, kMaxNameLength, "DT_NEEDED name"));
        total_name_bytes += dependencies.back().size();
        if(total_name_bytes > file.Size())
        {
            ThrowMalformed(kFormat, "DT_NEEDED names total " + std::to_string(total_name_bytes) +
                                        " bytes, more than the file (" + std::to_string(file.Size()) +
                                        " bytes)");
        }
    }

    return dependencies;
}
} // namespace

std::vector<std::string> ElfParser::ListDependencies(const std::string &file_path)
{
    return seed::internal::GuardEntryPoint(kFormat, [&] {
        const auto bytes = ReadFileBytes(file_path);
        const ByteSpan file(bytes, kFormat);

        if(bytes.size() < EI_NIDENT)
        {
            ThrowMalformed(kFormat, "ELF header is too small (" + std::to_string(bytes.size()) +
                                        " bytes, " + std::to_string(EI_NIDENT) + " needed)");
        }

        if(bytes[EI_MAG0] != ELFMAG0 || bytes[EI_MAG1] != ELFMAG1 || bytes[EI_MAG2] != ELFMAG2 ||
           bytes[EI_MAG3] != ELFMAG3)
        {
            ThrowMalformed(kFormat, "ELF header magic is invalid");
        }

        const std::uint8_t elf_class = bytes[EI_CLASS];
        const std::uint8_t elf_data = bytes[EI_DATA];

        if(elf_data != ELFDATA2LSB && elf_data != ELFDATA2MSB)
        {
            ThrowMalformed(kFormat, "ELF header endianness " + std::to_string(elf_data) + " is not supported");
        }

        const bool file_is_little_endian = elf_data == ELFDATA2LSB;

        if(elf_class == ELFCLASS32)
        {
            return ParseElf<Elf32_Ehdr, Elf32_Phdr, Elf32_Dyn>(file, file_is_little_endian);
        }

        if(elf_class == ELFCLASS64)
        {
            return ParseElf<Elf64_Ehdr, Elf64_Phdr, Elf64_Dyn>(file, file_is_little_endian);
        }

        ThrowMalformed(kFormat, "ELF header class " + std::to_string(elf_class) + " is not supported");
    });
}
