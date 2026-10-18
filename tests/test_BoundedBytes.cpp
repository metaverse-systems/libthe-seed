#include "TestPaths.hpp"

#include "internal/BoundedBytes.hpp"
#include "internal/PagedFile.hpp"
#include "internal/RandomAccessFile.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace bb = the_seed::internal;

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

// Runs the callable and requires that it throws exactly std::runtime_error
// (not a subclass or any other type) whose message starts with the prefix and
// contains every keyword.
template <typename F>
void RequireMalformed(F &&callable, const std::string &prefix,
                      const std::vector<std::string> &keywords = {})
{
    bool thrown = false;
    try
    {
        callable();
    }
    catch(const std::exception &e)
    {
        thrown = true;
        INFO("dynamic type: " << typeid(e).name());
        INFO("message: " << e.what());
        REQUIRE(typeid(e) == typeid(std::runtime_error));
        std::string message = e.what();
        REQUIRE(message.rfind(prefix, 0) == 0);
        for(const auto &keyword : keywords)
        {
            CHECK(message.find(keyword) != std::string::npos);
        }
    }
    REQUIRE(thrown);
}

std::vector<std::uint8_t> Sequence(std::size_t count)
{
    std::vector<std::uint8_t> bytes(count);
    for(std::size_t i = 0; i < count; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(i + 1);
    }
    return bytes;
}

} // namespace

TEST_CASE("RangeFits boundary table", "[BoundedBytes]")
{
    const std::uint64_t sizes[] = {0, 1, 8, kMax};
    const std::uint64_t lengths[] = {0, 1, 8, kMax};

    for(std::uint64_t size : sizes)
    {
        std::vector<std::uint64_t> offsets = {0, size - 1, size, size + 1, kMax - 7, kMax};
        for(std::uint64_t offset : offsets)
        {
            for(std::uint64_t length : lengths)
            {
                // Reference answer computed without the helper's formula, using
                // 128-bit arithmetic so nothing can wrap.
                unsigned __int128 end = static_cast<unsigned __int128>(offset) + length;
                bool expected = end <= static_cast<unsigned __int128>(size);

                INFO("size " << size << " offset " << offset << " length " << length);
                CHECK(bb::RangeFits(offset, length, size) == expected);
            }
        }
    }
}

TEST_CASE("RangeFits named cases", "[BoundedBytes]")
{
    CHECK(bb::RangeFits(0, 0, 0));
    CHECK(bb::RangeFits(0, 1, 1));
    CHECK_FALSE(bb::RangeFits(0, 2, 1));
    CHECK(bb::RangeFits(8, 0, 8));
    CHECK_FALSE(bb::RangeFits(8, 1, 8));
    CHECK_FALSE(bb::RangeFits(9, 0, 8));
    CHECK(bb::RangeFits(7, 1, 8));
    CHECK_FALSE(bb::RangeFits(kMax, 1, kMax));
    CHECK(bb::RangeFits(kMax, 0, kMax));
    CHECK_FALSE(bb::RangeFits(kMax - 7, 8, 8));
    CHECK_FALSE(bb::RangeFits(1, kMax, kMax));
    CHECK(bb::RangeFits(0, kMax, kMax));
}

TEST_CASE("CheckedAdd around the 64-bit limit", "[BoundedBytes]")
{
    CHECK(bb::CheckedAdd(0, 0, "PE", "sum") == 0);
    CHECK(bb::CheckedAdd(1, 2, "PE", "sum") == 3);
    CHECK(bb::CheckedAdd(kMax - 1, 1, "PE", "sum") == kMax);
    CHECK(bb::CheckedAdd(kMax, 0, "PE", "sum") == kMax);
    CHECK(bb::CheckedAdd(0, kMax, "PE", "sum") == kMax);

    RequireMalformed([] { bb::CheckedAdd(kMax, 1, "PE", "section end"); },
                     "PE: ", {"section end", "overflows"});
    RequireMalformed([] { bb::CheckedAdd(1, kMax, "ELF", "table end"); },
                     "ELF: ", {"table end", "overflows"});
    RequireMalformed([] { bb::CheckedAdd(kMax, kMax, "MSI", "sector end"); },
                     "MSI: ", {"sector end", "overflows"});
}

