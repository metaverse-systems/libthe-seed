# Feature Specification: DependencyLister

**Feature Branch**: `001-dependency-lister`  
**Created**: 2026-02-12  
**Status**: Draft  
**Input**: User description: "Create the DependencyLister class. Given a Portable Executable or ELF binary, it should recursively build a list of shared libraries the binary depends on. Something like std::map<librarypath, std::vector<list of project files that depend on it>>. This will be called by a typescript application."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - List Direct Dependencies of a Binary (Priority: P1)

A TypeScript application provides the path to a compiled binary (ELF shared object on Linux, PE DLL on Windows). The DependencyLister inspects that binary and returns a flat list of shared libraries it directly depends on.

**Why this priority**: The core value proposition — being able to read dependency information from a single binary is the foundation everything else builds on. Without this, recursive listing and reverse-mapping are meaningless.

**Independent Test**: Can be fully tested by passing a known binary with a known set of direct dependencies and verifying the returned list matches expectations.

**Acceptance Scenarios**:

1. **Given** a valid ELF shared object that depends on `libpthread.so` and `libdl.so`, **When** the caller requests its dependencies, **Then** the result includes both `libpthread.so` and `libdl.so`.
2. **Given** a valid PE DLL that depends on `kernel32.dll` and `msvcrt.dll`, **When** the caller requests its dependencies, **Then** the result includes both `kernel32.dll` and `msvcrt.dll`.
3. **Given** a file path that does not exist, **When** the caller requests its dependencies, **Then** an appropriate error is returned.
4. **Given** a file that is not a valid ELF or PE binary (e.g., a text file), **When** the caller requests its dependencies, **Then** an appropriate error is returned.

---

### User Story 2 - Recursively Build Full Dependency Tree (Priority: P2)

A TypeScript application provides a list of project binaries. The DependencyLister walks each binary's dependencies recursively — for every shared library found, it also inspects that library's dependencies — building a complete transitive dependency map. The result is a structure mapping each discovered library path to the list of project files that (directly or transitively) depend on it.

**Why this priority**: Recursive resolution is what makes the tool useful beyond a simple ldd wrapper. It enables the caller to understand the full dependency closure for bundling or deployment purposes.

**Independent Test**: Can be tested by providing two binaries that share a common transitive dependency and verifying the reverse map correctly lists both binaries under that shared library.

**Acceptance Scenarios**:

1. **Given** binary A depends on `libfoo.so`, and `libfoo.so` depends on `libbar.so`, **When** the caller requests the dependency map for binary A, **Then** the result maps both `libfoo.so` and `libbar.so` back to binary A.
2. **Given** binaries A and B both depend on `libcommon.so`, **When** the caller requests the dependency map for both binaries, **Then** `libcommon.so` maps to a list containing both A and B.
3. **Given** a circular dependency where `libx.so` depends on `liby.so` and `liby.so` depends on `libx.so`, **When** the caller requests the dependency map, **Then** the system handles the cycle gracefully without infinite recursion and both libraries appear in the result.

---

### User Story 3 - Cross-Platform Binary Support (Priority: P3)

The DependencyLister parses both ELF and PE binary formats on any platform — it can analyze ELF binaries on Windows and PE binaries on Linux — without requiring any third-party binary parsing library. The binary formats are parsed directly from their file structures.

**Why this priority**: Eliminating the LIEF dependency reduces build complexity and external dependency surface. Cross-platform parsing enables scenarios like analyzing deployment artifacts built for a different target OS.

**Independent Test**: Can be tested by providing both ELF and PE test binaries on a single platform and confirming correct dependency extraction from each.

**Acceptance Scenarios**:

1. **Given** the system is built on Linux, **When** the caller provides an ELF binary, **Then** its shared library dependencies are correctly extracted by parsing the ELF format directly.
2. **Given** the system is built on Linux, **When** the caller provides a PE binary, **Then** its DLL dependencies are correctly extracted by parsing the PE format directly.
3. **Given** the system is built on Windows, **When** the caller provides either an ELF or PE binary, **Then** its dependencies are correctly extracted.
4. **Given** the system is built on any supported platform, **When** the build system runs, **Then** no external binary parsing library (such as LIEF) is required.

---

### Edge Cases

- What happens when a dependency listed in the binary cannot be found on the filesystem? The library name is still included in the map but resolution continues for other dependencies.
- What happens when a binary has no shared library dependencies (statically linked)? An empty dependency map is returned for that binary.
- What happens when the same library appears at multiple paths (e.g., `/usr/lib/libfoo.so` and `/usr/local/lib/libfoo.so`)? The path used for resolution is the one found first in the caller-provided search path list.
- What happens when the binary file exists but is unreadable (permissions)? An appropriate error is returned for that file and processing continues for other binaries in the input list.
- What happens when a dependency is itself a valid binary but for a different platform (e.g., a PE file on a Linux system)? Since the DependencyLister parses both formats, it continues recursive resolution regardless of the binary's target platform. However, filesystem-based library search paths will only resolve libraries actually present on the host OS.

