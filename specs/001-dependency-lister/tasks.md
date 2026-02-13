# Tasks: DependencyLister

**Input**: Design documents from `/specs/001-dependency-lister/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/DependencyLister.hpp, quickstart.md

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[US1/US2/US3]**: Maps to user story from spec.md

---

## Phase 1: Setup

**Purpose**: Project structure and build system updates for new files

- [X] T001 Add DependencyLister.hpp to pkginclude_HEADERS in include/libthe-seed/Makefile.am. Add ElfParser.cpp and PeParser.cpp to libthe_seed_la_SOURCES in src/Makefile.am. Add ElfParser.hpp, PeParser.hpp, and ByteSwap.hpp to noinst_HEADERS in src/Makefile.am (internal headers, not installed).
- [X] T002 Add test_DependencyLister to check_PROGRAMS with sources and link flags in tests/Makefile.am
- [X] T003 Create public header include/libthe-seed/DependencyLister.hpp with DependencyResult struct and DependencyLister class declaration per specs/001-dependency-lister/contracts/DependencyLister.hpp

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Portable binary format struct definitions and shared utilities that both parsers and tests depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 [P] Create src/ElfParser.hpp with portable ELF struct definitions (Elf32_Ehdr, Elf64_Ehdr, Elf32_Phdr, Elf64_Phdr, Elf32_Dyn, Elf64_Dyn) using #pragma pack(push, 1) and cstdint types, plus constants (ELFMAG, ELFCLASS32, ELFCLASS64, ELFDATA2LSB, ELFDATA2MSB, PT_DYNAMIC, PT_LOAD, DT_NULL, DT_NEEDED, DT_STRTAB) and an ElfParser class with a static method to extract dependency names from a file path
- [X] T005 [P] Create src/PeParser.hpp with portable PE struct definitions (IMAGE_DOS_HEADER, IMAGE_FILE_HEADER, IMAGE_OPTIONAL_HEADER32, IMAGE_OPTIONAL_HEADER64, IMAGE_DATA_DIRECTORY, IMAGE_SECTION_HEADER, IMAGE_IMPORT_DESCRIPTOR) using #pragma pack(push, 1) and cstdint types, plus constants (MZ magic 0x5A4D, PE signature 0x00004550, PE32 magic 0x10B, PE32+ magic 0x20B) and a PeParser class with a static method to extract dependency names from a file path
- [X] T006 [P] Create byte-swap utility function template in a shared header (src/ByteSwap.hpp) using C++20 std::endian for host detection and manual byte reversal for uint16_t, uint32_t, uint64_t

**Checkpoint**: All struct definitions and utilities ready — parser implementation can begin

---

## Phase 3: User Story 1 — List Direct Dependencies of a Binary (Priority: P1) 🎯 MVP

**Goal**: Given a single binary file path, extract its direct shared library dependency names (DT_NEEDED for ELF, import table for PE). Return results via DependencyResult with error map for invalid inputs.

**Independent Test**: Pass a known ELF or PE binary with known dependencies, verify the returned list matches.

### Implementation for User Story 1

- [X] T007 [P] [US1] Implement ElfParser::ListDependencies in src/ElfParser.cpp — read ELF header at offset 0, verify magic, detect 32/64-bit class and endianness, iterate program headers to find PT_DYNAMIC, read dynamic entries for DT_NEEDED and DT_STRTAB, convert DT_STRTAB VA to file offset via PT_LOAD segments, read null-terminated library name strings. Return vector<string> of dependency names. Throw or return error string on invalid/truncated input. Handle both Elf32 and Elf64 paths with endianness-aware reads using ByteSwap utility.
- [X] T008 [P] [US1] Implement PeParser::ListDependencies in src/PeParser.cpp — read DOS header at offset 0, verify MZ magic, seek to e_lfanew for PE signature, verify PE signature, read COFF header for NumberOfSections and SizeOfOptionalHeader, read optional header Magic to distinguish PE32 vs PE32+, locate data directory index 1 (import table) at optional header offset 96 (PE32) or 112 (PE32+), convert import table RVA to file offset via section headers, iterate IMAGE_IMPORT_DESCRIPTOR entries until all-zero terminator, convert each Name RVA to file offset and read null-terminated DLL name string. Return vector<string>. PE is always little-endian so swap on big-endian hosts only.
- [X] T009 [US1] Implement DependencyLister::ListDependencies in src/DependencyLister.cpp — for each binary path in binary_paths: open file, read first 4 bytes to detect ELF magic (\x7fELF) vs PE magic (MZ/0x5A4D), dispatch to ElfParser or PeParser, collect returned dependency names into DependencyResult.dependencies (using recorded name as key since no recursive resolution yet — interim behaviour; Phase 4 will resolve to absolute paths with fallback), catch errors and populate DependencyResult.errors for that file, continue processing remaining files. No search path resolution or recursion in this phase.
- [X] T010 [US1] Write Catch2 tests in tests/test_DependencyLister.cpp — test direct ELF dependency extraction using libthe-seed.so itself (known dependency on libpthread), test error case with non-existent file path, test error case with a non-binary file (e.g., /etc/hostname or a temp text file), test that errors for one file do not prevent processing of remaining files, test a statically-linked binary (no DT_NEEDED entries) returns an empty dependency list without error

**Checkpoint**: DependencyLister can extract direct dependencies from any single ELF or PE binary and report errors. Independently testable with `make check`.

---

## Phase 4: User Story 2 — Recursively Build Full Dependency Tree (Priority: P2)

**Goal**: Given multiple binary paths and caller-provided search paths, recursively resolve all transitive dependencies and build the reverse dependency map (library resolved-path → [input binaries that need it]).

**Independent Test**: Provide two binaries sharing a transitive dependency, verify the reverse map lists both binaries under the shared library.

### Implementation for User Story 2

- [X] T011 [US2] Add private helper method to DependencyLister for search path resolution in src/DependencyLister.cpp — given a library name and the search_paths vector, iterate directories in order, construct std::filesystem::path(dir) / library_name, check std::filesystem::exists(), return std::filesystem::canonical() on first match or empty optional if not found
- [X] T012 [US2] Add private recursive helper method to DependencyLister in src/DependencyLister.cpp — accepts a resolved library path, the originating input binary path, search_paths, a shared std::unordered_set<string> visited set, and a reference to DependencyResult. If path is in visited set, return (cycle/redundancy guard). Add to visited. Parse the library (detect ELF/PE, dispatch to parser). For each dependency name: resolve via search paths, add resolved path (or fallback name) → originating binary to dependencies map (avoiding duplicate entries in the vector), then recurse into the resolved path if it exists on disk.
- [X] T013 [US2] Update DependencyLister::ListDependencies in src/DependencyLister.cpp to use recursive resolution — after extracting direct dependencies for each input binary, resolve each dependency name against search_paths, populate the reverse map with resolved paths as keys, recurse into resolved libraries using the recursive helper with shared visited set across all input binaries
- [X] T014 [US2] Add Catch2 tests for recursive resolution in tests/test_DependencyLister.cpp — test transitive dependency resolution using libthe-seed.so (it depends on libpthread which depends on libc, so libc should appear mapped back to libthe-seed.so), test that providing search paths like /usr/lib/x86_64-linux-gnu resolves to absolute canonical paths as map keys, test cycle handling by verifying the resolver completes without hanging on real system libraries (which may have mutual dependencies), test multiple input binaries sharing a dependency both appear in that library's value list, test that an unresolvable library name (not found in any search path) falls back to the recorded name as the map key per FR-007

**Checkpoint**: Full recursive dependency resolution works. Reverse map correctly aggregates multiple binaries. Cycles handled. Independently testable with `make check`.

---

## Phase 5: User Story 3 — Cross-Platform Binary Support (Priority: P3)

**Goal**: Verify and ensure both ELF and PE parsing work on any host platform. Remove the LIEF dependency from the build system.

**Independent Test**: On Linux, parse both an ELF binary and a PE binary and confirm correct dependency extraction from each.

### Implementation for User Story 3

- [X] T015 [P] [US3] Add a minimal PE test binary to tests/ for cross-platform testing — commit a pre-built small valid PE DLL with known imports (e.g., kernel32.dll, msvcrt.dll) as a test fixture file tests/fixtures/test.dll. The binary must be committed to the repository so tests run on any host without a PE toolchain.
- [X] T016 [US3] Add Catch2 tests for PE parsing on Linux in tests/test_DependencyLister.cpp — test that PeParser correctly extracts DLL dependency names from the PE test fixture, test that DependencyLister auto-detects PE format and dispatches correctly, test error case with a truncated/corrupt PE file
- [X] T017 [US3] Remove LIEF dependency from configure.ac — delete the PKG_CHECK_MODULES([LIEF], LIEF) line from the non-Windows AC_CHECK_HEADER block, remove any LIEF_CFLAGS or LIEF_LIBS references if present, verify ./configure and make still succeed without LIEF installed
- [X] T017b [US3] Update tests/test_LibraryLoader.cpp to remove its dependency on libLIEF.so — replace or stub the LIEF-dependent elf_parse call so the test compiles and passes after LIEF removal in T017. Update tests/Makefile.am if LIEF link flags are referenced.

**Checkpoint**: Both formats parse on any platform. LIEF is fully removed from the build. All tests pass with `make check`.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, validation, cleanup

- [X] T018 [P] Add Doxygen comments to all public methods in include/libthe-seed/DependencyLister.hpp (already present in contract, ensure synchronized)
- [X] T019 [P] Validate quickstart.md scenarios — build the library, run the C++ usage example from quickstart.md, verify output matches expected format
- [X] T020 Run full test suite (`make check`) and verify zero LIEF references remain in the codebase (grep -r "LIEF" configure.ac src/ include/ tests/)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Setup (T001-T003) — BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational (T004-T006)
- **US2 (Phase 4)**: Depends on US1 (T007-T010) — extends DependencyLister.cpp
- **US3 (Phase 5)**: Depends on US1 (T007-T008 for parser implementations) — can partially parallel with US2
- **Polish (Phase 6)**: Depends on all stories complete

### Within Each User Story

- Parsers (T007, T008) can be implemented in parallel [P] — different files
- DependencyLister integration (T009, T013) depends on parser implementations
- Tests depend on implementation being complete

### Parallel Opportunities

```text
Phase 2 (all [P]):
  T004 (ElfParser.hpp) ║ T005 (PeParser.hpp) ║ T006 (ByteSwap.hpp)