TEST_CASE("CheckedMultiply around the 64-bit limit", "[BoundedBytes]")
{
    CHECK(bb::CheckedMultiply(0, kMax, "ELF", "product") == 0);
    CHECK(bb::CheckedMultiply(kMax, 0, "ELF", "product") == 0);
    CHECK(bb::CheckedMultiply(1, kMax, "ELF", "product") == kMax);
    CHECK(bb::CheckedMultiply(kMax, 1, "ELF", "product") == kMax);
    CHECK(bb::CheckedMultiply(6, 7, "ELF", "product") == 42);
    CHECK(bb::CheckedMultiply(std::uint64_t{1} << 32, std::uint64_t{1} << 31, "ELF", "product") ==
          (std::uint64_t{1} << 63));

    RequireMalformed([] { bb::CheckedMultiply(2, kMax, "ELF", "program header table size"); },
                     "ELF: ", {"program header table size", "overflows"});
    RequireMalformed([] { bb::CheckedMultiply(kMax, kMax, "MSI", "sector offset"); },
                     "MSI: ", {"sector offset", "overflows"});
    RequireMalformed([] { bb::CheckedMultiply(std::uint64_t{1} << 32, std::uint64_t{1} << 32,
                                              "Mach-O", "slice table size"); },
                     "Mach-O: ", {"slice table size", "overflows"});
    RequireMalformed([] { bb::CheckedMultiply(std::uint64_t{1} << 63, 2, "PE", "area"); },
                     "PE: ", {"area", "overflows"});
}

TEST_CASE("ToSize", "[BoundedBytes]")
{
    CHECK(bb::ToSize(0, "PE", "value") == 0);
    CHECK(bb::ToSize(12345, "PE", "value") == 12345);

    if constexpr(sizeof(std::size_t) < sizeof(std::uint64_t))
    {
        RequireMalformed([] { bb::ToSize(kMax, "PE", "value"); }, "PE: ", {"value", "overflows"});
    }
    else
    {
        CHECK(bb::ToSize(kMax, "PE", "value") == static_cast<std::size_t>(kMax));
    }
}

TEST_CASE("ThrowMalformed builds the message", "[BoundedBytes]")
{
    RequireMalformed([] { bb::ThrowMalformed("Mach-O code signature", "blob index 2 is bad"); },
                     "Mach-O code signature: blob index 2 is bad");
    RequireMalformed([] { bb::ThrowMalformed("MSI", "directory entry 3 is reached twice"); },
                     "MSI: directory entry 3 is reached twice");
}

