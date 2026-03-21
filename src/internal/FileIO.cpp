#include "FileIO.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

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
        throw std::runtime_error("Unable to read file size: " + file_path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(size > 0)
    {
        input.read(reinterpret_cast<char *>(bytes.data()), size);
        if(!input)
        {
            throw std::runtime_error("Unable to read file: " + file_path);
        }
    }
    return bytes;
}

void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes)
{
    const auto temp_path = file_path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary);
        if(!output.is_open())
        {
            throw std::runtime_error("Unable to create temp file: " + temp_path);
        }
        output.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        if(!output)
        {
            throw std::runtime_error("Unable to write temp file: " + temp_path);
        }
    }
    try
    {
        std::filesystem::rename(temp_path, file_path);
    }
    catch(const std::filesystem::filesystem_error &e)
    {
        std::filesystem::remove(temp_path);
        throw std::runtime_error("Unable to rename temp file to " + file_path + ": " + e.what());
    }
}
