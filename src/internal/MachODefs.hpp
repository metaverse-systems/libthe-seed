#pragma once

#include <cstdint>

inline constexpr std::uint32_t MH_MAGIC    = 0xFEEDFACE;
inline constexpr std::uint32_t MH_CIGAM    = 0xCEFAEDFE;
inline constexpr std::uint32_t MH_MAGIC_64 = 0xFEEDFACF;
inline constexpr std::uint32_t MH_CIGAM_64 = 0xCFFAEDFE;
inline constexpr std::uint32_t FAT_MAGIC   = 0xCAFEBABE;
inline constexpr std::uint32_t FAT_CIGAM   = 0xBEBAFECA;

// Names by on-disk byte order. A value here is the first four bytes of a file
// read in file order (first byte most significant), so it does not depend on the
// byte order of the host: CF FA ED FE is 0xCFFAEDFE. These are the names the
// layout module uses; the older names above are host-order integers and are
// kept for the code that has not moved to the layout module yet.
namespace seed::internal::macho
{

inline constexpr std::uint32_t kBytesThin64Little = 0xCFFAEDFE; // CF FA ED FE
inline constexpr std::uint32_t kBytesThin32Little = 0xCEFAEDFE; // CE FA ED FE
inline constexpr std::uint32_t kBytesThin64Big    = 0xFEEDFACF; // FE ED FA CF
inline constexpr std::uint32_t kBytesThin32Big    = 0xFEEDFACE; // FE ED FA CE
inline constexpr std::uint32_t kBytesFat32        = 0xCAFEBABE; // CA FE BA BE
inline constexpr std::uint32_t kBytesFat64        = 0xCAFEBABF; // CA FE BA BF

inline constexpr std::uint32_t kLcSegment64      = 0x19;
inline constexpr std::uint32_t kLcCodeSignature  = 0x1D;
inline constexpr std::uint32_t kLcLoadDylib      = 0x0C;
inline constexpr std::uint32_t kLcLoadWeakDylib  = 0x80000018;
inline constexpr std::uint32_t kLcReexportDylib  = 0x8000001F;
inline constexpr std::uint32_t kLcLazyLoadDylib  = 0x20;
inline constexpr std::uint32_t kLcLoadUpwardDylib = 0x80000023;

inline constexpr std::uint32_t kSectionTypeMask          = 0xFF;
inline constexpr std::uint32_t kSectionTypeZeroFill      = 0x01;
inline constexpr std::uint32_t kSectionTypeGbZeroFill    = 0x0C;
inline constexpr std::uint32_t kSectionTypeTlsZeroFill   = 0x12;

inline constexpr std::uint64_t kHeader64Size            = 32;
inline constexpr std::uint64_t kLoadCommandHeaderSize   = 8;
inline constexpr std::uint64_t kSegment64CommandSize    = 72;
inline constexpr std::uint64_t kSection64Size           = 80;
inline constexpr std::uint64_t kDylibCommandMinSize     = 24;
inline constexpr std::uint64_t kCodeSignatureCommandSize = 16;
inline constexpr std::uint64_t kFatArch32Size           = 20;
inline constexpr std::uint64_t kFatArch64Size           = 32;
inline constexpr std::uint64_t kFatHeaderSize           = 8;

} // namespace seed::internal::macho

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

struct FatArch64
{
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint64_t offset;
    std::uint64_t size;
    std::uint32_t align;
    std::uint32_t reserved;
};
#pragma pack(pop)
