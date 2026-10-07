#pragma once

// Helpers for tests that feed crafted and truncated files to the binary
// tooling and check that each is rejected cleanly.
//
// Every program that includes this header must expand
// SEED_DEFINE_HEAP_COUNTER() exactly once (see HeapCounter.hpp).

#include "HeapCounter.hpp"
#include "TestPaths.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace seedtest::malformed
{

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint64_t kHeapAllowance = 16ull * 1024 * 1024;
inline constexpr std::chrono::seconds kTimeLimit{1};

inline Bytes ReadAll(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    {
        FAIL("Cannot open " << path);
    }
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Bytes of a file in tests/fixtures.
inline Bytes LoadSample(const std::string &name)
{
    return ReadAll(FixturePath(name));
}

// Bytes of the library under test (the ELF sample).
inline Bytes LoadLibraryUnderTest()
{
    return ReadAll(LibraryPath());
}

template <typename T>
inline void PatchLE(Bytes &bytes, std::uint64_t offset, T value)
{
    REQUIRE(offset + sizeof(T) <= bytes.size());
    for(std::size_t i = 0; i < sizeof(T); ++i)
    {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value)) >> (8 * i)) &
            0xFF);
    }
}

template <typename T>
inline void PatchBE(Bytes &bytes, std::uint64_t offset, T value)
{
    REQUIRE(offset + sizeof(T) <= bytes.size());
    for(std::size_t i = 0; i < sizeof(T); ++i)
    {
        bytes[offset + sizeof(T) - 1 - i] = static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<T>>(value)) >> (8 * i)) &
            0xFF);
    }
}

inline Bytes Truncate(const Bytes &bytes, std::size_t length)
{
    REQUIRE(length <= bytes.size());
    return Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
}

namespace detail
{

template <typename T>
inline T GetLE(const Bytes &bytes, std::uint64_t offset)
{
    REQUIRE(offset + sizeof(T) <= bytes.size());
    std::uint64_t value = 0;
    for(std::size_t i = 0; i < sizeof(T); ++i)
    {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    }
    return static_cast<T>(value);
}

} // namespace detail

// Offsets of header fields that tests patch.
inline constexpr std::uint64_t kElf64PhoffField = 32;
inline constexpr std::uint64_t kElf32PhoffField = 28;
inline constexpr std::uint64_t kPeLfanewField = 0x3C;
inline constexpr std::uint64_t kCfbHeaderSize = 512;

// File offset of the first ELF program header (the value of e_phoff) of a
// little-endian ELF file.
inline std::uint64_t ElfProgramHeaderOffset(const Bytes &bytes)
{
    REQUIRE(bytes.size() > 5);
    REQUIRE(bytes[0] == 0x7F);
    if(bytes[4] == 2)
    {
        return detail::GetLE<std::uint64_t>(bytes, kElf64PhoffField);
    }
    return detail::GetLE<std::uint32_t>(bytes, kElf32PhoffField);
}

// File offset of the "PE\0\0" signature (the value of e_lfanew).
inline std::uint64_t PeHeaderOffset(const Bytes &bytes)
{
    return static_cast<std::uint32_t>(detail::GetLE<std::int32_t>(bytes, kPeLfanewField));
}

// File offset of load command `index` (zero-based) in a thin Mach-O file whose
// header is stored little-endian.
inline std::uint64_t MachOCommandOffset(const Bytes &bytes, std::uint32_t index)
{
    const std::uint32_t magic = detail::GetLE<std::uint32_t>(bytes, 0);
    REQUIRE((magic == 0xFEEDFACFu || magic == 0xFEEDFACEu));
    std::uint64_t offset = magic == 0xFEEDFACFu ? 32 : 28;
    const std::uint32_t count = detail::GetLE<std::uint32_t>(bytes, 16);
    REQUIRE(index < count);
    for(std::uint32_t i = 0; i < index; ++i)
    {
        const std::uint32_t cmdsize = detail::GetLE<std::uint32_t>(bytes, offset + 4);
        REQUIRE(cmdsize >= 8);
        offset += cmdsize;
    }
    return offset;
}

// File offset of directory entry `index` of a compound file, following the
// FAT chain that holds the directory (header DIFAT entries only, which covers
// every sample-sized file).
inline std::uint64_t CfbDirectoryEntryOffset(const Bytes &bytes, std::uint32_t index)
{
    const std::uint32_t sector_size = 1u << detail::GetLE<std::uint16_t>(bytes, 30);
    REQUIRE((sector_size == 512 || sector_size == 4096));
    const std::uint64_t entries_per_sector = sector_size / 128;
    std::uint32_t hops = index / static_cast<std::uint32_t>(entries_per_sector);
    std::uint32_t sector = detail::GetLE<std::uint32_t>(bytes, 48);
    while(hops > 0)
    {
        const std::uint32_t fat_index = sector / (sector_size / 4);
        REQUIRE(fat_index < 109);
        const std::uint32_t fat_sector = detail::GetLE<std::uint32_t>(bytes, 76 + 4ull * fat_index);
        const std::uint64_t entry = (static_cast<std::uint64_t>(fat_sector) + 1) * sector_size +
                                    4ull * (sector % (sector_size / 4));
        sector = detail::GetLE<std::uint32_t>(bytes, entry);
        --hops;
    }
    return (static_cast<std::uint64_t>(sector) + 1) * sector_size +
           128ull * (index % entries_per_sector);
}

