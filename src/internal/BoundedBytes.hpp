#pragma once

// Bounds-checked access to untrusted byte buffers, and the guard that gives
// every public entry point of the binary tooling one error type.
//
// All position and length arithmetic is done in std::uint64_t and checked
// before any memory is touched. A rejection is always std::runtime_error
// whose message starts with "<Format>: ".

#include "ByteSwap.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#include <memory>
#endif

namespace seed::internal
{

enum class ByteOrder
{
    Little,
    Big
};

// True when [offset, offset + length) lies inside a buffer of the given size.
// Never overflows.
inline bool RangeFits(std::uint64_t offset, std::uint64_t length, std::uint64_t size)
{
    return offset <= size && length <= size - offset;
}

[[noreturn]] inline void ThrowMalformed(std::string_view format, const std::string &detail)
{
    std::string message(format);
    message += ": ";
    message += detail;
    throw std::runtime_error(message);
}

inline std::uint64_t CheckedAdd(std::uint64_t a, std::uint64_t b, std::string_view format,
                                std::string_view what)
{
    if(a > UINT64_MAX - b)
    {
        ThrowMalformed(format, std::string(what) + " overflows");
    }
    return a + b;
}

inline std::uint64_t CheckedMultiply(std::uint64_t a, std::uint64_t b, std::string_view format,
                                     std::string_view what)
{
    if(a != 0 && b > UINT64_MAX / a)
    {
        ThrowMalformed(format, std::string(what) + " overflows");
    }
    return a * b;
}

inline std::size_t ToSize(std::uint64_t value, std::string_view format, std::string_view what)
{
    if(value > static_cast<std::uint64_t>(SIZE_MAX))
    {
        ThrowMalformed(format, std::string(what) + " overflows");
    }
    return static_cast<std::size_t>(value);
}

namespace detail
{

template <typename T>
inline T ToHostOrder(T value, ByteOrder order)
{
    if constexpr(std::is_integral_v<T> && sizeof(T) > 1)
    {
        return ByteSwapIfNeeded(value, order == ByteOrder::Little);
    }
    else
    {
        (void)order;
        return value;
    }
}

inline std::string PastTheEnd(std::string_view what, std::uint64_t offset, std::string_view scope,
                              std::uint64_t size)
{
    std::string message(what);
    message += " at offset " + std::to_string(offset);
    message += " extends past the end of ";
    message += scope;
    message += " (" + std::to_string(size) + " bytes)";
    return message;
}

inline std::string StructurePastTheEnd(std::string_view structure, std::uint64_t offset,
                                       std::uint64_t length, std::string_view scope,
                                       std::uint64_t size)
{
    std::string message(structure);
    message += " (offset " + std::to_string(offset) + ", size " + std::to_string(length) + ")";
    message += " extends past the end of ";
    message += scope;
    message += " (" + std::to_string(size) + " bytes)";
    return message;
}

} // namespace detail

// Read-only view of a byte range. Every access is checked against the view.
class ByteSpan
{
public:
    ByteSpan(const std::uint8_t *data, std::uint64_t size, std::string_view format,
             std::string_view scope = "the file")
        : data(data), size(size), format(format), scope(scope)
    {
    }

    ByteSpan(const std::vector<std::uint8_t> &bytes, std::string_view format,
             std::string_view scope = "the file")
        : ByteSpan(bytes.data(), bytes.size(), format, scope)
    {
    }

