#pragma once

// Helpers for the dependency lister tests. They build the folders a request
// searches, so that no test reads an installed library:
//
//   - copies of the committed chain (libbaz <- libbar <- libfoo <- appA, appB)
//     in ELF and PE form, from tests/fixtures/dep;
//   - files built from bytes: an ELF with only DT_NEEDED entries, a PE with an
//     import table and a delay-load table, a Mach-O program in either byte
//     order, and a universal file;
//   - links, with a visible SKIPPED line when the file system refuses them;
//   - the number of times the library has read a file.

#include "TestPaths.hpp"

#include "internal/FileIO.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace seedtest::dep
{

using Bytes = std::vector<std::uint8_t>;

namespace detail
{

inline void Put(Bytes &bytes, std::size_t offset, std::uint64_t value, std::size_t width, bool big_endian)
{
    for(std::size_t i = 0; i < width; ++i)
    {
        const std::size_t shift = big_endian ? 8 * (width - 1 - i) : 8 * i;
        bytes[offset + i] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
    }
}

inline void Align(Bytes &bytes, std::size_t alignment)
{
    while(bytes.size() % alignment != 0)
    {
        bytes.push_back(0);
    }
}

inline void AppendText(Bytes &bytes, const std::string &text)
{
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.push_back(0);
}

}

// ---------------------------------------------------------------------------
// Files and folders
// ---------------------------------------------------------------------------

// Writes `bytes` to `folder`/`name` (creating the folder if needed) and
// returns the path.
inline std::filesystem::path WriteFile(const std::filesystem::path &folder, const std::string &name,
                                       const Bytes &bytes)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path path = folder / name;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if(!output)
    {
        FAIL("Cannot write " << path.string());
    }
    return path;
}

// Copies a committed sample from tests/fixtures (for example "dep/libfoo.so")
// into `folder` under `as`, or under its own file name when `as` is empty.
inline std::filesystem::path CopyFixture(const std::filesystem::path &folder, const std::string &sample,
                                         const std::string &as = "")
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path source = seedtest::FixturePath(sample);
    const std::filesystem::path target = folder / (as.empty() ? source.filename().string() : as);
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if(ec)
    {
        FAIL("Cannot copy " << source.string() << " to " << target.string() << ": " << ec.message());
    }
    return target;
}

// Names of the committed ELF chain, libraries first.
inline std::vector<std::string> ElfChainFiles()
{
    return {"libbaz.so", "libbar.so", "libfoo.so", "appA", "appB"};
}

// Names of the committed PE chain, libraries first.
inline std::vector<std::string> PeChainFiles()
{
    return {"libbaz.dll", "libbar.dll", "libfoo.dll", "appA.exe", "appB.exe"};
}

// Copies the ELF chain into `folder` (every file) and returns the folder.
inline std::filesystem::path CopyElfChain(const std::filesystem::path &folder)
{
    for(const std::string &name : ElfChainFiles())
    {
        CopyFixture(folder, "dep/" + name);
    }
    return folder;
}

// Copies the PE chain into `folder` (every file) and returns the folder.
inline std::filesystem::path CopyPeChain(const std::filesystem::path &folder)
{
    for(const std::string &name : PeChainFiles())
    {
        CopyFixture(folder, "dep/" + name);
    }
    return folder;
}

// ---------------------------------------------------------------------------
// Links, permissions, reads
// ---------------------------------------------------------------------------

// Reports a case that cannot run here. The line starts with "SKIPPED:" so it
// stands out in the test log.
inline void Skip(const std::string &reason)
{
    std::printf("SKIPPED: %s\n", reason.c_str());
    std::fflush(stdout);
}

// Creates a symbolic link `link` to `target` (the target text is stored
// as given, so it may be relative or dangling). Returns false, after printing
// a SKIPPED line, when the file system refuses.
inline bool MakeLink(const std::filesystem::path &target, const std::filesystem::path &link)
{
    std::error_code ec;
    std::filesystem::create_symlink(target, link, ec);
    if(ec)
    {
        Skip("cannot create the link " + link.string() + ": " + ec.message());
        return false;
    }
    return true;
}

// Creates a hard link `link` to the file `existing`. Returns false, after
// printing a SKIPPED line, when the file system refuses.
inline bool MakeHardLink(const std::filesystem::path &existing, const std::filesystem::path &link)
{
    std::error_code ec;
    std::filesystem::create_hard_link(existing, link, ec);
    if(ec)
    {
        Skip("cannot create the hard link " + link.string() + ": " + ec.message());
        return false;
    }
    return true;
}

