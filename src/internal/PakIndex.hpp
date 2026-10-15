#pragma once

#include "PakFile.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace seed::internal
{

// A pak breaks a rule of the pak format. what() is the detail text (without a
// leading "damaged: ").
class PakDamaged : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// One resource of a pak.
struct PakEntry
{
    std::string name;
    // Absolute file offset of the first byte.
    std::uint64_t offset = 0;
    // Byte count, already checked to fit a std::size_t.
    std::uint64_t size = 0;
};

// What the description of a pak says, once every rule of the format holds.
// Immutable after construction, so threads read it without locks.
struct PakIndex
{
    // The file version the index was validated against.
    PakStamp stamp;
    // In description order.
    std::vector<PakEntry> entries;
    // Resource name to position in `entries`.
    std::unordered_map<std::string, std::size_t> by_name;
    // Length of the description line including its newline: the offset of the
    // first resource.
    std::uint64_t description_size = 0;
};

// Reads and validates the description of `file`, applying every rule of the
// pak format in order and reporting the first one broken (then in resource
// order). This is the only place pak positions are computed.
//
// Throws PakDamaged for a broken rule or a file that ends while the
// description is read, and PakReadError when the system refuses a read. No
// parser exception leaves the function.
std::shared_ptr<const PakIndex> PakIndexRead(PakFile &file);

// Number of descriptions parsed since the process started or since the last
// reset. Tests use it to check how often a description is read; it has no
// effect on behaviour.
std::uint64_t PakDescriptionParseCount();
void ResetPakDescriptionParseCount();

} // namespace seed::internal
