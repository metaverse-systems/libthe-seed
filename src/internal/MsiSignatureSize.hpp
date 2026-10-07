#pragma once

// Size limit for a signature stored in an installer package. A version 3
// compound file keeps a stream size in 32 bits; version 4 keeps 64 bits.

#include "BoundedBytes.hpp"

#include <cstdint>
#include <string>

namespace seed::internal
{

inline void CheckMsiSignatureSize(std::uint64_t size, std::uint16_t major_version)
{
    if(major_version < 4 && size > UINT32_MAX)
    {
        ThrowMalformed("MSI", "signature size " + std::to_string(size) +
                                  " does not fit the 32-bit stream size of a version " +
                                  std::to_string(major_version) + " file");
    }
}

} // namespace seed::internal