// True when the process runs with the rights of the administrator account, so
// that permission-denied cases cannot fail and must be skipped.
inline bool RunningAsRoot()
{
#if defined(_WIN32)
    return false;
#else
    return ::geteuid() == 0;
#endif
}

// How many times the library has read a whole file since the last reset.
inline std::uint64_t ReadCount()
{
    return ::ReadFileBytesCallCount();
}

inline void ResetReadCount()
{
    ::ResetReadFileBytesCallCount();
}

// ---------------------------------------------------------------------------
// Files built from bytes
// ---------------------------------------------------------------------------

// A 64-bit little-endian ELF shared object whose dynamic section holds one
// DT_NEEDED entry per name (in the order given), a string table and nothing
// else. Names may repeat.
inline Bytes ElfNeeding(const std::vector<std::string> &needed)
{
    constexpr std::size_t kEhdr = 64;
    constexpr std::size_t kPhdr = 56;
    constexpr std::size_t kPhdrCount = 2;
    constexpr std::size_t kDyn = 16;
    constexpr std::uint64_t kBase = 0x400000;

    Bytes strtab;
    strtab.push_back(0);
    std::vector<std::uint64_t> name_offsets;
    for(const std::string &name : needed)
    {
        name_offsets.push_back(strtab.size());
        detail::AppendText(strtab, name);
    }

    const std::size_t dyn_offset = kEhdr + kPhdr * kPhdrCount;
    const std::size_t dyn_count = needed.size() + 3; // DT_STRTAB, DT_STRSZ, DT_NULL
    const std::size_t str_offset = dyn_offset + dyn_count * kDyn;
    const std::size_t total = str_offset + strtab.size();

    Bytes bytes(total, 0);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2; // 64-bit
    bytes[5] = 1; // little-endian
    bytes[6] = 1;
    detail::Put(bytes, 16, 3, 2, false);        // ET_DYN
    detail::Put(bytes, 18, 62, 2, false);       // x86-64
    detail::Put(bytes, 20, 1, 4, false);        // version
    detail::Put(bytes, 32, kEhdr, 8, false);    // program headers follow the header
    detail::Put(bytes, 52, kEhdr, 2, false);    // header size
    detail::Put(bytes, 54, kPhdr, 2, false);    // program header size
    detail::Put(bytes, 56, kPhdrCount, 2, false);

    // PT_LOAD over the whole file.
    std::size_t at = kEhdr;
    detail::Put(bytes, at + 0, 1, 4, false);
    detail::Put(bytes, at + 4, 4, 4, false);
    detail::Put(bytes, at + 8, 0, 8, false);
    detail::Put(bytes, at + 16, kBase, 8, false);
    detail::Put(bytes, at + 24, kBase, 8, false);
    detail::Put(bytes, at + 32, total, 8, false);
    detail::Put(bytes, at + 40, total, 8, false);
    detail::Put(bytes, at + 48, 0x1000, 8, false);

    // PT_DYNAMIC over the dynamic entries.
    at += kPhdr;
    detail::Put(bytes, at + 0, 2, 4, false);
    detail::Put(bytes, at + 4, 4, 4, false);
    detail::Put(bytes, at + 8, dyn_offset, 8, false);
    detail::Put(bytes, at + 16, kBase + dyn_offset, 8, false);
    detail::Put(bytes, at + 24, kBase + dyn_offset, 8, false);
    detail::Put(bytes, at + 32, dyn_count * kDyn, 8, false);
    detail::Put(bytes, at + 40, dyn_count * kDyn, 8, false);
    detail::Put(bytes, at + 48, 8, 8, false);

    at = dyn_offset;
    for(const std::uint64_t offset : name_offsets)
    {
        detail::Put(bytes, at, 1, 8, false); // DT_NEEDED
        detail::Put(bytes, at + 8, offset, 8, false);
        at += kDyn;
    }
    detail::Put(bytes, at, 5, 8, false); // DT_STRTAB
    detail::Put(bytes, at + 8, kBase + str_offset, 8, false);
    at += kDyn;
    detail::Put(bytes, at, 10, 8, false); // DT_STRSZ
    detail::Put(bytes, at + 8, strtab.size(), 8, false);

    std::copy(strtab.begin(), strtab.end(), bytes.begin() + static_cast<std::ptrdiff_t>(str_offset));
    return bytes;
}

// How the name of a delay-load descriptor is stored.
enum class DelayNameForm
{
    Rva,            // Attributes bit 0 set: the name field is an RVA (current linkers)
    VirtualAddress, // Attributes 0: the name field is image base plus RVA (old linkers)
};