TEST_CASE("ByteSpan::Read of each width at the edges, both orders", "[BoundedBytes]")
{
    const auto bytes = Sequence(16);
    const bb::ByteSpan span(bytes, "PE");

    SECTION("1 byte")
    {
        CHECK(span.Read<std::uint8_t>(0, bb::ByteOrder::Little, "f") == 0x01);
        CHECK(span.Read<std::uint8_t>(15, bb::ByteOrder::Little, "f") == 0x10);
        CHECK(span.Read<std::uint8_t>(15, bb::ByteOrder::Big, "f") == 0x10);
        RequireMalformed([&] { span.Read<std::uint8_t>(16, bb::ByteOrder::Little, "field"); },
                         "PE: field at offset 16 extends past the end of the file (16 bytes)");
    }

    SECTION("2 bytes")
    {
        CHECK(span.Read<std::uint16_t>(0, bb::ByteOrder::Little, "f") == 0x0201);
        CHECK(span.Read<std::uint16_t>(0, bb::ByteOrder::Big, "f") == 0x0102);
        CHECK(span.Read<std::uint16_t>(14, bb::ByteOrder::Little, "f") == 0x100F);
        CHECK(span.Read<std::uint16_t>(14, bb::ByteOrder::Big, "f") == 0x0F10);
        RequireMalformed([&] { span.Read<std::uint16_t>(15, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "offset 15", "16 bytes"});
        RequireMalformed([&] { span.Read<std::uint16_t>(15, bb::ByteOrder::Big, "field"); },
                         "PE: ", {"field", "offset 15", "16 bytes"});
    }

    SECTION("4 bytes")
    {
        CHECK(span.Read<std::uint32_t>(0, bb::ByteOrder::Little, "f") == 0x04030201u);
        CHECK(span.Read<std::uint32_t>(0, bb::ByteOrder::Big, "f") == 0x01020304u);
        CHECK(span.Read<std::uint32_t>(12, bb::ByteOrder::Little, "f") == 0x100F0E0Du);
        CHECK(span.Read<std::uint32_t>(12, bb::ByteOrder::Big, "f") == 0x0D0E0F10u);
        RequireMalformed([&] { span.Read<std::uint32_t>(13, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "offset 13", "16 bytes"});
        RequireMalformed([&] { span.Read<std::uint32_t>(13, bb::ByteOrder::Big, "field"); },
                         "PE: ", {"field", "offset 13", "16 bytes"});
    }

    SECTION("8 bytes")
    {
        CHECK(span.Read<std::uint64_t>(0, bb::ByteOrder::Little, "f") == 0x0807060504030201ull);
        CHECK(span.Read<std::uint64_t>(0, bb::ByteOrder::Big, "f") == 0x0102030405060708ull);
        CHECK(span.Read<std::uint64_t>(8, bb::ByteOrder::Little, "f") == 0x100F0E0D0C0B0A09ull);
        CHECK(span.Read<std::uint64_t>(8, bb::ByteOrder::Big, "f") == 0x090A0B0C0D0E0F10ull);
        RequireMalformed([&] { span.Read<std::uint64_t>(9, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "offset 9", "16 bytes"});
        RequireMalformed([&] { span.Read<std::uint64_t>(9, bb::ByteOrder::Big, "field"); },
                         "PE: ", {"field", "offset 9", "16 bytes"});
    }

    SECTION("offsets that would wrap")
    {
        RequireMalformed([&] { span.Read<std::uint64_t>(kMax - 7, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "18446744073709551608"});
        RequireMalformed([&] { span.Read<std::uint8_t>(kMax, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "18446744073709551615"});
    }
}

TEST_CASE("ByteSpan::Read on an empty view", "[BoundedBytes]")
{
    const std::vector<std::uint8_t> bytes;
    const bb::ByteSpan span(bytes, "ELF");

    CHECK(span.Size() == 0);
    RequireMalformed([&] { span.Read<std::uint8_t>(0, bb::ByteOrder::Little, "e_ident"); },
                     "ELF: e_ident at offset 0 extends past the end of the file (0 bytes)");
}

TEST_CASE("ByteSpan::Read reads a packed struct", "[BoundedBytes]")
{
#pragma pack(push, 1)
    struct Pair
    {
        std::uint16_t first;
        std::uint32_t second;
    };
#pragma pack(pop)

    const auto bytes = Sequence(8);
    const bb::ByteSpan span(bytes, "PE");

    Pair last = span.Read<Pair>(2, bb::ByteOrder::Little, "pair");
    CHECK(last.first == 0x0403);
    CHECK(last.second == 0x08070605u);

    RequireMalformed([&] { span.Read<Pair>(3, bb::ByteOrder::Little, "pair"); },
                     "PE: ", {"pair", "offset 3", "8 bytes"});
}

TEST_CASE("ByteSpan::Sub", "[BoundedBytes]")
{
    const auto bytes = Sequence(32);
    const bb::ByteSpan span(bytes, "Mach-O");

    SECTION("exact fits")
    {
        bb::ByteSpan whole = span.Sub(0, 32, "whole");
        CHECK(whole.Size() == 32);
        bb::ByteSpan tail = span.Sub(32, 0, "empty tail");
        CHECK(tail.Size() == 0);
        bb::ByteSpan last = span.Sub(31, 1, "last byte");
        CHECK(last.Size() == 1);
        CHECK(last.Data() == bytes.data() + 31);
    }

    SECTION("rejections name the structure and the containing size")
    {
        RequireMalformed([&] { span.Sub(0, 33, "load command table"); },
                         "Mach-O: ", {"load command table", "32 bytes"});
        RequireMalformed([&] { span.Sub(33, 0, "load command table"); },
                         "Mach-O: ", {"load command table", "offset 33", "32 bytes"});
        RequireMalformed([&] { span.Sub(16, 17, "load command table"); },
                         "Mach-O: ", {"load command table", "offset 16", "size 17", "32 bytes"});
        RequireMalformed([&] { span.Sub(kMax - 7, 8, "load command table"); },
                         "Mach-O: ", {"load command table"});
        RequireMalformed([&] { span.Sub(8, kMax, "load command table"); },
                         "Mach-O: ", {"load command table"});
    }

    SECTION("nested views check against their own scope and name it")
    {
        bb::ByteSpan outer = span.Sub(8, 16, "string table");
        CHECK(outer.Size() == 16);
        CHECK(outer.Data() == bytes.data() + 8);

        // Offsets are relative to the sub-view: 12 + 8 fits in the file but
        // not in the 16-byte string table.
        RequireMalformed([&] { outer.Sub(12, 8, "inner part"); },
                         "Mach-O: ", {"inner part", "string table", "16 bytes"});

        bb::ByteSpan inner = outer.Sub(12, 4, "inner part");
        CHECK(inner.Size() == 4);
        CHECK(inner.Data() == bytes.data() + 20);

        // A read past the end of the inner view names it, not the file.
        RequireMalformed([&] { inner.Read<std::uint32_t>(1, bb::ByteOrder::Little, "entry"); },
                         "Mach-O: entry at offset 1 extends past the end of inner part (4 bytes)");
        CHECK(inner.Read<std::uint32_t>(0, bb::ByteOrder::Little, "entry") == 0x18171615u);
    }
}

TEST_CASE("ByteSpan::CString", "[BoundedBytes]")
{
    const std::vector<std::uint8_t> bytes = {'a', 'b', 'c', 0, 'd', 'e', 0, 0, 'x', 'y'};
    const bb::ByteSpan span(bytes, "ELF");

    SECTION("terminated strings")
    {
        CHECK(span.CString(0, 100, "name") == "abc");
        CHECK(span.CString(4, 100, "name") == "de");
        CHECK(span.CString(5, 100, "name") == "e");
    }

    SECTION("empty string")
    {
        CHECK(span.CString(3, 100, "name").empty());
        CHECK(span.CString(7, 100, "name").empty());
    }

    SECTION("terminated at the last byte")
    {
        const std::vector<std::uint8_t> exact = {'o', 'k', 0};
        const bb::ByteSpan exactSpan(exact, "ELF");
        CHECK(exactSpan.CString(0, 100, "name") == "ok");
        CHECK(exactSpan.CString(2, 100, "name").empty());
    }

    SECTION("unterminated")
    {
        RequireMalformed([&] { span.CString(8, 100, "string table entry"); },
                         "ELF: ", {"string table entry", "unterminated"});

        const std::vector<std::uint8_t> none = {'a', 'b', 'c'};
        const bb::ByteSpan noneSpan(none, "ELF");
        RequireMalformed([&] { noneSpan.CString(0, 100, "string table entry"); },
                         "ELF: ", {"string table entry"});
    }

    SECTION("max_length reached")
    {
        // Terminator is at offset 3, so a string of 3 characters needs a
        // limit that covers the terminator.
        RequireMalformed([&] { span.CString(0, 2, "name"); },
                         "ELF: ", {"name"});
        RequireMalformed([&] { span.CString(0, 0, "name"); },
                         "ELF: ", {"name"});
        CHECK(span.CString(0, 4, "name") == "abc");
    }

    SECTION("limit does not reach beyond the view")
    {
        RequireMalformed([&] { span.CString(8, kMax, "name"); },
                         "ELF: ", {"name"});
    }

    SECTION("offset equal to size or beyond")
    {
        RequireMalformed([&] { span.CString(10, 100, "name"); },
                         "ELF: ", {"name", "offset 10"});
        RequireMalformed([&] { span.CString(11, 100, "name"); },
                         "ELF: ", {"name", "offset 11"});
        RequireMalformed([&] { span.CString(kMax, 100, "name"); },
                         "ELF: ", {"name"});
    }

    SECTION("empty view")
    {
        const std::vector<std::uint8_t> empty;
        const bb::ByteSpan emptySpan(empty, "ELF");
        RequireMalformed([&] { emptySpan.CString(0, 100, "name"); },
                         "ELF: ", {"name", "offset 0"});
    }

    SECTION("a sub-view limits the scan to its own bytes")
    {
        bb::ByteSpan part = span.Sub(0, 3, "short table");
        RequireMalformed([&] { part.CString(0, 100, "name"); },
                         "ELF: ", {"name"});
    }
}

TEST_CASE("MutableByteSpan Write at and past the end", "[BoundedBytes]")
{
    std::vector<std::uint8_t> bytes(8, 0);
    bb::MutableByteSpan span(bytes, "PE");

    SECTION("little and big endian writes at the last valid offset")
    {
        span.Write<std::uint32_t>(4, 0x11223344u, bb::ByteOrder::Little, "field");
        CHECK(bytes[4] == 0x44);
        CHECK(bytes[5] == 0x33);
        CHECK(bytes[6] == 0x22);
        CHECK(bytes[7] == 0x11);

        span.Write<std::uint32_t>(4, 0x11223344u, bb::ByteOrder::Big, "field");
        CHECK(bytes[4] == 0x11);
        CHECK(bytes[7] == 0x44);

        span.Write<std::uint8_t>(7, 0xAB, bb::ByteOrder::Little, "field");
        CHECK(bytes[7] == 0xAB);

        span.Write<std::uint64_t>(0, 0x0102030405060708ull, bb::ByteOrder::Big, "field");
        CHECK(bytes[0] == 0x01);
        CHECK(bytes[7] == 0x08);
    }

    SECTION("a write past the end changes nothing")
    {
        const std::vector<std::uint8_t> before = bytes;
        RequireMalformed([&] { span.Write<std::uint32_t>(5, 0xFFFFFFFFu, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "offset 5", "8 bytes"});
        RequireMalformed([&] { span.Write<std::uint8_t>(8, 0xFF, bb::ByteOrder::Little, "field"); },
                         "PE: ", {"field", "offset 8", "8 bytes"});
        RequireMalformed([&] { span.Write<std::uint64_t>(kMax - 7, 1, bb::ByteOrder::Big, "field"); },
                         "PE: ", {"field"});
        CHECK(bytes == before);
    }
}

TEST_CASE("MutableByteSpan Fill at and past the end", "[BoundedBytes]")
{
    std::vector<std::uint8_t> bytes(8, 0);
    bb::MutableByteSpan span(bytes, "MSI");

    span.Fill(6, 2, 0xEE, "padding");
    CHECK(bytes[5] == 0);
    CHECK(bytes[6] == 0xEE);
    CHECK(bytes[7] == 0xEE);

    span.Fill(8, 0, 0x11, "padding");
    span.Fill(0, 0, 0x11, "padding");
    CHECK(bytes[0] == 0);

    const std::vector<std::uint8_t> before = bytes;
    RequireMalformed([&] { span.Fill(7, 2, 0x55, "padding"); },
                     "MSI: ", {"padding", "offset 7", "8 bytes"});
    RequireMalformed([&] { span.Fill(9, 0, 0x55, "padding"); },
                     "MSI: ", {"padding", "offset 9", "8 bytes"});
    // The length is read through a volatile so the compiler cannot see the constant
    // and the call reaches the bound check at run time.
    volatile std::uint64_t hugeLength = kMax;
    const std::uint64_t huge = hugeLength;
    RequireMalformed([&] { span.Fill(0, huge, 0x55, "padding"); },
                     "MSI: ", {"padding"});
    CHECK(bytes == before);
}

TEST_CASE("MutableByteSpan Copy at and past the end", "[BoundedBytes]")
{
    std::vector<std::uint8_t> bytes(8, 0);
    bb::MutableByteSpan span(bytes, "Mach-O");
    const std::vector<std::uint8_t> source = {1, 2, 3, 4};

    span.Copy(4, source, "signature");
    CHECK(bytes[3] == 0);
    CHECK(bytes[4] == 1);
    CHECK(bytes[7] == 4);

    span.Copy(8, std::vector<std::uint8_t>{}, "signature");

    const std::vector<std::uint8_t> before = bytes;
    RequireMalformed([&] { span.Copy(5, source, "signature"); },
                     "Mach-O: ", {"signature", "offset 5", "8 bytes"});
    RequireMalformed([&] { span.Copy(9, std::vector<std::uint8_t>{}, "signature"); },
                     "Mach-O: ", {"signature", "offset 9", "8 bytes"});
    RequireMalformed([&] { span.Copy(kMax, source, "signature"); },
                     "Mach-O: ", {"signature"});
    CHECK(bytes == before);
}

TEST_CASE("Entry point guard", "[BoundedBytes]")
{
    SECTION("returns the callable's value")
    {
        int value = bb::GuardEntryPoint("PE", [] { return 42; });
        CHECK(value == 42);
    }

    SECTION("runs a void callable")
    {
        bool ran = false;
        bb::GuardEntryPoint("PE", [&] { ran = true; });
        CHECK(ran);
    }

    SECTION("a runtime_error passes through unchanged")
    {
        bool thrown = false;
        try
        {
            bb::GuardEntryPoint("ELF", []() -> int { throw std::runtime_error("ELF: program header table is bad"); });
        }
        catch(const std::exception &e)
        {
            thrown = true;
            CHECK(typeid(e) == typeid(std::runtime_error));
            CHECK(std::string(e.what()) == "ELF: program header table is bad");
        }
        CHECK(thrown);
    }

    SECTION("a runtime_error subclass passes through as the same object type")
    {
        struct Special : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };
        CHECK_THROWS_AS(bb::GuardEntryPoint("PE", []() -> int { throw Special("PE: special"); }), Special);
    }

    SECTION("a length_error becomes an internal error")
    {
        RequireMalformed([] { bb::GuardEntryPoint("Mach-O", []() -> int { throw std::length_error("vector"); }); },
                         "Mach-O: internal error (", {"vector", "length_error"});
    }

    SECTION("a bad_alloc becomes an internal error")
    {
        RequireMalformed([] { bb::GuardEntryPoint("MSI", []() -> int { throw std::bad_alloc(); }); },
                         "MSI: internal error (");
    }

    SECTION("any other std::exception becomes an internal error")
    {
        RequireMalformed([] { bb::GuardEntryPoint("Mach-O code signature", []() -> int { throw std::out_of_range("index"); }); },
                         "Mach-O code signature: internal error (", {"index"});
    }
}

// ── The same views over a file read in pages ────────────────────────

namespace {

// Writes the bytes to a file in the scratch directory and returns its path.
std::string WriteScratch(const seedtest::ScratchDir &scratch, const std::string &name,
                         const std::vector<std::uint8_t> &bytes)
{
    const std::string path = scratch.File(name);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("ByteSpan over a paged file: Read of each width at the edges, both orders", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const auto bytes = Sequence(16);
    const bb::PagedFile file(WriteScratch(scratch, "sixteen.bin", bytes));
    const bb::ByteSpan span(file, "PE");
    REQUIRE(span.Size() == 16);

    CHECK(span.Read<std::uint8_t>(0, bb::ByteOrder::Little, "f") == 0x01);
    CHECK(span.Read<std::uint8_t>(15, bb::ByteOrder::Little, "f") == 0x10);
    CHECK(span.Read<std::uint8_t>(15, bb::ByteOrder::Big, "f") == 0x10);
    RequireMalformed([&] { span.Read<std::uint8_t>(16, bb::ByteOrder::Little, "field"); },
                     "PE: field at offset 16 extends past the end of the file (16 bytes)");

    CHECK(span.Read<std::uint16_t>(0, bb::ByteOrder::Little, "f") == 0x0201);
    CHECK(span.Read<std::uint16_t>(0, bb::ByteOrder::Big, "f") == 0x0102);
    CHECK(span.Read<std::uint16_t>(14, bb::ByteOrder::Little, "f") == 0x100F);
    CHECK(span.Read<std::uint16_t>(14, bb::ByteOrder::Big, "f") == 0x0F10);
    RequireMalformed([&] { span.Read<std::uint16_t>(15, bb::ByteOrder::Little, "field"); },
                     "PE: ", {"field", "offset 15", "16 bytes"});

    CHECK(span.Read<std::uint32_t>(0, bb::ByteOrder::Little, "f") == 0x04030201u);
    CHECK(span.Read<std::uint32_t>(0, bb::ByteOrder::Big, "f") == 0x01020304u);
    CHECK(span.Read<std::uint32_t>(12, bb::ByteOrder::Little, "f") == 0x100F0E0Du);
    CHECK(span.Read<std::uint32_t>(12, bb::ByteOrder::Big, "f") == 0x0D0E0F10u);
    RequireMalformed([&] { span.Read<std::uint32_t>(13, bb::ByteOrder::Big, "field"); },
                     "PE: ", {"field", "offset 13", "16 bytes"});

    CHECK(span.Read<std::uint64_t>(0, bb::ByteOrder::Little, "f") == 0x0807060504030201ull);
    CHECK(span.Read<std::uint64_t>(0, bb::ByteOrder::Big, "f") == 0x0102030405060708ull);
    CHECK(span.Read<std::uint64_t>(8, bb::ByteOrder::Little, "f") == 0x100F0E0D0C0B0A09ull);
    CHECK(span.Read<std::uint64_t>(8, bb::ByteOrder::Big, "f") == 0x090A0B0C0D0E0F10ull);
    RequireMalformed([&] { span.Read<std::uint64_t>(9, bb::ByteOrder::Little, "field"); },
                     "PE: ", {"field", "offset 9", "16 bytes"});

    RequireMalformed([&] { span.Read<std::uint64_t>(kMax - 7, bb::ByteOrder::Little, "field"); },
                     "PE: ", {"field", "18446744073709551608"});
    RequireMalformed([&] { span.Read<std::uint8_t>(kMax, bb::ByteOrder::Little, "field"); },
                     "PE: ", {"field", "18446744073709551615"});
}

TEST_CASE("ByteSpan over a paged file: Read on an empty file", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const bb::PagedFile file(WriteScratch(scratch, "empty.bin", {}));
    const bb::ByteSpan span(file, "ELF");

    CHECK(span.Size() == 0);
    RequireMalformed([&] { span.Read<std::uint8_t>(0, bb::ByteOrder::Little, "e_ident"); },
                     "ELF: e_ident at offset 0 extends past the end of the file (0 bytes)");
}

TEST_CASE("ByteSpan over a paged file: Read reads a packed struct", "[BoundedBytes][Paged]")
{
#pragma pack(push, 1)
    struct Pair
    {
        std::uint16_t first;
        std::uint32_t second;
    };
#pragma pack(pop)

    seedtest::ScratchDir scratch;
    const bb::PagedFile file(WriteScratch(scratch, "eight.bin", Sequence(8)));
    const bb::ByteSpan span(file, "PE");

    Pair last = span.Read<Pair>(2, bb::ByteOrder::Little, "pair");
    CHECK(last.first == 0x0403);
    CHECK(last.second == 0x08070605u);

    RequireMalformed([&] { span.Read<Pair>(3, bb::ByteOrder::Little, "pair"); },
                     "PE: ", {"pair", "offset 3", "8 bytes"});
}

TEST_CASE("ByteSpan over a paged file: Sub", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const auto bytes = Sequence(32);
    const bb::PagedFile file(WriteScratch(scratch, "thirtytwo.bin", bytes));
    const bb::ByteSpan span(file, "Mach-O");

    SECTION("exact fits")
    {
        CHECK(span.Sub(0, 32, "whole").Size() == 32);
        CHECK(span.Sub(32, 0, "empty tail").Size() == 0);
        const bb::ByteSpan last = span.Sub(31, 1, "last byte");
        CHECK(last.Size() == 1);
        CHECK(last.Data()[0] == bytes[31]);
        const bb::ByteSpan all = span.Sub(0, 32, "whole");
        CHECK(std::memcmp(all.Data(), bytes.data(), bytes.size()) == 0);
    }

    SECTION("rejections name the structure and the containing size")
    {
        RequireMalformed([&] { span.Sub(0, 33, "load command table"); },
                         "Mach-O: ", {"load command table", "32 bytes"});
        RequireMalformed([&] { span.Sub(33, 0, "load command table"); },
                         "Mach-O: ", {"load command table", "offset 33", "32 bytes"});
        RequireMalformed([&] { span.Sub(16, 17, "load command table"); },
                         "Mach-O: ", {"load command table", "offset 16", "size 17", "32 bytes"});
        RequireMalformed([&] { span.Sub(kMax - 7, 8, "load command table"); },
                         "Mach-O: ", {"load command table"});
        RequireMalformed([&] { span.Sub(8, kMax, "load command table"); },
                         "Mach-O: ", {"load command table"});
    }

    SECTION("nested views check against their own scope and name it")
    {
        const bb::ByteSpan outer = span.Sub(8, 16, "string table");
        CHECK(outer.Size() == 16);
        CHECK(outer.Data()[0] == bytes[8]);

        RequireMalformed([&] { outer.Sub(12, 8, "inner part"); },
                         "Mach-O: ", {"inner part", "string table", "16 bytes"});

        const bb::ByteSpan inner = outer.Sub(12, 4, "inner part");
        CHECK(inner.Size() == 4);
        CHECK(inner.Data()[0] == bytes[20]);

        RequireMalformed([&] { inner.Read<std::uint32_t>(1, bb::ByteOrder::Little, "entry"); },
                         "Mach-O: entry at offset 1 extends past the end of inner part (4 bytes)");
        CHECK(inner.Read<std::uint32_t>(0, bb::ByteOrder::Little, "entry") == 0x18171615u);
    }
}

TEST_CASE("ByteSpan over a paged file: CString", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const std::vector<std::uint8_t> bytes = {'a', 'b', 'c', 0, 'd', 'e', 0, 0, 'x', 'y'};
    const bb::PagedFile file(WriteScratch(scratch, "strings.bin", bytes));
    const bb::ByteSpan span(file, "ELF");

    CHECK(span.CString(0, 100, "name") == "abc");
    CHECK(span.CString(4, 100, "name") == "de");
    CHECK(span.CString(5, 100, "name") == "e");
    CHECK(span.CString(3, 100, "name").empty());
    CHECK(span.CString(7, 100, "name").empty());
    CHECK(span.CString(0, 4, "name") == "abc");

    RequireMalformed([&] { span.CString(8, 100, "string table entry"); },
                     "ELF: ", {"string table entry", "unterminated"});
    RequireMalformed([&] { span.CString(0, 2, "name"); }, "ELF: ", {"name"});
    RequireMalformed([&] { span.CString(0, 0, "name"); }, "ELF: ", {"name"});
    RequireMalformed([&] { span.CString(8, kMax, "name"); }, "ELF: ", {"name"});
    RequireMalformed([&] { span.CString(10, 100, "name"); }, "ELF: ", {"name", "offset 10"});
    RequireMalformed([&] { span.CString(kMax, 100, "name"); }, "ELF: ", {"name"});

    const bb::ByteSpan part = span.Sub(0, 3, "short table");
    RequireMalformed([&] { part.CString(0, 100, "name"); }, "ELF: ", {"name"});
}

TEST_CASE("ByteSpan over a paged file: values and strings across page boundaries", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const std::uint64_t page = bb::PagedFile::kPageSize;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(page * 3 + 100));
    for(std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
    }
    // A string that crosses the first boundary and one that crosses 4096-byte steps.
    const std::string crossing = "a-name-that-crosses-the-first-page-boundary";
    std::memcpy(bytes.data() + page - 10, crossing.c_str(), crossing.size() + 1);
    std::string longer(5000, 'q');
    std::memcpy(bytes.data() + 100, longer.c_str(), longer.size() + 1);

    const bb::PagedFile file(WriteScratch(scratch, "pages.bin", bytes));
    const bb::ByteSpan span(file, "MSI");
    REQUIRE(span.Size() == bytes.size());

    SECTION("a value that spans two pages")
    {
        std::uint64_t expected = 0;
        std::memcpy(&expected, bytes.data() + page - 3, 8);
        CHECK(span.Read<std::uint64_t>(page - 3, bb::ByteOrder::Little, "f") == expected);
        CHECK(span.Read<std::uint32_t>(page - 1, bb::ByteOrder::Big, "f") ==
              ((static_cast<std::uint32_t>(bytes[page - 1]) << 24) | (static_cast<std::uint32_t>(bytes[page]) << 16) |
               (static_cast<std::uint32_t>(bytes[page + 1]) << 8) | bytes[page + 2]));
    }

    SECTION("the last bytes of the last page and one byte past")
    {
        CHECK(span.Read<std::uint8_t>(bytes.size() - 1, bb::ByteOrder::Little, "f") == bytes.back());
        RequireMalformed([&] { span.Read<std::uint8_t>(bytes.size(), bb::ByteOrder::Little, "field"); },
                         "MSI: ", {"field", "offset " + std::to_string(bytes.size())});
    }

    SECTION("strings that cross a page boundary or a scan step")
    {
        CHECK(span.CString(page - 10, 200, "name") == crossing);
        CHECK(span.CString(100, 6000, "name") == longer);
        RequireMalformed([&] { span.CString(100, 4999, "name"); }, "MSI: ", {"name", "longer than 4999"});
    }

    SECTION("a range that spans pages stays valid while others are read")
    {
        const bb::ByteSpan range = span.Sub(page - 8, 16, "range");
        const std::uint8_t *first = range.Data();
        (void)span.Read<std::uint8_t>(page * 2 + 5, bb::ByteOrder::Little, "f");
        (void)span.Sub(page * 2 - 4, 8, "other").Data();
        CHECK(std::memcmp(first, bytes.data() + page - 8, 16) == 0);
    }
}

TEST_CASE("ByteSpan over a paged file reads only the pages it touches", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const std::uint64_t page = bb::PagedFile::kPageSize;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(page * 8), 0);
    bytes[5] = 0x11;
    bytes[page * 5 + 9] = 0x22;
    const std::string path = WriteScratch(scratch, "eight-pages.bin", bytes);

    bb::ResetBytesReadCount();
    const bb::PagedFile file(path);
    const bb::ByteSpan span(file, "PE");
    CHECK(bb::BytesReadCount() == 0);

    // Sub and Size read nothing.
    const bb::ByteSpan part = span.Sub(page * 2, page * 4, "middle");
    CHECK(part.Size() == page * 4);
    CHECK(bb::BytesReadCount() == 0);

    CHECK(span.Read<std::uint8_t>(5, bb::ByteOrder::Little, "f") == 0x11);
    CHECK(bb::BytesReadCount() == page);
    CHECK(span.Read<std::uint8_t>(6, bb::ByteOrder::Little, "f") == 0);
    CHECK(bb::BytesReadCount() == page);

    CHECK(span.Read<std::uint8_t>(page * 5 + 9, bb::ByteOrder::Little, "f") == 0x22);
    CHECK(bb::BytesReadCount() == page * 2);
}

TEST_CASE("ByteSpan over a paged file: a short last page and a file that cannot be read", "[BoundedBytes][Paged]")
{
    seedtest::ScratchDir scratch;
    const std::uint64_t page = bb::PagedFile::kPageSize;

    SECTION("a file that does not end on a page boundary")
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(page + 10), 0xAB);
        bytes.back() = 0xCD;
        bb::ResetBytesReadCount();
        const bb::PagedFile file(WriteScratch(scratch, "short-last.bin", bytes));
        const bb::ByteSpan span(file, "PE");
        CHECK(span.Read<std::uint8_t>(page + 9, bb::ByteOrder::Little, "f") == 0xCD);
        CHECK(bb::BytesReadCount() == 10);
    }

    SECTION("a missing file")
    {
        const std::string path = scratch.File("missing.bin");
        try
        {
            const bb::PagedFile file(path);
            FAIL("opened a missing file");
        }
        catch(const std::exception &e)
        {
            CHECK(typeid(e) == typeid(std::runtime_error));
            CHECK(std::string(e.what()) == "Unable to open file: " + path);
        }
    }

    SECTION("a file cut short after it was opened")
    {
        const std::vector<std::uint8_t> bytes(static_cast<std::size_t>(page * 2), 0x5A);
        const std::string path = WriteScratch(scratch, "cut.bin", bytes);
        const bb::PagedFile file(path);
        const bb::ByteSpan span(file, "PE");
        CHECK(span.Read<std::uint8_t>(0, bb::ByteOrder::Little, "f") == 0x5A);

        std::filesystem::resize_file(path, 100);
        try
        {
            (void)span.Read<std::uint8_t>(page + 1, bb::ByteOrder::Little, "f");
            FAIL("read past the end of a cut file");
        }
        catch(const std::exception &e)
        {
            CHECK(typeid(e) == typeid(std::runtime_error));
            CHECK(std::string(e.what()) == "Unable to read file: " + path);
        }
        // The page read before the cut is still served.
        CHECK(span.Read<std::uint8_t>(1, bb::ByteOrder::Little, "f") == 0x5A);
    }
}
