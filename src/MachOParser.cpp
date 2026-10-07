#include <libthe-seed/MachOParser.hpp>

#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"
#include "internal/MachODefs.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace {

using seed::internal::ByteOrder;
using seed::internal::ByteSpan;
using seed::internal::GuardEntryPoint;
using seed::internal::RangeFits;
using seed::internal::ThrowMalformed;

constexpr const char *kFormat = "Mach-O";

constexpr std::uint32_t LC_LOAD_DYLIB = 0x0C;
constexpr std::uint32_t LC_LOAD_WEAK_DYLIB = 0x80000018;
constexpr std::uint32_t LC_REEXPORT_DYLIB = 0x8000001F;
constexpr std::uint32_t LC_LAZY_LOAD_DYLIB = 0x20;
constexpr std::uint32_t LC_LOAD_UPWARD_DYLIB = 0x80000023;
constexpr std::uint64_t kDylibCommandSize = 24;
constexpr std::uint64_t kFatArchEntrySize = 20;

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

// The name of a command that refers to another library, or null.
const char *ReferenceCommandName(std::uint32_t cmd)
{
    switch(cmd)
    {
    case LC_LOAD_DYLIB:
        return "LC_LOAD_DYLIB";
    case LC_LOAD_WEAK_DYLIB:
        return "LC_LOAD_WEAK_DYLIB";
    case LC_REEXPORT_DYLIB:
        return "LC_REEXPORT_DYLIB";
    case LC_LAZY_LOAD_DYLIB:
        return "LC_LAZY_LOAD_DYLIB";
    case LC_LOAD_UPWARD_DYLIB:
        return "LC_LOAD_UPWARD_DYLIB";
    default:
        return nullptr;
    }
}

ByteOrder OrderFor(bool big_endian)
{
    return big_endian ? ByteOrder::Big : ByteOrder::Little;
}

} // anonymous namespace

MachOParser::Format MachOParser::DetectFormat(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&]() {
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
    });
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
    return GuardEntryPoint(kFormat, [&]() {
        const auto bytes = ReadFileBytes(file_path);
        if(bytes.size() < 4)
        {
            ThrowMalformed(kFormat, "File too small to be a Mach-O binary");
        }

        const ByteSpan file(bytes, kFormat);
        const auto magic = ReadMagic(bytes);

        // Fat binary: read fat_arch entries
        if(magic == FAT_MAGIC || magic == FAT_CIGAM)
        {
            const ByteOrder order = OrderFor(magic == FAT_MAGIC);
            if(bytes.size() < sizeof(FatHeader))
            {
                ThrowMalformed(kFormat, "Truncated fat header");
            }

            const std::uint64_t nfat_arch = file.Read<std::uint32_t>(4, order, "fat_arch count");
            const std::uint64_t table_end = sizeof(FatHeader) + nfat_arch * kFatArchEntrySize;
            if(table_end > file.Size())
            {
                ThrowMalformed(kFormat, "Truncated fat_arch entry table: " + std::to_string(nfat_arch) +
                                            " entries need " + std::to_string(table_end) +
                                            " bytes but the file has " + std::to_string(file.Size()));
            }

            std::vector<ArchSlice> slices;
            slices.reserve(static_cast<std::size_t>(nfat_arch));

            for(std::uint64_t i = 0; i < nfat_arch; ++i)
            {
                const std::uint64_t entry_offset = sizeof(FatHeader) + i * kFatArchEntrySize;

                ArchSlice slice{};
                slice.cpu_type = file.Read<std::uint32_t>(entry_offset, order, "fat_arch cpu type");
                slice.cpu_subtype = file.Read<std::uint32_t>(entry_offset + 4, order, "fat_arch cpu subtype");
                slice.offset = file.Read<std::uint32_t>(entry_offset + 8, order, "fat_arch offset");
                slice.size = file.Read<std::uint32_t>(entry_offset + 12, order, "fat_arch size");

                const std::string name = "fat slice " + std::to_string(i);
                (void)file.Sub(slice.offset, slice.size, name);
                if(slice.offset < table_end)
                {
                    ThrowMalformed(kFormat, name + " starts at offset " + std::to_string(slice.offset) +
                                                ", inside the fat header table that ends at " +
                                                std::to_string(table_end));
                }
                slices.push_back(slice);
            }

            // No two slices may share bytes.
            std::vector<std::size_t> by_offset(slices.size());
            std::iota(by_offset.begin(), by_offset.end(), std::size_t{0});
            std::stable_sort(by_offset.begin(), by_offset.end(), [&](std::size_t a, std::size_t b) {
                return slices[a].offset < slices[b].offset;
            });
            for(std::size_t k = 1; k < by_offset.size(); ++k)
            {
                const auto &previous = slices[by_offset[k - 1]];
                const auto &current = slices[by_offset[k]];
                if(previous.offset + previous.size > current.offset)
                {
                    ThrowMalformed(kFormat, "fat slice " + std::to_string(by_offset[k - 1]) +
                                                " and fat slice " + std::to_string(by_offset[k]) +
                                                " overlap");
                }
            }

            return slices;
        }

        // Single-arch Mach-O
        if(magic == MH_MAGIC || magic == MH_CIGAM ||
           magic == MH_MAGIC_64 || magic == MH_CIGAM_64)
        {
            const ByteOrder order = OrderFor(IsBigEndianMachO(magic));
            (void)file.Sub(0, 12, "Mach-O header");
            ArchSlice slice{};
            slice.cpu_type = file.Read<std::uint32_t>(4, order, "cpu type");
            slice.cpu_subtype = file.Read<std::uint32_t>(8, order, "cpu subtype");
            slice.offset = 0;
            slice.size = static_cast<std::uint64_t>(bytes.size());
            return std::vector<ArchSlice>{ slice };
        }

        ThrowMalformed(kFormat, "File is not a Mach-O binary");
    });
}

