#pragma once

// The test-side checker for Mac programs and signatures, and builders for
// small synthetic images.
//
// Nothing here includes or calls library code: the SHA-256, the byte reads,
// the table walk and the checks are written out again, so that a mistake in
// the library cannot hide a mistake in the check. The checker is the C++
// counterpart of fixtures/check_pages.py; both are calibrated on the signature
// that ld64.lld writes (a different producer from the library).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace machoref {

using Bytes = std::vector<std::uint8_t>;

// ---------------------------------------------------------------------------
// SHA-256, written out here.
// ---------------------------------------------------------------------------

inline Bytes Sha256(const std::uint8_t *data, std::size_t size)
{
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    Bytes message(data, data + size);
    message.push_back(0x80);
    while(message.size() % 64 != 56)
    {
        message.push_back(0);
    }
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8;
    for(int i = 7; i >= 0; --i)
    {
        message.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for(std::size_t block = 0; block < message.size(); block += 64)
    {
        std::uint32_t w[64];
        for(int i = 0; i < 16; ++i)
        {
            const std::uint8_t *p = &message[block + 4 * i];
            w[i] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                   (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
        }
        for(int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for(int i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    Bytes out;
    for(std::uint32_t v : h)
    {
        for(int i = 3; i >= 0; --i)
        {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    return out;
}

inline std::string Hex(const Bytes &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for(std::uint8_t b : bytes)
    {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Reads that never leave the buffer. A read outside it is a failed check, not a
// crash: the functions return false and the caller records a problem.
// ---------------------------------------------------------------------------

inline bool Fits(const Bytes &b, std::uint64_t offset, std::uint64_t length)
{
    return offset <= b.size() && length <= b.size() - offset;
}

inline bool ReadBE(const Bytes &b, std::uint64_t at, int width, std::uint64_t &out)
{
    if(!Fits(b, at, static_cast<std::uint64_t>(width)))
    {
        return false;
    }
    out = 0;
    for(int i = 0; i < width; ++i)
    {
        out = (out << 8) | b[static_cast<std::size_t>(at) + i];
    }
    return true;
}

inline bool ReadLE(const Bytes &b, std::uint64_t at, int width, std::uint64_t &out)
{
    if(!Fits(b, at, static_cast<std::uint64_t>(width)))
    {
        return false;
    }
    out = 0;
    for(int i = width - 1; i >= 0; --i)
    {
        out = (out << 8) | b[static_cast<std::size_t>(at) + i];
    }
    return true;
}

// ---------------------------------------------------------------------------
// The checker.
// ---------------------------------------------------------------------------

struct SliceReport
{
    std::uint64_t cputype = 0;
    std::uint64_t cpusubtype = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t ncmds = 0;
    std::uint64_t sizeofcmds = 0;
    std::uint64_t header_end = 0;
    std::uint64_t walked_commands = 0;
    std::uint64_t signature_commands = 0;
    bool has_signature = false;
    std::uint64_t dataoff = 0;
    std::uint64_t datasize = 0;
    std::uint64_t linkedit_fileoff = 0;
    std::uint64_t linkedit_filesize = 0;
    std::uint64_t linkedit_vmsize = 0;
    std::uint64_t code_limit = 0;
    std::uint64_t n_code_slots = 0;
    std::uint64_t superblob_length = 0;
    std::string identifier;
    std::vector<std::string> page_hashes; // recomputed, hex
    std::vector<bool> page_ok;            // recomputed against the stored one
    bool requirements_ok = true;
    std::vector<std::string> problems;
};

struct FileReport
{
    int form = 0; // 0 thin, 1 universal with 32-bit table, 2 universal with 64-bit table
    std::vector<SliceReport> slices;
    std::vector<std::string> problems; // file level, then every slice's, prefixed "slice N: "

    bool Ok() const
    {
        return this->problems.empty();
    }
};

inline void CheckSlice(const Bytes &file, SliceReport &r)
{
    auto problem = [&](const std::string &text) { r.problems.push_back(text); };
    const Bytes slice(file.begin() + static_cast<std::ptrdiff_t>(r.offset),
                      file.begin() + static_cast<std::ptrdiff_t>(r.offset + r.size));
    std::uint64_t magic = 0;
    if(!ReadBE(slice, 0, 4, magic) || magic != 0xCFFAEDFE)
    {
        problem("not a little-endian 64-bit program");
        return;
    }
    std::uint64_t flags_unused = 0;
    if(!ReadLE(slice, 4, 4, r.cputype) || !ReadLE(slice, 8, 4, r.cpusubtype) ||
       !ReadLE(slice, 16, 4, r.ncmds) || !ReadLE(slice, 20, 4, r.sizeofcmds) ||
       !ReadLE(slice, 24, 4, flags_unused))
    {
        problem("header is truncated");
        return;
    }
    r.header_end = 32 + r.sizeofcmds;
    if(r.header_end > slice.size())
    {
        problem("load commands run past the end of the slice");
        return;
    }
    std::uint64_t at = 32;
    std::uint64_t total = 0;
    bool walk_ok = true;
    for(std::uint64_t i = 0; i < r.ncmds; ++i)
    {
        std::uint64_t cmd = 0, size = 0;
        if(at + 8 > r.header_end || !ReadLE(slice, at, 4, cmd) || !ReadLE(slice, at + 4, 4, size))
        {
            problem("command " + std::to_string(i) + " header is outside the load commands");
            walk_ok = false;
            break;
        }
        if(size == 0)
        {
            problem("command " + std::to_string(i) + " has size zero");
            walk_ok = false;
            break;
        }
        if(size < 8 || size % 4 != 0 || at + size > r.header_end)
        {
            problem("command " + std::to_string(i) + " has a bad size " + std::to_string(size));
            walk_ok = false;
            break;
        }
        ++r.walked_commands;
        total += size;
        if(cmd == 0x19 && size >= 72) // LC_SEGMENT_64
        {
            std::string name(reinterpret_cast<const char *>(&slice[at + 8]), 16);
            name = name.substr(0, name.find('\0'));
            if(name == "__LINKEDIT")
            {
                ReadLE(slice, at + 32, 8, r.linkedit_vmsize);
                ReadLE(slice, at + 40, 8, r.linkedit_fileoff);
                ReadLE(slice, at + 48, 8, r.linkedit_filesize);
            }
        }
        else if(cmd == 0x1D && size == 16) // LC_CODE_SIGNATURE
        {
            ++r.signature_commands;
            ReadLE(slice, at + 8, 4, r.dataoff);
            ReadLE(slice, at + 12, 4, r.datasize);
        }
        at += size;
    }
    if(walk_ok && total != r.sizeofcmds)
    {
        problem("sum of command sizes " + std::to_string(total) + " differs from sizeofcmds " +
                std::to_string(r.sizeofcmds));
    }
    if(r.signature_commands > 1)
    {
        problem(std::to_string(r.signature_commands) + " signature commands");
    }
    if(r.signature_commands == 0)
    {
        return;
    }
    r.has_signature = true;
    if(!Fits(slice, r.dataoff, r.datasize))
    {
        problem("signature data runs past the end of the slice");
        return;
    }
    if(r.dataoff + r.datasize != slice.size())
    {
        problem("signature data does not end at the end of the slice");
    }
    if(r.linkedit_fileoff + r.linkedit_filesize != slice.size())
    {
        problem("__LINKEDIT does not end at the end of the slice");
    }
    std::uint64_t blob_magic = 0, length = 0, count = 0;
    const std::uint64_t base = r.dataoff;
    if(!ReadBE(slice, base, 4, blob_magic) || !ReadBE(slice, base + 4, 4, length) ||
       !ReadBE(slice, base + 8, 4, count) || blob_magic != 0xFADE0CC0)
    {
        problem("no SuperBlob at the signature offset");
        return;
    }
    r.superblob_length = length;
    if(length > r.datasize || length < 12 + 8 * count)
    {
        problem("SuperBlob length " + std::to_string(length) + " is outside its bounds");
        return;
    }
    for(std::uint64_t i = length; i < r.datasize; ++i)
    {
        if(slice[static_cast<std::size_t>(base + i)] != 0)
        {
            problem("bytes after the SuperBlob length are not zero");
            break;
        }
    }
    std::uint64_t cd_off = 0, req_off = 0;
    bool have_cd = false, have_req = false;
    for(std::uint64_t i = 0; i < count; ++i)
    {
        std::uint64_t type = 0, off = 0;
        ReadBE(slice, base + 12 + 8 * i, 4, type);
        ReadBE(slice, base + 16 + 8 * i, 4, off);
        if(type == 0)
        {
            cd_off = off;
            have_cd = true;
        }
        else if(type == 2)
        {
            req_off = off;
            have_req = true;
        }
    }
    if(!have_cd)
    {
        problem("no CodeDirectory slot");
        return;
    }
    const std::uint64_t cd = base + cd_off;
    std::uint64_t cd_magic = 0, hash_off = 0, ident_off = 0, nspecial = 0, nslots = 0, limit = 0;
    std::uint64_t hash_size = 0, hash_type = 0, page_log2 = 0;
    if(!ReadBE(slice, cd, 4, cd_magic) || cd_magic != 0xFADE0C02 ||
       !ReadBE(slice, cd + 16, 4, hash_off) || !ReadBE(slice, cd + 20, 4, ident_off) ||
       !ReadBE(slice, cd + 24, 4, nspecial) || !ReadBE(slice, cd + 28, 4, nslots) ||
       !ReadBE(slice, cd + 32, 4, limit) || !ReadBE(slice, cd + 36, 1, hash_size) ||
       !ReadBE(slice, cd + 37, 1, hash_type) || !ReadBE(slice, cd + 39, 1, page_log2))
    {
        problem("CodeDirectory is damaged");
        return;
    }
    r.code_limit = limit;
    r.n_code_slots = nslots;
    for(std::uint64_t i = cd + ident_off; i < slice.size() && slice[static_cast<std::size_t>(i)] != 0; ++i)
    {
        r.identifier.push_back(static_cast<char>(slice[static_cast<std::size_t>(i)]));
    }
    if(hash_size != 32 || hash_type != 2 || page_log2 == 0 || page_log2 > 20)
    {
        problem("CodeDirectory does not describe SHA-256 pages");
        return;
    }
    const std::uint64_t page = std::uint64_t(1) << page_log2;
    if(limit != r.dataoff)
    {
        problem("codeLimit " + std::to_string(limit) + " differs from the signature offset " +
                std::to_string(r.dataoff));
    }
    if(nslots != (limit + page - 1) / page)
    {
        problem("nCodeSlots " + std::to_string(nslots) + " does not match codeLimit " +
                std::to_string(limit));
    }
    if(limit > slice.size() || !Fits(slice, cd + hash_off, nslots * 32) || hash_off < nspecial * 32)
    {
        problem("hash slots do not fit");
        return;
    }
    for(std::uint64_t i = 0; i < nslots; ++i)
    {
        const std::uint64_t start = i * page;
        const std::uint64_t end = std::min(start + page, limit);
        const Bytes want = Sha256(slice.data() + start, static_cast<std::size_t>(end - start));
        const bool same = std::equal(want.begin(), want.end(),
                                     slice.begin() + static_cast<std::ptrdiff_t>(cd + hash_off + 32 * i));
        r.page_hashes.push_back(Hex(want));
        r.page_ok.push_back(same);
        if(!same)
        {
            problem("page " + std::to_string(i) + " hash does not match");
        }
    }
    if(nspecial >= 2 && have_req)
    {
        std::uint64_t req_len = 0;
        if(ReadBE(slice, base + req_off + 4, 4, req_len) && Fits(slice, base + req_off, req_len))
        {
            const Bytes want = Sha256(slice.data() + base + req_off, static_cast<std::size_t>(req_len));
            r.requirements_ok = std::equal(want.begin(), want.end(),
                                           slice.begin() + static_cast<std::ptrdiff_t>(cd + hash_off - 64));
            if(!r.requirements_ok)
            {
                problem("requirements special slot does not match the requirements blob");
            }
        }
    }
}

// Walks a thin or universal file (either table form) and checks every slice.
inline FileReport CheckFile(const Bytes &file)
{
    FileReport report;
    std::uint64_t magic = 0;
    if(!ReadBE(file, 0, 4, magic))
    {
        report.problems.push_back("file shorter than four bytes");
        return report;
    }
    struct Entry
    {
        std::uint64_t cpu, sub, offset, size;
    };
    std::vector<Entry> entries;
    if(magic == 0xCAFEBABE || magic == 0xCAFEBABF)
    {
        const bool wide = magic == 0xCAFEBABF;
        report.form = wide ? 2 : 1;
        std::uint64_t count = 0;
        if(!ReadBE(file, 4, 4, count) || count == 0 || count > 256 ||
           !Fits(file, 8, count * (wide ? 32 : 20)))
        {
            report.problems.push_back("universal table does not fit");
            return report;
        }
        std::uint64_t previous_end = 8 + count * (wide ? 32 : 20);
        for(std::uint64_t i = 0; i < count; ++i)
        {
            const std::uint64_t at = 8 + i * (wide ? 32 : 20);
            Entry e{};
            ReadBE(file, at, 4, e.cpu);
            ReadBE(file, at + 4, 4, e.sub);
            ReadBE(file, at + 8, wide ? 8 : 4, e.offset);
            ReadBE(file, at + (wide ? 16 : 12), wide ? 8 : 4, e.size);
            if(!Fits(file, e.offset, e.size))
            {
                report.problems.push_back("slice " + std::to_string(i) + " runs past the end of the file");
                return report;
            }
            if(e.offset < previous_end)
            {
                report.problems.push_back("slice " + std::to_string(i) +
                                          " overlaps the table or the previous slice");
                return report;
            }
            previous_end = e.offset + e.size;
            entries.push_back(e);
        }
    }
    else
    {
        entries.push_back({0, 0, 0, file.size()});
    }
    for(std::size_t i = 0; i < entries.size(); ++i)
    {
        SliceReport r;
        r.offset = entries[i].offset;
        r.size = entries[i].size;
        CheckSlice(file, r);
        for(const std::string &p : r.problems)
        {
            report.problems.push_back("slice " + std::to_string(i) + ": " + p);
        }
        report.slices.push_back(std::move(r));
    }
    return report;
}

// ---------------------------------------------------------------------------
// "Bytes outside the named changes equal the original."
// ---------------------------------------------------------------------------

struct Range
{
    std::uint64_t offset;
    std::uint64_t length;
};

// Compares the first min(size) bytes of the two buffers, skipping the byte
// ranges the change is allowed to touch (given in the coordinates of both
// buffers, which agree up to the end of the original's content). Returns the
// offset of the first difference outside the ranges, or -1 when there is none.
inline std::int64_t FirstDifferenceOutside(const Bytes &original, const Bytes &changed,
                                           const std::vector<Range> &allowed,
                                           std::uint64_t compare_length)
{
    const std::uint64_t n = std::min<std::uint64_t>({compare_length, original.size(), changed.size()});
    for(std::uint64_t i = 0; i < n; ++i)
    {
        bool skip = false;
        for(const Range &r : allowed)
        {
            if(i >= r.offset && i - r.offset < r.length)
            {
                skip = true;
                break;
            }
        }
        if(!skip && original[static_cast<std::size_t>(i)] != changed[static_cast<std::size_t>(i)])
        {
            return static_cast<std::int64_t>(i);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Synthetic images. Header only: a magic, the CPU fields and zero padding, in
// the byte order the magic names. They exist for the cases a linker cannot
// produce (big-endian, 32-bit, a Java class file) and are never evidence that
// anything is valid.
// ---------------------------------------------------------------------------

inline void Put32(Bytes &b, std::uint32_t value, bool big)
{
    for(int i = 0; i < 4; ++i)
    {
        const int shift = big ? 24 - 8 * i : 8 * i;
        b.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

// magic_bytes are written in file order (for example {0xFE,0xED,0xFA,0xCF}).
inline Bytes HeaderOnly(std::initializer_list<std::uint8_t> magic_bytes, bool big,
                        std::uint32_t cputype, std::uint32_t cpusubtype, std::size_t header_size)
{
    Bytes b(magic_bytes);
    Put32(b, cputype, big);
    Put32(b, cpusubtype, big);
    b.resize(header_size, 0);
    return b;
}

inline Bytes BigEndian64() // FE ED FA CF, a PowerPC 64-bit header
{
    return HeaderOnly({0xFE, 0xED, 0xFA, 0xCF}, true, 0x01000012, 0, 32);
}

inline Bytes BigEndian32() // FE ED FA CE, a PowerPC header
{
    return HeaderOnly({0xFE, 0xED, 0xFA, 0xCE}, true, 18, 0, 28);
}

inline Bytes Little32() // CE FA ED FE, an i386 header
{
    return HeaderOnly({0xCE, 0xFA, 0xED, 0xFE}, false, 7, 3, 28);
}

// CA FE BA BE followed by minor and major version 0 and 52 (Java 8) and padding.
inline Bytes JavaClassHeader()
{
    Bytes b{0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x34};
    b.resize(2048, 0);
    return b;
}

// A universal file with the given slices at the given alignment (log2), table
// big-endian, in the 32-bit or the 64-bit form. Slices are placed in order.
inline Bytes Universal(const std::vector<Bytes> &slices, bool wide, unsigned align_log2 = 12)
{
    Bytes b;
    const std::uint32_t magic = wide ? 0xCAFEBABF : 0xCAFEBABE;
    Put32(b, magic, true);
    Put32(b, static_cast<std::uint32_t>(slices.size()), true);
    const std::uint64_t entry = wide ? 32 : 20;
    std::uint64_t at = 8 + slices.size() * entry;
    const std::uint64_t align = std::uint64_t(1) << align_log2;
    std::vector<std::uint64_t> offsets;
    for(const Bytes &s : slices)
    {
        at = (at + align - 1) / align * align;
        offsets.push_back(at);
        at += s.size();
    }
    for(std::size_t i = 0; i < slices.size(); ++i)
    {
        std::uint64_t cpu = 0, sub = 0;
        ReadLE(slices[i], 4, 4, cpu);
        ReadLE(slices[i], 8, 4, sub);
        if(!slices[i].empty() && (slices[i][0] == 0xFE))
        {
            ReadBE(slices[i], 4, 4, cpu);
            ReadBE(slices[i], 8, 4, sub);
        }
        Put32(b, static_cast<std::uint32_t>(cpu), true);
        Put32(b, static_cast<std::uint32_t>(sub), true);
        if(wide)
        {
            Put32(b, static_cast<std::uint32_t>(offsets[i] >> 32), true);
            Put32(b, static_cast<std::uint32_t>(offsets[i]), true);
            Put32(b, static_cast<std::uint32_t>(slices[i].size() >> 32), true);
            Put32(b, static_cast<std::uint32_t>(slices[i].size()), true);
            Put32(b, align_log2, true);
            Put32(b, 0, true);
        }
        else
        {
            Put32(b, static_cast<std::uint32_t>(offsets[i]), true);
            Put32(b, static_cast<std::uint32_t>(slices[i].size()), true);
            Put32(b, align_log2, true);
        }
    }
    for(std::size_t i = 0; i < slices.size(); ++i)
    {
        b.resize(static_cast<std::size_t>(offsets[i]), 0);
        b.insert(b.end(), slices[i].begin(), slices[i].end());
    }
    return b;
}

} // namespace machoref
