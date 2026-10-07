#pragma once

// Limits of the PE certificate table that do not depend on a file.

#include "BoundedBytes.hpp"

#include <cstdint>
#include <string>

namespace seed::internal
{

// A WIN_CERTIFICATE holds an 8-byte header and the signature, and its length
// is a 32-bit field.
inline constexpr std::uint64_t kPeMaxSignatureSize = static_cast<std::uint64_t>(UINT32_MAX) - 8;

// Rejects a signature that the WIN_CERTIFICATE length field cannot describe.
// Takes the length so the check needs no buffer of that size.
inline void CheckPeSignatureSize(std::uint64_t signature_size)
{
    if(signature_size > kPeMaxSignatureSize)
    {
        ThrowMalformed("PE", "signature size " + std::to_string(signature_size) +
                                 " does not fit in a WIN_CERTIFICATE (limit " +
                                 std::to_string(kPeMaxSignatureSize) + ")");
    }
}

} // namespace seed::internal
