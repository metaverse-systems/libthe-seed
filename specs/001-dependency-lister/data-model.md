# Data Model: DependencyLister

**Feature**: 001-dependency-lister  
**Date**: 2026-02-12

---

## Entities

### DependencyResult

The top-level return type from the single stateless API call.

| Field | Type | Description |
|-------|------|-------------|
| `dependencies` | `map<string, vector<string>>` | Reverse dependency map. Key: resolved absolute path of a library (or recorded name if unresolvable). Value: list of input binary paths that depend on it (directly or transitively). |
| `errors` | `map<string, string>` | Error map. Key: input binary path that failed. Value: human-readable error description. |

**Relationships**: Contains exactly one of each sub-map. Both may be populated simultaneously (partial success).

**Validation rules**:
- `dependencies` keys are unique (enforced by map).
- `dependencies` values contain no duplicates for a given key (each project file appears at most once per library).
- `errors` keys are a subset of the original input binary paths.
- A given input binary path appears in either `dependencies` (as a value entry) or `errors` (as a key), never both.

---

### Binary (Input)

An input file to be analyzed. Not a persisted entity — exists only as a file path string in the input list.

| Field | Type | Description |
|-------|------|-------------|
| path | `string` | Absolute filesystem path to the binary file. |

**Validation rules**:
- Must be a non-empty string.
- File must exist and be readable (otherwise → error map entry).
- File must begin with a valid ELF magic (`\x7fELF`) or PE magic (`MZ`) (otherwise → error map entry).

---

### SharedLibrary (Discovered)

A dependency discovered during parsing. Not directly exposed as a standalone entity — exists as a key in the `dependencies` map.

| Field | Type | Description |
|-------|------|-------------|
| recorded_name | `string` | Name as recorded in the binary (DT_NEEDED string or PE import DLL name). |
| resolved_path | `string` (optional) | Absolute canonical filesystem path if found via search path resolution. |

**State transitions**: `recorded_name` → (search path resolution) → `resolved_path`. If resolution fails, `recorded_name` is used as the map key.

**Validation rules**:
- `recorded_name` is always non-empty (read from binary).
- `resolved_path`, when present, is an absolute canonical path (symlinks resolved).

---

## Relationships

```
Input Binary Paths (vector<string>)
        │
        ▼
┌─────────────────────┐
│  DependencyResult   │
├─────────────────────┤
│  dependencies:      │──── key: library resolved_path or recorded_name
│    map<str, vec>    │──── value: [input binary paths that depend on this library]
│                     │
│  errors:            │──── key: input binary path that failed
│    map<str, str>    │──── value: error description
└─────────────────────┘
```

Each input binary's recorded dependencies fan out to SharedLibrary entries, which are then resolved and aggregated into the reverse map. Multiple input binaries may map to the same library key (shared dependencies).

---

## Data Flow

1. **Input**: `vector<string>` binary_paths + `vector<string>` search_paths
2. **Per binary**: Parse → extract recorded dependency names → resolve each against search paths → recurse into resolved libraries
3. **Aggregation**: For each resolved library, append the originating input binary to its value list in the `dependencies` map
4. **Output**: `DependencyResult { dependencies, errors }`
