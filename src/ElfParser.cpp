#include "ElfParser.hpp"

#include "ByteSwap.hpp"

#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
struct LoadSegment
{
    std::uint64_t vaddr;
    std::uint64_t memsz;
    std::uint64_t offset;
};

template <typename T>
T ReadStruct(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset + sizeof(T) > bytes.size())
    {
        throw std::runtime_error("Truncated ELF file");
    }

    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path)
{
    std::ifstream input(file_path, std::ios::binary);
    if(!input.is_open())
    {
        throw std::runtime_error("Unable to open file");
    }

    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);

    if(size < 0)
    {
        throw std::runtime_error("Unable to read file size");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(size > 0)
    {
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        if(!input)
        {
            throw std::runtime_error("Unable to read file");
        }
    }

    return bytes;
}

std::optional<std::uint64_t> VirtualAddressToOffset(
    std::uint64_t virtual_address,
    const std::vector<LoadSegment> &segments
)
{
    for(const auto &segment : segments)
    {
        if(virtual_address >= segment.vaddr && virtual_address < segment.vaddr + segment.memsz)
        {
            return segment.offset + (virtual_address - segment.vaddr);
        }
    }

    return std::nullopt;
}

std::string ReadCString(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset >= bytes.size())
    {
        throw std::runtime_error("Invalid string table offset in ELF");
    }

    std::size_t end = offset;
    while(end < bytes.size() && bytes[end] != 0)
    {
        ++end;
    }

    if(end == bytes.size())
    {
        throw std::runtime_error("Unterminated dependency string in ELF");
    }

    return std::string(
        reinterpret_cast<const char *>(bytes.data() + offset),
        reinterpret_cast<const char *>(bytes.data() + end)
    );
}

std::vector<std::string> ParseElf32(const std::vector<std::uint8_t> &bytes, bool file_is_little_endian)
{
    const auto header = ReadStruct<Elf32_Ehdr>(bytes, 0);

    const std::uint64_t phoff = ByteSwapIfNeeded(header.e_phoff, file_is_little_endian);
    const std::uint16_t phentsize = ByteSwapIfNeeded(header.e_phentsize, file_is_little_endian);
    const std::uint16_t phnum = ByteSwapIfNeeded(header.e_phnum, file_is_little_endian);

    if(phentsize < sizeof(Elf32_Phdr))
    {
        throw std::runtime_error("Invalid ELF program header entry size");
    }

    std::optional<std::uint64_t> dynamic_offset;
    std::uint64_t dynamic_size = 0;
    std::vector<LoadSegment> load_segments;

    for(std::uint16_t index = 0; index < phnum; ++index)
    {
        const std::uint64_t entry_offset = phoff + static_cast<std::uint64_t>(index) * phentsize;
        const auto phdr = ReadStruct<Elf32_Phdr>(bytes, static_cast<std::size_t>(entry_offset));

        const std::uint32_t p_type = ByteSwapIfNeeded(phdr.p_type, file_is_little_endian);
        const std::uint32_t p_offset = ByteSwapIfNeeded(phdr.p_offset, file_is_little_endian);
        const std::uint32_t p_vaddr = ByteSwapIfNeeded(phdr.p_vaddr, file_is_little_endian);
        const std::uint32_t p_memsz = ByteSwapIfNeeded(phdr.p_memsz, file_is_little_endian);
        const std::uint32_t p_filesz = ByteSwapIfNeeded(phdr.p_filesz, file_is_little_endian);

        if(p_type == PT_LOAD)
        {
            load_segments.push_back({p_vaddr, p_memsz, p_offset});
        }

        if(p_type == PT_DYNAMIC)
        {
            dynamic_offset = p_offset;
            dynamic_size = p_filesz;
        }
    }

    if(!dynamic_offset.has_value() || dynamic_size == 0)
    {
        return {};
    }

    std::vector<std::uint64_t> needed_offsets;
    std::optional<std::uint64_t> string_table_va;

    for(std::uint64_t offset = *dynamic_offset; offset + sizeof(Elf32_Dyn) <= *dynamic_offset + dynamic_size;
        offset += sizeof(Elf32_Dyn))
    {
        const auto dyn = ReadStruct<Elf32_Dyn>(bytes, static_cast<std::size_t>(offset));
        const std::int32_t tag = ByteSwapIfNeeded(dyn.d_tag, file_is_little_endian);

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
    }

    if(!string_table_va.has_value())
    {
        throw std::runtime_error("Missing DT_STRTAB in ELF dynamic section");
    }

    const auto string_table_offset = VirtualAddressToOffset(*string_table_va, load_segments);
    if(!string_table_offset.has_value())
    {
        throw std::runtime_error("Unable to resolve ELF string table offset");
    }

    std::vector<std::string> dependencies;
    dependencies.reserve(needed_offsets.size());

    for(const auto needed_offset : needed_offsets)
    {
        dependencies.push_back(ReadCString(bytes, static_cast<std::size_t>(*string_table_offset + needed_offset)));
    }

    return dependencies;
}

