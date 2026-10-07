#pragma once

// Size limit for a Mach-O code signature SuperBlob. The load command that
// points at it stores its offset and size in 32 bits.

#include "BoundedBytes.hpp"

#include <cstdint>
#include <string>

namespace seed::internal
{

inline void CheckMachOSuperBlobSize(std::uint64_t size)
{
    if(size > UINT32_MAX)
    {
        ThrowMalformed("Mach-O", "SuperBlob size " + std::to_string(size) +
                                     " does not fit the 32-bit size field of LC_CODE_SIGNATURE");
    }
}

} // namespace seed::internal
