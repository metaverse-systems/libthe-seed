# Research: DependencyLister

**Feature**: 001-dependency-lister  
**Date**: 2026-02-12  
**Purpose**: Resolve all technical unknowns before design

---

## 1. ELF Binary Parsing (DT_NEEDED Extraction)

### Decision: Parse ELF headers directly using portable struct definitions

**Rationale**: The ELF format is well-documented and stable. Extracting DT_NEEDED entries requires reading only 4 structure types (ELF header, program header, dynamic entry, string table). The traversal path is deterministic and small — typically under 4KB of header data for any binary.

**Alternatives considered**:
- LIEF library — rejected (explicit project constraint to remove this dependency)
- libelf — rejected (adds a new external dependency, defeats the purpose)
- Shell out to `ldd` / `readelf` — rejected (not cross-platform, adds process overhead, can't parse PE)

### Traversal Path

1. Read `Elf32_Ehdr` / `Elf64_Ehdr` at offset 0. Verify magic `\x7fELF`. Detect 32/64-bit from `e_ident[EI_CLASS]`, endianness from `e_ident[EI_DATA]`.
2. Seek to `e_phoff`. Iterate `e_phnum` program headers to find `PT_DYNAMIC` (type 2).
3. Read dynamic section entries at `PT_DYNAMIC`'s `p_offset`. Collect all `DT_NEEDED` (tag 1) values and the `DT_STRTAB` (tag 5) virtual address.
4. Convert `DT_STRTAB` VA to file offset by scanning `PT_LOAD` segments: `file_offset = p_offset + (VA - p_vaddr)`.
5. For each `DT_NEEDED` entry, read the null-terminated string at `strtab_file_offset + d_val`.

### Portability

- On Linux: `<elf.h>` provides all struct definitions natively.
- On Windows: No `<elf.h>` exists. Define portable structs using `<cstdint>` with `#pragma pack(push, 1)`.
- Strategy: Always use our own struct definitions for consistency across platforms. Conditionally include `<elf.h>` constants only if available, or define them ourselves.

### 32-bit vs 64-bit

Key differences: address/offset fields are uint32 vs uint64. `Elf64_Phdr` moves `p_flags` to offset 4 (after `p_type`) for alignment. Dynamic entry `d_tag` is int32 vs int64. Strategy: detect class from `e_ident[4]`, then template or branch into the appropriate parsing path.

---

## 2. PE Binary Parsing (Import Table Extraction)

### Decision: Parse PE headers directly using portable struct definitions

**Rationale**: Same reasoning as ELF — the PE import table traversal is well-documented and requires only 5 structure types. The format is always little-endian, simplifying cross-platform parsing.

**Alternatives considered**:
- LIEF library — rejected (same constraint)
- Windows API (`ImageDirectoryEntryToData`) — rejected (Windows-only)
- `objdump` / `dumpbin` — rejected (not portable, process overhead)

### Traversal Path

1. Read `IMAGE_DOS_HEADER` at offset 0. Verify `e_magic == 0x5A4D` ("MZ"). Read `e_lfanew` (uint32 at offset 0x3C) for PE signature location.
2. At `e_lfanew`, verify PE signature `0x00004550` ("PE\0\0").
3. Read COFF header (20 bytes). Extract `NumberOfSections` and `SizeOfOptionalHeader`.
4. Read optional header `Magic`: `0x10B` = PE32, `0x20B` = PE32+. Data directories start at offset 96 (PE32) or 112 (PE32+) within the optional header. Import table is data directory index 1.
5. Convert import table RVA to file offset using section headers. Read `IMAGE_IMPORT_DESCRIPTOR` entries (20 bytes each, terminated by all-zero entry). Extract `Name` RVA from each, convert to file offset, read null-terminated DLL name string.

### Portability

- On Windows: `<windows.h>` provides all struct definitions.
- On Linux: No `<windows.h>`. Define portable structs using `<cstdint>` with `#pragma pack(push, 1)`.
- Strategy: Same as ELF — always use our own struct definitions for cross-platform consistency.

### PE32 vs PE32+

For DLL name extraction, the only difference is the data directory offset within the optional header (96 vs 112). Import descriptors and DLL name strings are format-identical.

---

## 3. Endianness Handling

### Decision: Use C++20 `std::endian` for detection, conditional byte-swap for multi-byte fields

**Rationale**: ELF files can be big-endian or little-endian (encoded in `e_ident[EI_DATA]`). PE files are always little-endian. Cross-platform parsing requires handling the case where file endianness differs from host endianness.

**Approach**:
- Detect host endianness at compile time via `std::endian::native` (`<bit>` header, C++20).
- For ELF: compare `e_ident[EI_DATA]` against host endianness. If different, byte-swap every multi-byte field after reading.
- For PE: always little-endian. On big-endian hosts (rare), swap. On little-endian hosts, no-op.
- Implement a small `swap_bytes<T>()` utility using manual byte reversal (C++23 `std::byteswap` not available in C++20).

---

## 4. Library Search Path Resolution

### Decision: Exact name match against caller-provided search directories, in order

**Rationale**: Per spec clarification, the caller (TypeScript app) provides all search paths explicitly. No platform defaults. This simplifies the resolver to a straightforward directory scan.

**Algorithm**:
1. For each library name from the binary (e.g., `libfoo.so.1` from DT_NEEDED, `kernel32.dll` from PE import):
2. Iterate caller-provided search directories in order.
3. Construct `directory / library_name` using `std::filesystem::path`.
4. If `std::filesystem::exists()` returns true, use `std::filesystem::canonical()` to get the resolved absolute path. Return this as the map key.
5. If no match found in any directory, use the recorded library name as-is for the map key.

**SONAME versioning**: DT_NEEDED records the SONAME (e.g., `libfoo.so.1`), not the unversioned name. The search uses this exact string. Symlinks are followed transparently by `std::filesystem`.

**Deduplication**: `std::filesystem::canonical()` resolves symlinks, so two paths pointing to the same file produce the same canonical key.

---

## 5. Cycle Detection

### Decision: Shared visited set using `std::unordered_set<std::string>`

**Rationale**: Standard graph traversal approach. A single visited set shared across the entire call avoids both infinite recursion and redundant work when multiple input binaries share transitive dependencies.

**Approach**:
- Before recursing into a library's dependencies, check if its canonical path is in the visited set.
- If present: skip recursion but still record the dependency relationship in the map.
- If absent: add to visited set, then recurse.

---

## 6. File I/O Strategy

### Decision: `std::ifstream` in binary mode with random-access seeks

**Rationale**: Portable, no platform-specific APIs needed. Binary headers are small (hundreds of bytes to low KB) — `mmap` would add complexity for no benefit.

**Alternatives considered**:
- `mmap` — rejected (not portable to Windows without `#ifdef`, overkill for small reads)
- C `fopen`/`fread` — rejected (no advantage over `std::ifstream`, less idiomatic C++)

**Validation**: Before every seek/read, validate that `offset + size <= file_size` to prevent reading past EOF. Report truncated/corrupt files via the error map.

---

## 7. Return Structure

### Decision: Single struct containing both dependency map and error map

**Rationale**: Per spec clarification (Q5), errors are returned alongside results — not thrown. This keeps the API stateless and lets the TypeScript caller handle partial results cleanly.

**Structure**:
```
Result {
    dependencies: map<string, vector<string>>   // library path → [project files]
    errors: map<string, string>                 // file path → error description
}
```

---

## 8. Build System Integration

### Decision: Follow existing autotools patterns

**Rationale**: The project uses autotools (autoconf + automake + libtool). New files are added to `src/Makefile.am` sources list and `tests/Makefile.am`.

**Changes required**:
- `src/Makefile.am`: Add `ElfParser.cpp`, `PeParser.cpp` to `libthe_seed_la_SOURCES` (DependencyLister.cpp already listed). Add `../include/libthe-seed/DependencyLister.hpp` to `pkginclude_HEADERS`.
- `tests/Makefile.am`: Add `test_DependencyLister` to `check_PROGRAMS`.
- `configure.ac`: Remove `PKG_CHECK_MODULES([LIEF], LIEF)` from the non-Windows branch once LIEF is fully replaced.