std::vector<std::string> ParseElf64(const std::vector<std::uint8_t> &bytes, bool file_is_little_endian)
{
    const auto header = ReadStruct<Elf64_Ehdr>(bytes, 0);

    const std::uint64_t phoff = ByteSwapIfNeeded(header.e_phoff, file_is_little_endian);
    const std::uint16_t phentsize = ByteSwapIfNeeded(header.e_phentsize, file_is_little_endian);
    const std::uint16_t phnum = ByteSwapIfNeeded(header.e_phnum, file_is_little_endian);

    if(phentsize < sizeof(Elf64_Phdr))
    {
        throw std::runtime_error("Invalid ELF program header entry size");
    }

    std::optional<std::uint64_t> dynamic_offset;
    std::uint64_t dynamic_size = 0;
    std::vector<LoadSegment> load_segments;

    for(std::uint16_t index = 0; index < phnum; ++index)
    {
        const std::uint64_t entry_offset = phoff + static_cast<std::uint64_t>(index) * phentsize;
        const auto phdr = ReadStruct<Elf64_Phdr>(bytes, static_cast<std::size_t>(entry_offset));

        const std::uint32_t p_type = ByteSwapIfNeeded(phdr.p_type, file_is_little_endian);
        const std::uint64_t p_offset = ByteSwapIfNeeded(phdr.p_offset, file_is_little_endian);
        const std::uint64_t p_vaddr = ByteSwapIfNeeded(phdr.p_vaddr, file_is_little_endian);
        const std::uint64_t p_memsz = ByteSwapIfNeeded(phdr.p_memsz, file_is_little_endian);
        const std::uint64_t p_filesz = ByteSwapIfNeeded(phdr.p_filesz, file_is_little_endian);

        if(p_type == PT_LOAD)
        {
            load_segments.push_back({p_vaddr, p_memsz, p_offset});
        }

        if(p_type == PT_DYNAMIC)
        {
            dynamic_offset = p_offset;
            dynamic_size = p_filesz;
        }
    }

    if(!dynamic_offset.has_value() || dynamic_size == 0)
    {
        return {};
    }

    std::vector<std::uint64_t> needed_offsets;
    std::optional<std::uint64_t> string_table_va;

    for(std::uint64_t offset = *dynamic_offset; offset + sizeof(Elf64_Dyn) <= *dynamic_offset + dynamic_size;
        offset += sizeof(Elf64_Dyn))
    {
        const auto dyn = ReadStruct<Elf64_Dyn>(bytes, static_cast<std::size_t>(offset));
        const std::int64_t tag = ByteSwapIfNeeded(dyn.d_tag, file_is_little_endian);

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
    }

    if(!string_table_va.has_value())
    {
        throw std::runtime_error("Missing DT_STRTAB in ELF dynamic section");
    }

    const auto string_table_offset = VirtualAddressToOffset(*string_table_va, load_segments);
    if(!string_table_offset.has_value())
    {
        throw std::runtime_error("Unable to resolve ELF string table offset");
    }

    std::vector<std::string> dependencies;
    dependencies.reserve(needed_offsets.size());

    for(const auto needed_offset : needed_offsets)
    {
        dependencies.push_back(ReadCString(bytes, static_cast<std::size_t>(*string_table_offset + needed_offset)));
    }

    return dependencies;
}
} // namespace

std::vector<std::string> ElfParser::ListDependencies(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    if(bytes.size() < EI_NIDENT)
    {
        throw std::runtime_error("File is too small to be an ELF binary");
    }

    if(bytes[EI_MAG0] != ELFMAG0 || bytes[EI_MAG1] != ELFMAG1 || bytes[EI_MAG2] != ELFMAG2 ||
       bytes[EI_MAG3] != ELFMAG3)
    {
        throw std::runtime_error("Invalid ELF magic");
    }

    const std::uint8_t elf_class = bytes[EI_CLASS];
    const std::uint8_t elf_data = bytes[EI_DATA];

    if(elf_data != ELFDATA2LSB && elf_data != ELFDATA2MSB)
    {
        throw std::runtime_error("Unsupported ELF endianness");
    }

    const bool file_is_little_endian = elf_data == ELFDATA2LSB;

    if(elf_class == ELFCLASS32)
    {
        return ParseElf32(bytes, file_is_little_endian);
    }

    if(elf_class == ELFCLASS64)
    {
        return ParseElf64(bytes, file_is_little_endian);
    }

    throw std::runtime_error("Unsupported ELF class");
}
