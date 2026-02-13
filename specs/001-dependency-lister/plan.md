# Implementation Plan: DependencyLister

**Branch**: `001-dependency-lister` | **Date**: 2026-02-12 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-dependency-lister/spec.md`

## Summary

Implement a `DependencyLister` class that parses ELF and PE binary formats directly (no LIEF) to extract shared library dependencies. Given a list of binary file paths and search directories, it recursively resolves transitive dependencies and produces a reverse-dependency map (`library-path → [project files that need it]`) plus an error map for failed inputs. Both ELF and PE parsing work on any platform. The result is consumed by a TypeScript application via the existing `libthe-seed` binding mechanism.

## Technical Context

**Language/Version**: C++20 (matches existing `libthe_seed_la_CXXFLAGS = -std=c++20`)  
**Primary Dependencies**: None new — the entire point is removing LIEF. Uses only standard library + existing project deps (ecs-cpp, pthread)  
**Storage**: N/A — stateless, file reads only  
**Testing**: Catch2 (already configured via `PKG_CHECK_MODULES([CATCH2], catch2)`)  
**Target Platform**: Linux (primary), Windows (cross-compile) — both ELF and PE parsed on any host  
**Project Type**: Single shared library (`libthe-seed.la`) built with autotools/libtool  
**Performance Goals**: N/A — binary headers are small (KB range), performance is not a concern  
**Constraints**: No LIEF dependency. No platform-default search paths. Caller provides all inputs.  
**Scale/Scope**: Single class addition to existing library. ~4 files (header, impl, ELF parser, PE parser + tests).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

No constitution file exists (`.specify/memory/constitution.md` not found). Gate is N/A — no rules to violate. Proceeding.

## Project Structure

### Documentation (this feature)

```text
specs/001-dependency-lister/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── DependencyLister.hpp  # C++ API contract (header)
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
include/libthe-seed/
└── DependencyLister.hpp     # Public header (installed via pkginclude_HEADERS)

src/
├── DependencyLister.cpp     # Main class implementation + recursive resolver
├── ElfParser.cpp            # ELF binary parser (DT_NEEDED extraction)
├── ElfParser.hpp            # ELF parser header with portable struct definitions
├── PeParser.cpp             # PE binary parser (import table extraction)
└── PeParser.hpp             # PE parser header with portable struct definitions

tests/
└── test_DependencyLister.cpp  # Catch2 tests
```

**Structure Decision**: Single shared library pattern, matching existing code. New files follow the same `src/` + `include/libthe-seed/` + `tests/` layout used by `LibraryLoader`, `ComponentLoader`, etc. Internal parsers (`ElfParser`, `PeParser`) are implementation-only headers in `src/` (not installed).

## Complexity Tracking

No constitution violations to justify — constitution does not exist.
