# Quickstart: DependencyLister

**Feature**: 001-dependency-lister  
**Date**: 2026-02-12

---

## What It Does

`DependencyLister` takes a list of compiled binaries (ELF `.so` / PE `.dll` files) and a list of directories to search, then returns a map of every shared library those binaries depend on — including transitive dependencies — along with which input binaries need each library.

## Build

The DependencyLister is built as part of `libthe-seed`. No new dependencies are required.

```bash
# From repository root
./autogen.sh          # if configure doesn't exist yet
./configure
make
```

After the LIEF removal is complete, the `PKG_CHECK_MODULES([LIEF], LIEF)` line in `configure.ac` should be removed.

## Usage (C++)

```cpp
#include <libthe-seed/DependencyLister.hpp>
#include <iostream>

int main()
{
    DependencyLister lister;

    auto result = lister.ListDependencies(
        // Binaries to analyze
        {"./build/myapp", "./build/libwidget.so"},
        // Directories to search for dependencies
        {"/usr/lib/x86_64-linux-gnu", "/usr/lib", "./build/libs"}
    );

    // Print the reverse dependency map
    for (const auto &[library, dependents] : result.dependencies)
    {
        std::cout << library << " is needed by:" << std::endl;
        for (const auto &dep : dependents)
        {
            std::cout << "  " << dep << std::endl;
        }
    }

    // Print any errors
    for (const auto &[file, error] : result.errors)
    {
        std::cerr << "Error processing " << file << ": " << error << std::endl;
    }
}
```

## Usage (from TypeScript)

The TypeScript application calls into `libthe-seed` through the existing binding mechanism. The `DependencyResult` maps directly to a JSON-compatible structure:

```typescript
interface DependencyResult {
    dependencies: Record<string, string[]>;  // library path -> [binary paths]
    errors: Record<string, string>;          // binary path -> error message
}
```

## Example Output

Given `myapp` depends on `libfoo.so` → `libbar.so`, and `libwidget.so` depends on `libfoo.so`:

```json
{
    "dependencies": {
        "/usr/lib/x86_64-linux-gnu/libfoo.so.1": ["./build/myapp", "./build/libwidget.so"],
        "/usr/lib/x86_64-linux-gnu/libbar.so.2": ["./build/myapp"]
    },
    "errors": {}
}
```

## Running Tests

```bash
make check
# or run directly:
./tests/test_DependencyLister
```
