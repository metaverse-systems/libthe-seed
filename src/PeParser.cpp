#include "PeParser.hpp"

#include "ByteSwap.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
template <typename T>
T ReadStruct(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset + sizeof(T) > bytes.size())
    {
        throw std::runtime_error("Truncated PE file");
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

template <typename T>
T FromLittleEndian(T value)
{
    return ByteSwapIfNeeded(value, true);
}

std::string ReadCString(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset >= bytes.size())
    {
        throw std::runtime_error("Invalid string offset in PE");
    }

    std::size_t end = offset;
    while(end < bytes.size() && bytes[end] != 0)
    {
        ++end;
    }

    if(end == bytes.size())
    {
        throw std::runtime_error("Unterminated DLL name in PE");
    }

    return std::string(
        reinterpret_cast<const char *>(bytes.data() + offset),
        reinterpret_cast<const char *>(bytes.data() + end)
    );
}

std::size_t RvaToOffset(
    std::uint32_t rva,
    const std::vector<IMAGE_SECTION_HEADER> &sections,
    std::uint32_t size_of_headers,
    std::size_t file_size
)
{
    if(rva < size_of_headers && rva < file_size)
    {
        return rva;
    }

    for(const auto &section : sections)
    {
        const std::uint32_t virtual_address = FromLittleEndian(section.VirtualAddress);
        const std::uint32_t virtual_size = FromLittleEndian(section.Misc.VirtualSize);
        const std::uint32_t raw_size = FromLittleEndian(section.SizeOfRawData);
        const std::uint32_t raw_pointer = FromLittleEndian(section.PointerToRawData);
        const std::uint32_t span = std::max(virtual_size, raw_size);

        if(rva >= virtual_address && rva < virtual_address + span)
        {
            const std::size_t offset = static_cast<std::size_t>(raw_pointer) + (rva - virtual_address);
            if(offset >= file_size)
            {
                break;
            }
            return offset;
        }
    }

    throw std::runtime_error("Unable to convert PE RVA to file offset");
}
} // namespace

std::vector<std::string> PeParser::ListDependencies(const std::string &file_path)
{
    const auto bytes = ReadFileBytes(file_path);
    if(bytes.size() < sizeof(IMAGE_DOS_HEADER))
    {
        throw std::runtime_error("File is too small to be a PE binary");
    }

    const auto dos_header = ReadStruct<IMAGE_DOS_HEADER>(bytes, 0);
    if(FromLittleEndian(dos_header.e_magic) != IMAGE_DOS_SIGNATURE)
    {
        throw std::runtime_error("Invalid PE DOS signature");
    }

    const auto pe_header_offset = static_cast<std::size_t>(FromLittleEndian(dos_header.e_lfanew));
    const auto signature = ReadStruct<std::uint32_t>(bytes, pe_header_offset);
    if(FromLittleEndian(signature) != IMAGE_NT_SIGNATURE)
    {
        throw std::runtime_error("Invalid PE signature");
    }

    const auto coff_header = ReadStruct<IMAGE_FILE_HEADER>(bytes, pe_header_offset + sizeof(std::uint32_t));
    const std::uint16_t section_count = FromLittleEndian(coff_header.NumberOfSections);
    const std::uint16_t optional_header_size = FromLittleEndian(coff_header.SizeOfOptionalHeader);

    const std::size_t optional_header_offset = pe_header_offset + sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER);
    const auto optional_magic = ReadStruct<std::uint16_t>(bytes, optional_header_offset);
    const std::uint16_t optional_magic_value = FromLittleEndian(optional_magic);

    std::size_t data_directory_offset = 0;
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
        throw std::runtime_error("Unsupported PE optional header format");
    }

    if(optional_header_size < data_directory_offset + (2 * sizeof(IMAGE_DATA_DIRECTORY)))
    {
        throw std::runtime_error("PE optional header is too small for import directory");
    }

    const auto import_directory = ReadStruct<IMAGE_DATA_DIRECTORY>(
        bytes,
        optional_header_offset + data_directory_offset + sizeof(IMAGE_DATA_DIRECTORY)
    );

    const std::uint32_t import_table_rva = FromLittleEndian(import_directory.VirtualAddress);
    if(import_table_rva == 0)
    {
        return {};
    }

    std::uint32_t size_of_headers = 0;
    if(optional_magic_value == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        const auto optional32 = ReadStruct<IMAGE_OPTIONAL_HEADER32>(bytes, optional_header_offset);
        size_of_headers = FromLittleEndian(optional32.SizeOfHeaders);
    }
    else
    {
        const auto optional64 = ReadStruct<IMAGE_OPTIONAL_HEADER64>(bytes, optional_header_offset);
        size_of_headers = FromLittleEndian(optional64.SizeOfHeaders);
    }

    std::vector<IMAGE_SECTION_HEADER> sections;
    sections.reserve(section_count);

    const std::size_t section_table_offset = optional_header_offset + optional_header_size;
    for(std::uint16_t index = 0; index < section_count; ++index)
    {
        sections.push_back(ReadStruct<IMAGE_SECTION_HEADER>(
            bytes,
            section_table_offset + static_cast<std::size_t>(index) * sizeof(IMAGE_SECTION_HEADER)
        ));
    }

    std::size_t descriptor_offset = RvaToOffset(import_table_rva, sections, size_of_headers, bytes.size());

    std::vector<std::string> dependencies;
    while(true)
    {
        const auto descriptor = ReadStruct<IMAGE_IMPORT_DESCRIPTOR>(bytes, descriptor_offset);
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

        const auto name_offset = RvaToOffset(name_rva, sections, size_of_headers, bytes.size());
        dependencies.push_back(ReadCString(bytes, name_offset));

        descriptor_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }

    return dependencies;
}
