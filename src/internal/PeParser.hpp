#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace the_seed::internal
{

namespace pe
{

constexpr std::uint16_t kImageDosSignature = 0x5A4D;
constexpr std::uint32_t kImageNtSignature = 0x00004550;
constexpr std::uint16_t kImageNtOptionalHdr32Magic = 0x10B;
constexpr std::uint16_t kImageNtOptionalHdr64Magic = 0x20B;

#pragma pack(push, 1)
struct ImageDosHeader
{
    std::uint16_t e_magic;
    std::uint16_t e_cblp;
    std::uint16_t e_cp;
    std::uint16_t e_crlc;
    std::uint16_t e_cparhdr;
    std::uint16_t e_minalloc;
    std::uint16_t e_maxalloc;
    std::uint16_t e_ss;
    std::uint16_t e_sp;
    std::uint16_t e_csum;
    std::uint16_t e_ip;
    std::uint16_t e_cs;
    std::uint16_t e_lfarlc;
    std::uint16_t e_ovno;
    std::uint16_t e_res[4];
    std::uint16_t e_oemid;
    std::uint16_t e_oeminfo;
    std::uint16_t e_res2[10];
    std::int32_t e_lfanew;
};

struct ImageFileHeader
{
    std::uint16_t Machine;
    std::uint16_t NumberOfSections;
    std::uint32_t TimeDateStamp;
    std::uint32_t PointerToSymbolTable;
    std::uint32_t NumberOfSymbols;
    std::uint16_t SizeOfOptionalHeader;
    std::uint16_t Characteristics;
};

struct ImageDataDirectory
{
    std::uint32_t VirtualAddress;
    std::uint32_t Size;
};

struct ImageOptionalHeader32
{
    std::uint16_t Magic;
    std::uint8_t MajorLinkerVersion;
    std::uint8_t MinorLinkerVersion;
    std::uint32_t SizeOfCode;
    std::uint32_t SizeOfInitializedData;
    std::uint32_t SizeOfUninitializedData;
    std::uint32_t AddressOfEntryPoint;
    std::uint32_t BaseOfCode;
    std::uint32_t BaseOfData;
    std::uint32_t ImageBase;
    std::uint32_t SectionAlignment;
    std::uint32_t FileAlignment;
    std::uint16_t MajorOperatingSystemVersion;
    std::uint16_t MinorOperatingSystemVersion;
    std::uint16_t MajorImageVersion;
    std::uint16_t MinorImageVersion;
    std::uint16_t MajorSubsystemVersion;
    std::uint16_t MinorSubsystemVersion;
    std::uint32_t Win32VersionValue;
    std::uint32_t SizeOfImage;
    std::uint32_t SizeOfHeaders;
    std::uint32_t CheckSum;
    std::uint16_t Subsystem;
    std::uint16_t DllCharacteristics;
    std::uint32_t SizeOfStackReserve;
    std::uint32_t SizeOfStackCommit;
    std::uint32_t SizeOfHeapReserve;
    std::uint32_t SizeOfHeapCommit;
    std::uint32_t LoaderFlags;
    std::uint32_t NumberOfRvaAndSizes;
};

struct ImageOptionalHeader64
{
    std::uint16_t Magic;
    std::uint8_t MajorLinkerVersion;
    std::uint8_t MinorLinkerVersion;
    std::uint32_t SizeOfCode;
    std::uint32_t SizeOfInitializedData;
    std::uint32_t SizeOfUninitializedData;
    std::uint32_t AddressOfEntryPoint;
    std::uint32_t BaseOfCode;
    std::uint64_t ImageBase;
    std::uint32_t SectionAlignment;
    std::uint32_t FileAlignment;
    std::uint16_t MajorOperatingSystemVersion;
    std::uint16_t MinorOperatingSystemVersion;
    std::uint16_t MajorImageVersion;
    std::uint16_t MinorImageVersion;
    std::uint16_t MajorSubsystemVersion;
    std::uint16_t MinorSubsystemVersion;
    std::uint32_t Win32VersionValue;
    std::uint32_t SizeOfImage;
    std::uint32_t SizeOfHeaders;
    std::uint32_t CheckSum;
    std::uint16_t Subsystem;
    std::uint16_t DllCharacteristics;
    std::uint64_t SizeOfStackReserve;
    std::uint64_t SizeOfStackCommit;
    std::uint64_t SizeOfHeapReserve;
    std::uint64_t SizeOfHeapCommit;
    std::uint32_t LoaderFlags;
    std::uint32_t NumberOfRvaAndSizes;
};

struct ImageSectionHeader
{
    std::uint8_t Name[8];
    union
    {
        std::uint32_t PhysicalAddress;
        std::uint32_t VirtualSize;
    } Misc;
    std::uint32_t VirtualAddress;
    std::uint32_t SizeOfRawData;
    std::uint32_t PointerToRawData;
    std::uint32_t PointerToRelocations;
    std::uint32_t PointerToLinenumbers;
    std::uint16_t NumberOfRelocations;
    std::uint16_t NumberOfLinenumbers;
    std::uint32_t Characteristics;
};

struct ImageImportDescriptor
{
    std::uint32_t OriginalFirstThunk;
    std::uint32_t TimeDateStamp;
    std::uint32_t ForwarderChain;
    std::uint32_t Name;
    std::uint32_t FirstThunk;
};
#pragma pack(pop)

} // namespace pe

class PeParser
{
  public:
    static std::vector<std::string> ListDependencies(const std::string &file_path);

    // The same listing for a file already held in memory.
    static std::vector<std::string> ListDependenciesFromBytes(const std::vector<std::uint8_t> &bytes);
};

} // namespace the_seed::internal
