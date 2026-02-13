#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint8_t EI_MAG0 = 0;
constexpr std::uint8_t EI_MAG1 = 1;
constexpr std::uint8_t EI_MAG2 = 2;
constexpr std::uint8_t EI_MAG3 = 3;
constexpr std::uint8_t EI_CLASS = 4;
constexpr std::uint8_t EI_DATA = 5;
constexpr std::uint8_t EI_NIDENT = 16;

constexpr std::uint8_t ELFMAG0 = 0x7F;
constexpr std::uint8_t ELFMAG1 = 'E';
constexpr std::uint8_t ELFMAG2 = 'L';
constexpr std::uint8_t ELFMAG3 = 'F';

constexpr std::uint8_t ELFCLASS32 = 1;
constexpr std::uint8_t ELFCLASS64 = 2;
constexpr std::uint8_t ELFDATA2LSB = 1;
constexpr std::uint8_t ELFDATA2MSB = 2;

constexpr std::uint32_t PT_LOAD = 1;
constexpr std::uint32_t PT_DYNAMIC = 2;

constexpr std::int64_t DT_NULL = 0;
constexpr std::int64_t DT_NEEDED = 1;
constexpr std::int64_t DT_STRTAB = 5;

#pragma pack(push, 1)
struct Elf32_Ehdr
{
    std::uint8_t e_ident[EI_NIDENT];
    std::uint16_t e_type;
    std::uint16_t e_machine;
    std::uint32_t e_version;
    std::uint32_t e_entry;
    std::uint32_t e_phoff;
    std::uint32_t e_shoff;
    std::uint32_t e_flags;
    std::uint16_t e_ehsize;
    std::uint16_t e_phentsize;
    std::uint16_t e_phnum;
    std::uint16_t e_shentsize;
    std::uint16_t e_shnum;
    std::uint16_t e_shstrndx;
};

struct Elf64_Ehdr
{
    std::uint8_t e_ident[EI_NIDENT];
    std::uint16_t e_type;
    std::uint16_t e_machine;
    std::uint32_t e_version;
    std::uint64_t e_entry;
    std::uint64_t e_phoff;
    std::uint64_t e_shoff;
    std::uint32_t e_flags;
    std::uint16_t e_ehsize;
    std::uint16_t e_phentsize;
    std::uint16_t e_phnum;
    std::uint16_t e_shentsize;
    std::uint16_t e_shnum;
    std::uint16_t e_shstrndx;
};

struct Elf32_Phdr
{
    std::uint32_t p_type;
    std::uint32_t p_offset;
    std::uint32_t p_vaddr;
    std::uint32_t p_paddr;
    std::uint32_t p_filesz;
    std::uint32_t p_memsz;
    std::uint32_t p_flags;
    std::uint32_t p_align;
};

struct Elf64_Phdr
{
    std::uint32_t p_type;
    std::uint32_t p_flags;
    std::uint64_t p_offset;
    std::uint64_t p_vaddr;
    std::uint64_t p_paddr;
    std::uint64_t p_filesz;
    std::uint64_t p_memsz;
    std::uint64_t p_align;
};

struct Elf32_Dyn
{
    std::int32_t d_tag;
    union
    {
        std::uint32_t d_val;
        std::uint32_t d_ptr;
    } d_un;
};

struct Elf64_Dyn
{
    std::int64_t d_tag;
    union
    {
        std::uint64_t d_val;
        std::uint64_t d_ptr;
    } d_un;
};
#pragma pack(pop)

class ElfParser
{
  public:
    static std::vector<std::string> ListDependencies(const std::string &file_path);
};