    template <typename T>
    T Read(std::uint64_t offset, ByteOrder order, std::string_view field) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "Read requires a trivially copyable type");
        if(!RangeFits(offset, sizeof(T), this->size))
        {
            ThrowMalformed(this->format,
                           detail::PastTheEnd(field, offset, this->scope, this->size));
        }
        T value;
        std::memcpy(&value, this->data + offset, sizeof(T));
        return detail::ToHostOrder(value, order);
    }

    ByteSpan Sub(std::uint64_t offset, std::uint64_t length, std::string_view structure) const
    {
        if(!RangeFits(offset, length, this->size))
        {
            ThrowMalformed(this->format, detail::StructurePastTheEnd(structure, offset, length,
                                                                     this->scope, this->size));
        }
        return ByteSpan(this->data + offset, length, this->format, structure);
    }

    // Returns the bytes before the first NUL at or after offset. The NUL must
    // lie within max_length bytes of offset and inside the view.
    std::string CString(std::uint64_t offset, std::uint64_t max_length,
                        std::string_view structure) const
    {
        if(offset >= this->size)
        {
            ThrowMalformed(this->format,
                           std::string(structure) + " at offset " + std::to_string(offset) +
                               " is outside " + std::string(this->scope) + " (" +
                               std::to_string(this->size) + " bytes)");
        }
        std::uint64_t available = this->size - offset;
        std::uint64_t limit = max_length < available ? max_length : available;
        const std::uint8_t *start = this->data + offset;
        for(std::uint64_t i = 0; i < limit; ++i)
        {
            if(start[i] == 0)
            {
                return std::string(reinterpret_cast<const char *>(start),
                                   static_cast<std::size_t>(i));
            }
        }
        if(limit == max_length && max_length < available)
        {
            ThrowMalformed(this->format, std::string(structure) + " at offset " +
                                             std::to_string(offset) + " is longer than " +
                                             std::to_string(max_length) + " bytes");
        }
        ThrowMalformed(this->format,
                       std::string(structure) + " at offset " + std::to_string(offset) +
                           " is unterminated before the end of " + std::string(this->scope) +
                           " (" + std::to_string(this->size) + " bytes)");
    }

    std::uint64_t Size() const
    {
        return this->size;
    }

    const std::uint8_t *Data() const
    {
        return this->data;
    }

private:
    const std::uint8_t *data;
    std::uint64_t size;
    std::string format;
    std::string scope;
};

// Writable view used by the signers for in-place updates.
class MutableByteSpan
{
public:
    MutableByteSpan(std::uint8_t *data, std::uint64_t size, std::string_view format,
                    std::string_view scope = "the file")
        : data(data), size(size), format(format), scope(scope)
    {
    }

    MutableByteSpan(std::vector<std::uint8_t> &bytes, std::string_view format,
                    std::string_view scope = "the file")
        : MutableByteSpan(bytes.data(), bytes.size(), format, scope)
    {
    }

    template <typename T>
    void Write(std::uint64_t offset, T value, ByteOrder order, std::string_view field) const
    {
        static_assert(std::is_trivially_copyable_v<T>, "Write requires a trivially copyable type");
        if(!RangeFits(offset, sizeof(T), this->size))
        {
            ThrowMalformed(this->format,
                           detail::PastTheEnd(field, offset, this->scope, this->size));
        }
        T stored = detail::ToHostOrder(value, order);
        std::memcpy(this->data + offset, &stored, sizeof(T));
    }

    void Fill(std::uint64_t offset, std::uint64_t length, std::uint8_t byte,
              std::string_view structure) const
    {
        this->Check(offset, length, structure);
        if(length != 0)
        {
            std::memset(this->data + offset, byte, static_cast<std::size_t>(length));
        }
    }

    void Copy(std::uint64_t offset, const std::vector<std::uint8_t> &source,
              std::string_view structure) const
    {
        this->Check(offset, source.size(), structure);
        if(!source.empty())
        {
            std::memcpy(this->data + offset, source.data(), source.size());
        }
    }

    std::uint64_t Size() const
    {
        return this->size;
    }

private:
    void Check(std::uint64_t offset, std::uint64_t length, std::string_view structure) const
    {
        if(!RangeFits(offset, length, this->size))
        {
            ThrowMalformed(this->format, detail::StructurePastTheEnd(structure, offset, length,
                                                                     this->scope, this->size));
        }
    }

    std::uint8_t *data;
    std::uint64_t size;
    std::string format;
    std::string scope;
};

namespace detail
{

inline std::string TypeName(const std::exception &e)
{
    const char *name = typeid(e).name();
#if defined(__GNUG__)
    int status = 0;
    std::unique_ptr<char, void (*)(void *)> demangled(
        abi::__cxa_demangle(name, nullptr, nullptr, &status), std::free);
    if(status == 0 && demangled)
    {
        return demangled.get();
    }
#endif
    return name;
}

} // namespace detail

// Runs the body of a public entry point. std::runtime_error (and its
// subclasses) pass through unchanged; any other std::exception becomes a
// std::runtime_error naming the format and the original type.
template <typename F>
auto GuardEntryPoint(std::string_view format, F &&callable) -> decltype(callable())
{
    try
    {
        return callable();
    }
    catch(const std::runtime_error &)
    {
        throw;
    }
    catch(const std::exception &e)
    {
        std::string message(format);
        message += ": internal error (" + detail::TypeName(e) + "): " + e.what();
        throw std::runtime_error(message);
    }
}

} // namespace seed::internal