Phase 3 (parsers [P]):
  T007 (ElfParser.cpp) ║ T008 (PeParser.cpp)
  → T009 (DependencyLister.cpp) — sequential after both parsers
  → T010 (tests) — sequential after T009

Phase 5 (test fixture [P]):
  T015 (PE test fixture) can run in parallel with Phase 4 work
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T003)
2. Complete Phase 2: Foundational (T004-T006)
3. Complete Phase 3: User Story 1 (T007-T010)
4. **STOP and VALIDATE**: `make check` passes, direct dependency extraction works
5. Can be used immediately for non-recursive dependency listing

### Incremental Delivery

1. Setup + Foundational → Build system and type definitions ready
2. Add User Story 1 → Direct dependency extraction works → **MVP**
3. Add User Story 2 → Full recursive resolution → **Primary use case complete**
4. Add User Story 3 → Cross-platform + LIEF removal → **Feature complete**
5. Polish → Documentation and validation → **Ship-ready**

---

## Notes

- Total tasks: 21
- Tasks per user story: US1=4, US2=4, US3=4, Setup=3, Foundational=3, Polish=3
- Parallel opportunities: 7 tasks marked [P]
- Suggested MVP scope: Phase 1 + Phase 2 + Phase 3 (User Story 1 = direct dependency extraction)
- DependencyLister.cpp already exists (empty) and is already in src/Makefile.am SOURCES
- The existing test for LibraryLoader uses LIEF (libLIEF.so) — T017b addresses updating this test before LIEF removal in T017
