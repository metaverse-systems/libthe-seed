#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

inline std::string ReadCString(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if(offset >= bytes.size())
    {
        throw std::runtime_error("Invalid string offset: out of bounds");
    }

    std::size_t end = offset;
    while(end < bytes.size() && bytes[end] != 0)
    {
        ++end;
    }

    if(end == bytes.size())
    {
        throw std::runtime_error("Unterminated string at offset " + std::to_string(offset));
    }

    return std::string(
        reinterpret_cast<const char *>(bytes.data() + offset),
        reinterpret_cast<const char *>(bytes.data() + end)
    );
}