// A PE32+ program with an import table naming `imports` and, when
// `delay_imports` is not empty, a delay-load table (data directory 13) naming
// those libraries. Both tables live in one section. The program is never run.
inline Bytes PeImporting(const std::vector<std::string> &imports,
                         const std::vector<std::string> &delay_imports = {},
                         DelayNameForm delay_form = DelayNameForm::Rva)
{
    constexpr std::size_t kHeaders = 0x200;
    constexpr std::uint32_t kSectionRva = 0x1000;
    constexpr std::uint64_t kImageBase = 0x140000000ull;
    constexpr std::size_t kImportDescriptor = 20;
    constexpr std::size_t kDelayDescriptor = 32;
    constexpr std::size_t kOptionalHeader = 112 + 16 * 8;

    // Section content: import descriptors, delay descriptors, names.
    const std::size_t import_bytes = (imports.size() + 1) * kImportDescriptor;
    const std::size_t delay_bytes = delay_imports.empty() ? 0 : (delay_imports.size() + 1) * kDelayDescriptor;
    Bytes section(import_bytes + delay_bytes, 0);
    std::vector<std::uint32_t> import_names;
    std::vector<std::uint32_t> delay_names;
    for(const std::string &name : imports)
    {
        import_names.push_back(kSectionRva + static_cast<std::uint32_t>(section.size()));
        detail::AppendText(section, name);
    }
    for(const std::string &name : delay_imports)
    {
        delay_names.push_back(kSectionRva + static_cast<std::uint32_t>(section.size()));
        detail::AppendText(section, name);
    }
    detail::Align(section, 16);
    const std::uint32_t thunk_rva = kSectionRva + static_cast<std::uint32_t>(section.size());
    section.resize(section.size() + 16, 0);
    detail::Align(section, 0x200);

    for(std::size_t i = 0; i < imports.size(); ++i)
    {
        const std::size_t at = i * kImportDescriptor;
        detail::Put(section, at + 12, import_names[i], 4, false);
        detail::Put(section, at + 16, thunk_rva, 4, false);
    }
    for(std::size_t i = 0; i < delay_imports.size(); ++i)
    {
        const std::size_t at = import_bytes + i * kDelayDescriptor;
        if(delay_form == DelayNameForm::Rva)
        {
            detail::Put(section, at + 0, 1, 4, false);
            detail::Put(section, at + 4, delay_names[i], 4, false);
        }
        else
        {
            detail::Put(section, at + 0, 0, 4, false);
            detail::Put(section, at + 4, kImageBase + delay_names[i], 4, false);
        }
        detail::Put(section, at + 16, thunk_rva, 4, false);
    }

    Bytes bytes(kHeaders, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    detail::Put(bytes, 60, 0x40, 4, false); // e_lfanew
    detail::Put(bytes, 0x40, 0x00004550, 4, false); // "PE\0\0"
    const std::size_t file_header = 0x44;
    detail::Put(bytes, file_header + 0, 0x8664, 2, false);
    detail::Put(bytes, file_header + 2, 1, 2, false); // one section
    detail::Put(bytes, file_header + 16, kOptionalHeader, 2, false);
    detail::Put(bytes, file_header + 18, 0x0022, 2, false);

    const std::size_t optional = file_header + 20;
    detail::Put(bytes, optional + 0, 0x20B, 2, false);
    detail::Put(bytes, optional + 24, kImageBase, 8, false);
    detail::Put(bytes, optional + 32, 0x1000, 4, false); // section alignment
    detail::Put(bytes, optional + 36, 0x200, 4, false);  // file alignment
    detail::Put(bytes, optional + 40, 6, 2, false);      // OS version
    detail::Put(bytes, optional + 48, 6, 2, false);      // subsystem version
    detail::Put(bytes, optional + 56, kSectionRva + 0x1000, 4, false); // size of image
    detail::Put(bytes, optional + 60, kHeaders, 4, false);             // size of headers
    detail::Put(bytes, optional + 68, 3, 2, false);                    // console
    detail::Put(bytes, optional + 108, 16, 4, false);                  // data directories

    const std::size_t directories = optional + 112;
    if(!imports.empty())
    {
        detail::Put(bytes, directories + 8 * 1, kSectionRva, 4, false);
        detail::Put(bytes, directories + 8 * 1 + 4, import_bytes, 4, false);
    }
    if(!delay_imports.empty())
    {
        detail::Put(bytes, directories + 8 * 13, kSectionRva + import_bytes, 4, false);
        detail::Put(bytes, directories + 8 * 13 + 4, delay_bytes, 4, false);
    }

    const std::size_t section_header = optional + kOptionalHeader;
    const char name[8] = {'.', 'i', 'd', 'a', 't', 'a', 0, 0};
    std::copy(name, name + 8, bytes.begin() + static_cast<std::ptrdiff_t>(section_header));
    detail::Put(bytes, section_header + 8, section.size(), 4, false);
    detail::Put(bytes, section_header + 12, kSectionRva, 4, false);
    detail::Put(bytes, section_header + 16, section.size(), 4, false);
    detail::Put(bytes, section_header + 20, kHeaders, 4, false);
    detail::Put(bytes, section_header + 36, 0xC0000040u, 4, false); // initialised data, read, write

    bytes.insert(bytes.end(), section.begin(), section.end());
    return bytes;
}

// The byte order of the fields of a Mach-O image built by MachOReferencing.
enum class MachOFields
{
    // Starts with CF FA ED FE and holds its header and command fields
    // big-endian: the form the library reads (the form of its other tests).
    BigAfterMagic,
    // Starts with CF FA ED FE and holds its fields little-endian, as a real
    // x86-64 or arm64 program does.
    LittleAfterMagic,
};

// Load command kinds that name a library.
inline constexpr std::uint32_t kLoadDylib = 0x0C;
inline constexpr std::uint32_t kLoadWeakDylib = 0x80000018u;
inline constexpr std::uint32_t kReexportDylib = 0x8000001Fu;
inline constexpr std::uint32_t kLazyLoadDylib = 0x20;
inline constexpr std::uint32_t kLoadUpwardDylib = 0x80000023u;

struct MachOReference
{
    std::uint32_t kind;
    std::string name;
};

// A 64-bit Mach-O program with one library reference command per entry and no
// segments. The program is never run.
inline Bytes MachOReferencing(const std::vector<MachOReference> &references,
                              MachOFields fields = MachOFields::BigAfterMagic)
{
    const bool big = fields == MachOFields::BigAfterMagic;
    constexpr std::size_t kHeader = 32;

    Bytes commands;
    std::uint32_t command_count = 0;
    for(const MachOReference &reference : references)
    {
        Bytes command(24, 0);
        detail::Put(command, 0, reference.kind, 4, big);
        detail::Put(command, 8, 24, 4, big); // name offset
        detail::Put(command, 12, 2, 4, big); // timestamp
        detail::Put(command, 16, 0x10000, 4, big);
        detail::Put(command, 20, 0x10000, 4, big);
        detail::AppendText(command, reference.name);
        detail::Align(command, 8);
        detail::Put(command, 4, command.size(), 4, big);
        commands.insert(commands.end(), command.begin(), command.end());
        ++command_count;
    }

    Bytes bytes(kHeader, 0);
    bytes[0] = 0xCF;
    bytes[1] = 0xFA;
    bytes[2] = 0xED;
    bytes[3] = 0xFE;
    detail::Put(bytes, 4, 0x01000007, 4, big); // x86-64
    detail::Put(bytes, 8, 3, 4, big);
    detail::Put(bytes, 12, 2, 4, big); // executable
    detail::Put(bytes, 16, command_count, 4, big);
    detail::Put(bytes, 20, commands.size(), 4, big);
    bytes.insert(bytes.end(), commands.begin(), commands.end());
    return bytes;
}

// A universal (fat) file holding `slices`, each aligned to 16 bytes. The
// header magic is CA FE BA BE. `big_endian_fields` writes the standard
// big-endian fields; false writes little-endian fields, the form the library
// reads.
inline Bytes MachOUniversal(const std::vector<Bytes> &slices, bool big_endian_fields)
{
    const std::size_t table_end = 8 + slices.size() * 20;
    Bytes bytes(table_end, 0);
    bytes[0] = 0xCA;
    bytes[1] = 0xFE;
    bytes[2] = 0xBA;
    bytes[3] = 0xBE;
    detail::Put(bytes, 4, slices.size(), 4, big_endian_fields);

    std::size_t index = 0;
    for(const Bytes &slice : slices)
    {
        detail::Align(bytes, 16);
        const std::size_t at = 8 + index * 20;
        detail::Put(bytes, at + 0, 0x01000007, 4, big_endian_fields);
        detail::Put(bytes, at + 4, 3, 4, big_endian_fields);
        detail::Put(bytes, at + 8, bytes.size(), 4, big_endian_fields);
        detail::Put(bytes, at + 12, slice.size(), 4, big_endian_fields);
        detail::Put(bytes, at + 16, 4, 4, big_endian_fields); // alignment 2^4
        bytes.insert(bytes.end(), slice.begin(), slice.end());
        ++index;
    }
    return bytes;
}

}
