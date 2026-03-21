#pragma once

#include <cstdint>

inline constexpr std::uint32_t MH_MAGIC    = 0xFEEDFACE;
inline constexpr std::uint32_t MH_CIGAM    = 0xCEFAEDFE;
inline constexpr std::uint32_t MH_MAGIC_64 = 0xFEEDFACF;
inline constexpr std::uint32_t MH_CIGAM_64 = 0xCFFAEDFE;
inline constexpr std::uint32_t FAT_MAGIC   = 0xCAFEBABE;
inline constexpr std::uint32_t FAT_CIGAM   = 0xBEBAFECA;

#pragma pack(push, 1)
struct MachHeader32
{
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
};

struct MachHeader64
{
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
    std::uint32_t reserved;
};

struct LoadCommand
{
    std::uint32_t cmd;
    std::uint32_t cmdsize;
};

struct FatHeader
{
    std::uint32_t magic;
    std::uint32_t nfat_arch;
};

struct FatArch
{
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t align;
};
#pragma pack(pop)