// Writes bytes into the scratch directory and returns the path.
inline std::string WriteScratch(const ScratchDir &scratch, const std::string &name,
                                const Bytes &bytes)
{
    const std::string path = scratch.File(name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out)
    {
        FAIL("Cannot create " << path);
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path;
}

// After a rejected embed or strip the file must be byte-for-byte unchanged.
inline void RequireUnchanged(const std::string &path, const Bytes &original)
{
    const Bytes now = ReadAll(path);
    INFO("file " << path << " was modified (" << original.size() << " -> " << now.size()
                 << " bytes)");
    REQUIRE(now == original);
}

namespace detail
{

struct Outcome
{
    bool threw = false;
    bool runtime_error_type = false;
    std::string type_name;
    std::string message;
    std::chrono::steady_clock::duration elapsed{};
    std::size_t peak_growth = 0;
};

template <typename F>
inline Outcome Run(F &&callable)
{
    Outcome outcome;
    heap::HeapGrowthScope scope;
    const auto start = std::chrono::steady_clock::now();
    try
    {
        callable();
    }
    catch(const std::exception &e)
    {
        outcome.threw = true;
        outcome.type_name = typeid(e).name();
        outcome.runtime_error_type = typeid(e) == typeid(std::runtime_error) ||
                                     dynamic_cast<const std::runtime_error *>(&e) != nullptr;
        outcome.message = e.what();
    }
    catch(...)
    {
        outcome.threw = true;
        outcome.type_name = "(not derived from std::exception)";
    }
    outcome.elapsed = std::chrono::steady_clock::now() - start;
    outcome.peak_growth = scope.PeakGrowth();
    return outcome;
}

inline void CheckLimits(const Outcome &outcome, std::uint64_t input_size,
                        std::uint64_t extra_allowance)
{
    INFO("elapsed ms: " << std::chrono::duration_cast<std::chrono::milliseconds>(outcome.elapsed).count());
    CHECK(outcome.elapsed < kTimeLimit);
    if(heap::HeapGrowthScope::Active())
    {
        INFO("peak heap growth: " << outcome.peak_growth);
        CHECK(outcome.peak_growth <= 10 * input_size + kHeapAllowance + extra_allowance);
    }
}

inline void CheckRejection(const Outcome &outcome, const std::string &format,
                           const std::string &keyword)
{
    INFO("dynamic type: " << outcome.type_name);
    INFO("message: " << outcome.message);
    REQUIRE(outcome.threw);
    REQUIRE(outcome.runtime_error_type);
    CHECK(outcome.message.rfind(format + ": ", 0) == 0);
    CHECK(outcome.message.find(keyword) != std::string::npos);
    CHECK(outcome.message.find("internal error (") == std::string::npos);
}

} // namespace detail

// Passes only when the callable throws std::runtime_error whose message starts
// with "<format>: " and contains the keyword, never mentions an internal error,
// and the call stays within the time and heap limits. input_size is the size of
// the file being parsed; extra_allowance covers signature data supplied for
// embedding.
template <typename F>
inline void RequireRejected(F &&callable, const std::string &format, const std::string &keyword,
                            std::uint64_t input_size = 0, std::uint64_t extra_allowance = 0)
{
    const detail::Outcome outcome = detail::Run(callable);
    detail::CheckRejection(outcome, format, keyword);
    detail::CheckLimits(outcome, input_size, extra_allowance);
}

// For the truncation sweep: a prefix of a sample may still be acceptable. The
// callable must either be rejected as RequireRejected describes (any keyword)
// or return exactly `same_as_full`, the result for the whole sample.
template <typename F, typename R>
inline void RequireRejectedOrSameAsBefore(F &&callable, const std::string &format,
                                          const R &same_as_full, std::uint64_t input_size = 0,
                                          std::uint64_t extra_allowance = 0)
{
    std::optional<R> result;
    const detail::Outcome outcome = detail::Run([&] { result = callable(); });
    if(outcome.threw)
    {
        detail::CheckRejection(outcome, format, "");
    }
    else
    {
        REQUIRE(result.has_value());
        CHECK(*result == same_as_full);
    }
    detail::CheckLimits(outcome, input_size, extra_allowance);
}

} // namespace seedtest::malformed