## Clarifications

### Session 2026-02-12

- Q: What should the dependency map key be — the recorded library name, the resolved absolute path, or the SONAME? → A: Resolved absolute filesystem path, falling back to the recorded name if the library cannot be found on disk.
- Q: Should the DependencyLister parse only the native platform's binary format, or both ELF and PE on all platforms? → A: Both ELF and PE on all platforms — full cross-platform parsing.
- Q: How should library search paths work for recursive resolution, especially for cross-platform analysis? → A: Caller always provides search paths — no platform defaults at all.
- Q: What shape should the primary API surface take? → A: Single method call — caller provides binary paths and search paths together, gets back the full dependency map.
- Q: How should errors be communicated alongside successful results? → A: Return errors alongside successful results in the same return structure (a separate error map in addition to the dependency map).

## Constraints

- **No LIEF dependency**: The DependencyLister MUST parse ELF and PE binary formats directly from file headers and structures, without relying on the LIEF library or any other third-party binary parsing library. This is an explicit goal of this feature — to remove the existing LIEF dependency from `libthe-seed`.
- **TypeScript interoperability**: The result data structure MUST be consumable by a TypeScript (Node.js) application that calls into this library. The map structure (`std::map<std::string, std::vector<std::string>>`) should serialize naturally to a JSON-compatible object.

## Assumptions

- The DependencyLister supports both ELF and PE binary formats on all platforms. Cross-platform binary analysis (e.g., reading PE files on Linux or ELF files on Windows) is supported.
- Library search paths for recursive resolution are always provided explicitly by the caller. The system does not infer or use any platform-default paths (e.g., `LD_LIBRARY_PATH`, `/usr/lib`, system `PATH`). This gives the TypeScript caller full control over resolution and ensures consistent behavior across native and cross-platform analysis.
- The TypeScript application communicates with this C++ library through an existing binding mechanism already established in the project (matching the existing patterns in `libthe-seed`).
- The DependencyLister exposes a single stateless call — the caller provides all inputs (binary paths + search paths) and receives the full dependency map as the return value. No state is accumulated across calls.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST expose a single stateless operation that accepts a list of binary file paths and a list of search paths, and returns the complete dependency map in one call.
- **FR-002**: System MUST parse the ELF format directly to extract the list of shared library dependencies (DT_NEEDED entries from the `.dynamic` section) on any platform.
- **FR-003**: System MUST parse the PE format directly to extract the list of DLL dependencies (import table entries) on any platform.
- **FR-004**: System MUST recursively resolve dependencies — for each discovered library, it inspects that library's dependencies as well.
- **FR-005**: System MUST accept a list of search paths from the caller to locate dependent libraries on the filesystem for recursive resolution. The system MUST NOT use any platform-default search paths (e.g., `LD_LIBRARY_PATH`, `/usr/lib`, `PATH`) — all search paths are explicitly provided by the caller.
- **FR-006**: System MUST detect and handle circular dependencies gracefully, avoiding infinite recursion.
- **FR-007**: System MUST produce a result mapping each discovered library's resolved absolute filesystem path to the list of input project files that depend on it (directly or transitively), using a structure equivalent to `std::map<std::string, std::vector<std::string>>`. If a library cannot be located on the filesystem, its key falls back to the name as recorded in the binary.
- **FR-008**: System MUST return per-file errors alongside successful results in the same return structure. Invalid inputs (non-existent files, non-binary files, unreadable files) produce an error entry for that file but do not prevent processing of other files.
- **FR-009**: The return structure MUST include both a dependency map (for successfully processed binaries) and an error map (mapping failed file paths to error descriptions), so the caller can handle partial results and errors in one place.
- **FR-010**: System MUST NOT depend on any third-party binary parsing library (e.g., LIEF). All binary format parsing MUST be implemented directly.

### Key Entities

- **DependencyMap**: The primary output — a mapping from each library's resolved absolute filesystem path (or recorded name if unresolvable) to a list of project file paths (strings) that depend on it. Represents the full transitive reverse-dependency graph.
- **Binary**: An input file (ELF or PE format) whose shared library dependencies are to be analyzed. Identified by its filesystem path.
- **SharedLibrary**: A discovered dependency of a binary. Has a name (as recorded in the binary) and optionally a resolved filesystem path (if found during recursive resolution).
- **ErrorMap**: A mapping from file path (string) to error description (string) for binaries that could not be processed. Returned alongside the DependencyMap in the same result structure.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Given a binary with known dependencies, the DependencyLister correctly identifies 100% of direct shared library dependencies.
- **SC-002**: Given a chain of transitive dependencies (A → B → C), all transitive libraries appear in the result mapped back to the originating project file.
- **SC-003**: Circular dependency chains are handled without hanging or crashing — processing completes within the same time bounds as acyclic inputs of similar size.
- **SC-004**: The LIEF library is no longer required at build time or runtime after this feature replaces its usage.
- **SC-005**: A TypeScript application can invoke the DependencyLister and receive a JSON-compatible object representing the dependency map.
