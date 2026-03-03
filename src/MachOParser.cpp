#include <libthe-seed/MachOParser.hpp>

#include "ByteSwap.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

constexpr std::uint32_t MH_MAGIC    = 0xFEEDFACE; // Mach-O 32-bit BE
constexpr std::uint32_t MH_CIGAM    = 0xCEFAEDFE; // Mach-O 32-bit LE
constexpr std::uint32_t MH_MAGIC_64 = 0xFEEDFACF; // Mach-O 64-bit BE
constexpr std::uint32_t MH_CIGAM_64 = 0xCFFAEDFE; // Mach-O 64-bit LE
constexpr std::uint32_t FAT_MAGIC   = 0xCAFEBABE; // Fat BE
constexpr std::uint32_t FAT_CIGAM   = 0xBEBAFECA; // Fat LE

constexpr std::uint32_t LC_LOAD_DYLIB = 0x0C;

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

struct DylibCommand
{
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t name_offset;
    std::uint32_t timestamp;
    std::uint32_t current_version;
    std::uint32_t compat_version;
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

std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path)
{
    std::ifstream input(file_path, std::ios::binary);
    if(!input.is_open())
    {
        throw std::runtime_error("Unable to open file: " + file_path);
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
    }
    return bytes;
}

std::uint32_t ReadMagic(const std::vector<std::uint8_t> &bytes)
{
    if(bytes.size() < 4)
    {
        return 0;
    }
    std::uint32_t magic;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    return magic;
}

bool IsBigEndianMachO(std::uint32_t magic)
{
    return magic == MH_MAGIC || magic == MH_MAGIC_64 || magic == FAT_MAGIC;
}

template <typename T>
T ReadField(const std::vector<std::uint8_t> &bytes, std::size_t offset, bool big_endian)
{
    if(offset + sizeof(T) > bytes.size())
    {
        throw std::runtime_error("Truncated Mach-O file");
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return ByteSwapIfNeeded(value, !big_endian); // big_endian file → file_is_little_endian = false
}

std::string ReadCString(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset >= bytes.size())
    {
        return "";
    }
    std::size_t end = offset;
    while(end < bytes.size() && bytes[end] != 0)
    {
        ++end;
    }
    return std::string(
        reinterpret_cast<const char *>(bytes.data() + offset),
        reinterpret_cast<const char *>(bytes.data() + end)
    );
}

} // anonymous namespace

MachOParser::Format MachOParser::DetectFormat(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    if(bytes.size() < 4)
    {
        return Format::NotMachO;
    }

    const auto magic = ReadMagic(bytes);

    switch(magic)
    {
    case MH_MAGIC:
    case MH_CIGAM:
        return Format::MachO32;
    case MH_MAGIC_64:
    case MH_CIGAM_64:
        return Format::MachO64;
    case FAT_MAGIC:
    case FAT_CIGAM:
        return Format::Fat;
    default:
        return Format::NotMachO;
    }
}

bool MachOParser::IsMachO(const std::string &file_path)
{
    return DetectFormat(file_path) != Format::NotMachO;
}

bool MachOParser::IsFatBinary(const std::string &file_path)
{
    return DetectFormat(file_path) == Format::Fat;
}

std::vector<MachOParser::ArchSlice> MachOParser::GetArchSlices(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    if(bytes.size() < 4)
    {
        throw std::runtime_error("File too small to be a Mach-O binary");
    }

    const auto magic = ReadMagic(bytes);

    // Fat binary: read fat_arch entries
    if(magic == FAT_MAGIC || magic == FAT_CIGAM)
    {
        const bool big_endian = (magic == FAT_MAGIC);
        if(bytes.size() < sizeof(FatHeader))
        {
            throw std::runtime_error("Truncated fat header");
        }

        const auto nfat_arch = ReadField<std::uint32_t>(bytes, 4, big_endian);
        std::vector<ArchSlice> slices;
        slices.reserve(nfat_arch);

        for(std::uint32_t i = 0; i < nfat_arch; ++i)
        {
            const std::size_t entry_offset = sizeof(FatHeader) + static_cast<std::size_t>(i) * sizeof(FatArch);
            if(entry_offset + sizeof(FatArch) > bytes.size())
            {
                throw std::runtime_error("Truncated fat_arch entry");
            }

            ArchSlice slice{};
            slice.cpu_type = ReadField<std::uint32_t>(bytes, entry_offset, big_endian);
            slice.cpu_subtype = ReadField<std::uint32_t>(bytes, entry_offset + 4, big_endian);
            slice.offset = ReadField<std::uint32_t>(bytes, entry_offset + 8, big_endian);
            slice.size = ReadField<std::uint32_t>(bytes, entry_offset + 12, big_endian);
            slices.push_back(slice);
        }

        return slices;
    }

    // Single-arch Mach-O
    if(magic == MH_MAGIC || magic == MH_CIGAM ||
       magic == MH_MAGIC_64 || magic == MH_CIGAM_64)
    {
        const bool big_endian = IsBigEndianMachO(magic);
        ArchSlice slice{};
        slice.cpu_type = ReadField<std::uint32_t>(bytes, 4, big_endian);
        slice.cpu_subtype = ReadField<std::uint32_t>(bytes, 8, big_endian);
        slice.offset = 0;
        slice.size = static_cast<std::uint64_t>(bytes.size());
        return { slice };
    }

    throw std::runtime_error("File is not a Mach-O binary");
}

std::vector<std::string> MachOParser::ListDependencies(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    if(bytes.size() < 4)
    {
        throw std::runtime_error("File too small to be a Mach-O binary");
    }

    const auto magic = ReadMagic(bytes);
    bool big_endian = IsBigEndianMachO(magic);
    std::size_t header_size = 0;

    switch(magic)
    {
    case MH_MAGIC:
    case MH_CIGAM:
        header_size = sizeof(MachHeader32);
        break;
    case MH_MAGIC_64:
    case MH_CIGAM_64:
        header_size = sizeof(MachHeader64);
        break;
    default:
        throw std::runtime_error("Not a single-arch Mach-O binary");
    }

    const auto ncmds = ReadField<std::uint32_t>(bytes, 16, big_endian);
    std::vector<std::string> deps;
    std::size_t cmd_offset = header_size;

    for(std::uint32_t i = 0; i < ncmds; ++i)
    {
        if(cmd_offset + sizeof(LoadCommand) > bytes.size())
        {
            break;
        }

        const auto cmd = ReadField<std::uint32_t>(bytes, cmd_offset, big_endian);
        const auto cmdsize = ReadField<std::uint32_t>(bytes, cmd_offset + 4, big_endian);

        if(cmd == LC_LOAD_DYLIB && cmdsize >= sizeof(DylibCommand))
        {
            const auto name_offset = ReadField<std::uint32_t>(bytes, cmd_offset + 8, big_endian);
            if(name_offset < cmdsize)
            {
                deps.push_back(ReadCString(bytes, cmd_offset + name_offset));
            }
        }

        cmd_offset += cmdsize;
    }

    return deps;
}
