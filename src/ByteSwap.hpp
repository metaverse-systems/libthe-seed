#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

template <typename T>
inline T ByteSwap(T value)
{
    static_assert(std::is_integral_v<T>, "ByteSwap requires an integral type");

    using UnsignedT = std::make_unsigned_t<T>;
    UnsignedT input = static_cast<UnsignedT>(value);

    if constexpr(sizeof(T) == sizeof(std::uint16_t))
    {
        UnsignedT swapped = ((input & 0x00FFu) << 8u) | ((input & 0xFF00u) >> 8u);
        return static_cast<T>(swapped);
    }

    if constexpr(sizeof(T) == sizeof(std::uint32_t))
    {
        UnsignedT swapped = ((input & 0x000000FFu) << 24u) |
                            ((input & 0x0000FF00u) << 8u) |
                            ((input & 0x00FF0000u) >> 8u) |
                            ((input & 0xFF000000u) >> 24u);
        return static_cast<T>(swapped);
    }

    if constexpr(sizeof(T) == sizeof(std::uint64_t))
    {
        UnsignedT swapped = ((input & 0x00000000000000FFull) << 56u) |
                            ((input & 0x000000000000FF00ull) << 40u) |
                            ((input & 0x0000000000FF0000ull) << 24u) |
                            ((input & 0x00000000FF000000ull) << 8u) |
                            ((input & 0x000000FF00000000ull) >> 8u) |
                            ((input & 0x0000FF0000000000ull) >> 24u) |
                            ((input & 0x00FF000000000000ull) >> 40u) |
                            ((input & 0xFF00000000000000ull) >> 56u);
        return static_cast<T>(swapped);
    }

    return value;
}

template <typename T>
inline T ByteSwapIfNeeded(T value, bool file_is_little_endian)
{
    static_assert(std::is_integral_v<T>, "ByteSwapIfNeeded requires an integral type");

    const bool host_is_little_endian = std::endian::native == std::endian::little;
    if(host_is_little_endian == file_is_little_endian)
    {
        return value;
    }

    return ByteSwap(value);
}
