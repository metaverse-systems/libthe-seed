#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path);
void WriteFileBytes(const std::string &file_path, const std::vector<std::uint8_t> &bytes);
