#include "PeParser.hpp"

#include "ByteSwap.hpp"
#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using seed::internal::ByteOrder;
using seed::internal::ByteSpan;
using seed::internal::ThrowMalformed;

constexpr const char *kFormat = "PE";
constexpr std::uint64_t kMaxNameLength = 4096;

template <typename T>
T FromLittleEndian(T value)
{
    return ByteSwapIfNeeded(value, true);
}

// Converts an RVA to a file offset. All arithmetic is 64-bit, so a section
// near the top of the 32-bit range does not wrap.
std::uint64_t RvaToOffset(
    std::uint32_t rva,
    const std::vector<IMAGE_SECTION_HEADER> &sections,
    std::uint32_t size_of_headers,
    std::uint64_t file_size,
    const std::string &what
)
{
    if(rva < size_of_headers && rva < file_size)
    {
        return rva;
    }

    for(const auto &section : sections)
    {
        const std::uint64_t virtual_address = FromLittleEndian(section.VirtualAddress);
        const std::uint64_t virtual_size = FromLittleEndian(section.Misc.VirtualSize);
        const std::uint64_t raw_size = FromLittleEndian(section.SizeOfRawData);
        const std::uint64_t raw_pointer = FromLittleEndian(section.PointerToRawData);
        const std::uint64_t span = std::max(virtual_size, raw_size);

        if(rva >= virtual_address && rva < virtual_address + span)
        {
            const std::uint64_t offset = raw_pointer + (rva - virtual_address);
            if(offset >= file_size)
            {
                break;
            }
            return offset;
        }
    }

    ThrowMalformed(kFormat, what + " RVA " + std::to_string(rva) + " is not inside the file (" +
                                std::to_string(file_size) + " bytes)");
}
} // namespace

std::vector<std::string> PeParser::ListDependencies(const std::string &file_path)
{
    return PeParser::ListDependenciesFromBytes(ReadFileBytes(file_path));
}

