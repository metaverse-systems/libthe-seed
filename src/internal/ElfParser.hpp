#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace the_seed::internal
{

namespace elf
{

constexpr std::uint8_t kEiMag0 = 0;
constexpr std::uint8_t kEiMag1 = 1;
constexpr std::uint8_t kEiMag2 = 2;
constexpr std::uint8_t kEiMag3 = 3;
constexpr std::uint8_t kEiClass = 4;
constexpr std::uint8_t kEiData = 5;
constexpr std::uint8_t kEiNident = 16;

constexpr std::uint8_t kElfMag0 = 0x7F;
constexpr std::uint8_t kElfMag1 = 'E';
constexpr std::uint8_t kElfMag2 = 'L';
constexpr std::uint8_t kElfMag3 = 'F';

constexpr std::uint8_t kElfClass32 = 1;
constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kElfData2Lsb = 1;
constexpr std::uint8_t kElfData2Msb = 2;

constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;

constexpr std::int64_t kDtNull = 0;
constexpr std::int64_t kDtNeeded = 1;
constexpr std::int64_t kDtStrtab = 5;
constexpr std::int64_t kDtStrsz = 10;

#pragma pack(push, 1)
struct Elf32_Ehdr
{
    std::uint8_t e_ident[kEiNident];
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
    std::uint8_t e_ident[kEiNident];
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

} // namespace elf

class ElfParser
{
  public:
    static std::vector<std::string> ListDependencies(const std::string &file_path);

    // The same listing for a file already held in memory.
    static std::vector<std::string> ListDependenciesFromBytes(const std::vector<std::uint8_t> &bytes);
};

} // namespace the_seed::internal