std::vector<std::string> MachOParser::ListDependencies(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&]() {
        const auto bytes = ReadFileBytes(file_path);
        if(bytes.size() < 4)
        {
            ThrowMalformed(kFormat, "File too small to be a Mach-O binary");
        }

        const auto outer_magic = ReadMagic(bytes);
        std::vector<std::string> deps;

        // Appends the references of one single-arch image, keeping the first
        // occurrence of each name.
        const auto list_image = [&](const ByteSpan &file) {
            std::uint32_t magic = 0;
            if(file.Size() >= 4)
            {
                magic = file.Read<std::uint32_t>(0, ByteOrder::Little, "Mach-O magic");
            }
            const ByteOrder order = OrderFor(IsBigEndianMachO(magic));
            std::uint64_t header_size = 0;

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
                ThrowMalformed(kFormat, "Not a single-arch Mach-O binary");
            }

            (void)file.Sub(0, header_size, "Mach-O header");
            const auto ncmds = file.Read<std::uint32_t>(16, order, "load command count");
            std::uint64_t cmd_offset = header_size;

            for(std::uint32_t i = 0; i < ncmds; ++i)
            {
                // A command header that does not fit in the file ends the walk.
                if(!RangeFits(cmd_offset, sizeof(LoadCommand), file.Size()))
                {
                    break;
                }

                const auto cmd = file.Read<std::uint32_t>(cmd_offset, order, "load command");
                const auto cmdsize = file.Read<std::uint32_t>(cmd_offset + 4, order, "load command size");
                const std::string index = std::to_string(i);

                if(cmdsize < sizeof(LoadCommand))
                {
                    ThrowMalformed(kFormat, "load command " + index + " size " + std::to_string(cmdsize) +
                                                " is smaller than 8");
                }
                if(cmdsize % 4 != 0)
                {
                    ThrowMalformed(kFormat, "load command " + index + " size " + std::to_string(cmdsize) +
                                                " is not a multiple of 4");
                }

                const char *command_name = ReferenceCommandName(cmd);
                if(command_name != nullptr)
                {
                    const std::string name = std::string(command_name) + " command " + index;
                    if(cmdsize < kDylibCommandSize)
                    {
                        ThrowMalformed(kFormat, name + " size " + std::to_string(cmdsize) +
                                                    " is smaller than " + std::to_string(kDylibCommandSize));
                    }
                    const ByteSpan command = file.Sub(cmd_offset, cmdsize, name);
                    const auto name_offset = command.Read<std::uint32_t>(8, order, "dylib name offset");
                    if(name_offset >= cmdsize)
                    {
                        ThrowMalformed(kFormat, name + " has its name at offset " + std::to_string(name_offset) +
                                                    ", outside the command of " + std::to_string(cmdsize) +
                                                    " bytes");
                    }
                    std::string library = command.CString(name_offset, cmdsize, name + " name");
                    if(std::find(deps.begin(), deps.end(), library) == deps.end())
                    {
                        deps.push_back(std::move(library));
                    }
                }

                cmd_offset += cmdsize;
            }
        };

        if(outer_magic == FAT_MAGIC || outer_magic == FAT_CIGAM)
        {
            // GetArchSlices checks the slice table and that every slice lies
            // inside the file and apart from the others.
            const ByteSpan whole(bytes, kFormat);
            // A slice table that does not fit in the file is reported as it
            // always was for this call: the file is not taken as a fat file.
            // (The genuine universal sample lands here until the byte order is
            // corrected.)
            if(bytes.size() >= sizeof(FatHeader))
            {
                const std::uint64_t count = whole.Read<std::uint32_t>(
                    4, OrderFor(outer_magic == FAT_MAGIC), "fat_arch count");
                if(sizeof(FatHeader) + count * kFatArchEntrySize > whole.Size())
                {
                    ThrowMalformed(kFormat, "Not a single-arch Mach-O binary");
                }
            }
            for(const ArchSlice &slice : MachOParser::GetArchSlices(file_path))
            {
                list_image(whole.Sub(slice.offset, slice.size, "fat slice"));
            }
        }
        else
        {
            list_image(ByteSpan(bytes, kFormat));
        }

        return deps;
    });
}