std::vector<std::string> PeParser::ListDependenciesFromBytes(const std::vector<std::uint8_t> &bytes)
{
    const ByteSpan file(bytes, kFormat);
    const std::uint64_t file_size = file.Size();

    const ByteSpan dos = file.Sub(0, sizeof(IMAGE_DOS_HEADER), "DOS header");
    if(dos.Read<std::uint16_t>(0, ByteOrder::Little, "DOS header signature") != IMAGE_DOS_SIGNATURE)
    {
        ThrowMalformed(kFormat, "DOS header signature is not MZ");
    }

    const std::int32_t e_lfanew =
        dos.Read<std::int32_t>(offsetof(IMAGE_DOS_HEADER, e_lfanew), ByteOrder::Little, "e_lfanew");
    if(e_lfanew < 0)
    {
        ThrowMalformed(kFormat, "e_lfanew " + std::to_string(e_lfanew) + " is negative");
    }

    const std::uint64_t pe_header_offset = static_cast<std::uint64_t>(e_lfanew);
    const std::uint64_t optional_header_offset =
        pe_header_offset + sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER);

    // Signature, file header and the optional header's magic.
    const ByteSpan pe_header =
        file.Sub(pe_header_offset, sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER) + 2, "PE header");
    if(pe_header.Read<std::uint32_t>(0, ByteOrder::Little, "PE signature") != IMAGE_NT_SIGNATURE)
    {
        ThrowMalformed(kFormat, "PE header signature is invalid");
    }

    const auto coff_header = pe_header.Read<IMAGE_FILE_HEADER>(sizeof(std::uint32_t), ByteOrder::Little,
                                                               "PE file header");
    const std::uint16_t section_count = FromLittleEndian(coff_header.NumberOfSections);
    const std::uint16_t optional_header_size = FromLittleEndian(coff_header.SizeOfOptionalHeader);

    const std::uint16_t optional_magic_value = FromLittleEndian(pe_header.Read<std::uint16_t>(
        sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER), ByteOrder::Little, "optional header magic"));

    std::uint64_t data_directory_offset = 0;
    if(optional_magic_value == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        data_directory_offset = 96;
    }
    else if(optional_magic_value == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        data_directory_offset = 112;
    }
    else
    {
        ThrowMalformed(kFormat, "PE header: optional header format " +
                                    std::to_string(optional_magic_value) + " is not supported");
    }

    if(optional_header_size < data_directory_offset + (2 * sizeof(IMAGE_DATA_DIRECTORY)))
    {
        ThrowMalformed(kFormat, "PE header: optional header size " +
                                    std::to_string(optional_header_size) +
                                    " is too small for the import directory");
    }

    const auto import_directory = file.Read<IMAGE_DATA_DIRECTORY>(
        optional_header_offset + data_directory_offset + sizeof(IMAGE_DATA_DIRECTORY),
        ByteOrder::Little, "import directory");

    const std::uint32_t import_table_rva = FromLittleEndian(import_directory.VirtualAddress);
    if(import_table_rva == 0)
    {
        return {};
    }

    // SizeOfHeaders is at offset 60 in both optional header formats.
    const std::uint32_t size_of_headers = FromLittleEndian(
        file.Read<std::uint32_t>(optional_header_offset + 60, ByteOrder::Little, "SizeOfHeaders"));

    // The whole table must fit before any space is reserved for it.
    const ByteSpan section_table = file.Sub(
        optional_header_offset + optional_header_size,
        static_cast<std::uint64_t>(section_count) * sizeof(IMAGE_SECTION_HEADER), "section table");
    std::vector<IMAGE_SECTION_HEADER> sections;
    sections.reserve(section_count);
    for(std::uint16_t index = 0; index < section_count; ++index)
    {
        sections.push_back(section_table.Read<IMAGE_SECTION_HEADER>(
            static_cast<std::uint64_t>(index) * sizeof(IMAGE_SECTION_HEADER), ByteOrder::Little,
            "section table entry"));
    }

    std::uint64_t descriptor_offset = RvaToOffset(import_table_rva, sections, size_of_headers,
                                                  file_size, "import descriptor table");

    // Walk the descriptors first; each step is 20 bytes inside the file.
    std::vector<std::uint32_t> name_rvas;
    while(true)
    {
        const auto descriptor = file.Read<IMAGE_IMPORT_DESCRIPTOR>(descriptor_offset, ByteOrder::Little,
                                                                   "import descriptor");
        const std::uint32_t original_first_thunk = FromLittleEndian(descriptor.OriginalFirstThunk);
        const std::uint32_t time_date_stamp = FromLittleEndian(descriptor.TimeDateStamp);
        const std::uint32_t forwarder_chain = FromLittleEndian(descriptor.ForwarderChain);
        const std::uint32_t name_rva = FromLittleEndian(descriptor.Name);
        const std::uint32_t first_thunk = FromLittleEndian(descriptor.FirstThunk);

        if(original_first_thunk == 0 && time_date_stamp == 0 && forwarder_chain == 0 && name_rva == 0 &&
           first_thunk == 0)
        {
            break;
        }

        name_rvas.push_back(name_rva);
        descriptor_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }

    std::vector<std::string> dependencies;
    dependencies.reserve(name_rvas.size());
    std::uint64_t total_name_bytes = 0;
    for(const std::uint32_t name_rva : name_rvas)
    {
        const auto name_offset = RvaToOffset(name_rva, sections, size_of_headers, file_size, "DLL name");
        dependencies.push_back(file.CString(name_offset, kMaxNameLength, "DLL name"));
        total_name_bytes += dependencies.back().size();
        if(total_name_bytes > file_size)
        {
            ThrowMalformed(kFormat, "DLL names total " + std::to_string(total_name_bytes) +
                                        " bytes, more than the file (" + std::to_string(file_size) +
                                        " bytes)");
        }
    }

    return dependencies;
}
