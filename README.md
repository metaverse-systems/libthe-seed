# libthe-seed

libthe-seed is the engine runtime for The Seed, built on libecs-cpp: it loads component and system plugins, resource paks and scenes, lists the libraries a program needs, and signs Windows programs, Mac programs and Windows installers. It installs as the pkg-config package `the-seed`; the reference documentation is at https://metaverse-systems.github.io/libthe-seed/.

## What it offers

| Capability | Public class | Section |
|---|---|---|
| Component plugin loading | `ComponentLoader` | [ComponentLoader](#componentloader) |
| System plugin loading | `SystemLoader` | [SystemLoader](#systemloader) |
| Shared-library loading | `LibraryLoader` | [Public surface](#public-surface) |
| Scene loading | `JSONLoader` | [JSONLoader](#jsonloader) |
| Resource paks | `PakLoader`, `ResourcePak` | [PakLoader](#pakloader) |
| Dependency listing | `DependencyLister` | [Dependency listing](#dependency-listing) |
| Reading Mac programs | `MachOParser` | [Dependency listing](#dependency-listing) |
| Signing Windows programs | `PeSigner` | [Signing Windows programs](#signing-windows-programs) |
| Signing Mac programs | `MachOSigner` | [Signing Mac programs](#signing-mac-programs) |
| Signing Windows installers | `MsiSigner` | [Signing Windows installers](#signing-windows-installers) |
| Safe file replacement | the signers' replace behaviour | [Replacing files](#replacing-files) |
| Load errors | `LoadError` | [Error Handling](#error-handling) |
| Export macro | `THE_SEED_API` (`include/libthe-seed/Export.hpp`) | [Public surface](#public-surface) |

## Requirements

To build and install:

* a C++20 compiler, make, autoconf, automake, libtool and pkg-config
* libecs-cpp 2.1.0 or later, installed where `pkg-config` finds it (https://github.com/metaverse-systems/libecs-cpp)

To run the tests, in addition:

* the `external/Catch2` submodule (see "Running the tests")
* osslsigncode, wine, node, and `clang-query` (in the `clang-tools` package) for the style check; the checks that need a tool skip themselves without it

To generate the documentation: doxygen and graphviz.

For the Windows cross build, a mingw-w64 compiler with POSIX threads, its binutils and headers, and a libecs-cpp built for Windows. On Debian:

```
sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev
```

If `x86_64-w64-mingw32-g++` is set to the `win32` variant, select the POSIX one with `sudo update-alternatives --config x86_64-w64-mingw32-g++` (and the same for `x86_64-w64-mingw32-gcc`).

## Build and install

Native build. If libecs-cpp is installed outside the places `pkg-config` searches, set `PKG_CONFIG_PATH` to its `lib/pkgconfig` folder first.

```
cd libthe-seed
./autogen.sh
./configure --prefix=/usr/local
make
make install
```

Use `sudo make install` only when the prefix is a folder you cannot write; a prefix in your home folder needs no `sudo`. The installation puts these files under the prefix: the headers `include/libthe-seed/*.hpp`, `lib/libthe-seed.a`, `lib/libthe-seed.la`, `lib/libthe-seed.so`, `lib/libthe-seed.so.1`, `lib/libthe-seed.so.1.0.0` and `lib/pkgconfig/the-seed.pc`. `make uninstall` removes them again.

To build outside the source folder, run `./autogen.sh` in the source folder once, then configure from another folder; the source folder stays clean:

```
mkdir ../build && cd ../build
../libthe-seed/configure --prefix=/usr/local
make
```

Windows build, cross-compiled on Linux. Set `PKG_CONFIG_PATH` to the `lib/pkgconfig` folder of the Windows prefix that holds libecs-cpp, and use that prefix for the installation (here `/usr/x86_64-w64-mingw32`):

```
cd libthe-seed
./autogen.sh
PKG_CONFIG_PATH=/usr/x86_64-w64-mingw32/lib/pkgconfig ./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
make
make install
```

The Windows installation has `bin/libthe-seed-1.dll`, `lib/libthe-seed.dll.a`, `lib/libthe-seed.a`, `lib/libthe-seed.la`, the headers and `lib/pkgconfig/the-seed.pc`. Both builds work on a fresh clone made without submodules.

## Configure options

| Option | Values | Default | Effect |
|---|---|---|---|
| `--prefix=DIR` | a folder | `/usr/local` | Where `make install` puts the files. |
| `--host=TRIPLET` | for example `x86_64-w64-mingw32` | the build machine | Cross-compiles for that system. |
| `--enable-tests` | `auto`, `yes`, `no` | `auto` | `auto` enables the tests when `external/Catch2` is present; `yes` stops with an error when it is missing; `no` does not look for it. |
| `--enable-werror` | `no`, `yes` | `no` | `yes` turns every compiler warning in the library, the tests and the test plugins into an error. Warnings are always shown. Maintainers should use `yes`. |
| `--enable-sanitizer` | `no`, `address`, `thread` | `no` | Builds the library and every test with AddressSanitizer and UndefinedBehaviorSanitizer (`address`) or ThreadSanitizer (`thread`). |

Any other value of `--enable-tests`, `--enable-werror` or `--enable-sanitizer` is an error that names the option and the accepted values.

## Using the library

```
pkg-config --cflags --libs the-seed
```

prints the include folder, `-std=c++20`, the library folder and `-lthe-seed` (and the flags of libecs-cpp, which the package requires). In a `configure.ac`, ask for the version you need:

```
PKG_CHECK_MODULES([LIBTHE_SEED], [the-seed >= 0.8.0])
```

To link statically, use `pkg-config --static --cflags --libs the-seed`, which adds the libraries the static library needs (`Libs.private`).

The library's compatibility version is 1: the Linux library is `libthe-seed.so.1` and the Windows library is `libthe-seed-1.dll`. A program or plugin that links the library must be rebuilt when the compatibility version changes. The rule for raising it, and the rule for the package version, are at the top of `NEWS`. A prefix that held an older installation keeps its `libthe-seed.so.0*` or `libthe-seed-0.dll` files next to the new ones; programs built against them keep working with them, and you can delete them when nothing needs them.

## Running the tests

Building and installing the library does not need the test framework. Only the tests do: they need libecs-cpp installed where `pkg-config` finds it, and the Catch2 submodule (`external/Catch2`). Clone with submodules, or fetch the framework into an existing clone:

```
git clone --recurse-submodules git@github.com:metaverse-systems/libthe-seed.git
cd libthe-seed
./autogen.sh && ./configure && make && make check
```

```
git submodule update --init external/Catch2
```

`npm test` does the same from a clone: it runs `./autogen.sh` when `configure` is missing, `./configure` when there is no `Makefile`, and then `make check`.

`./configure --enable-tests=auto` (the default) enables the tests when the framework is present. `--enable-tests=yes` stops with an error when it is missing, and `--enable-tests=no` does not look for it. On a clone without the submodule, `make check` fails with a message saying the tests are not enabled and how to fetch the framework; run `./configure` again afterwards. With `--enable-tests=no` it fails with a message saying the tests were switched off.

`make check` builds every test program, runs them all and fails if any test fails, if a program leaves files behind, or if a sample file under `tests/fixtures/` is missing or changed. Use `make check -j4` to run in parallel. The tests do not depend on the directory they are started from. A build in another folder works the same way. When cross-compiling (for example `--host=x86_64-w64-mingw32`) the test programs are built but not run, a notice says so, and only the scripts run: `tests/check-fixtures.sh`, `tests/check-exports.sh` (reads the DLL's export table), `tests/check-public-headers.sh` (compiles the installed headers with the cross compiler), `tests/check-versions.sh`, `tests/check-install.sh`, `tests/check-consumer.sh`, `tests/check-docs.sh` and `tests/check-readme.sh`; `tests/check-style.sh` skips itself.

To check for memory errors and undefined behaviour, configure a build with the sanitizer variant (AddressSanitizer and UndefinedBehaviorSanitizer for the library and every test):

```
mkdir ../build-asan && cd ../build-asan
../libthe-seed/configure --enable-sanitizer=address
make && make check VERBOSE=1
```

`--enable-sanitizer=no`, or leaving the option out, builds without a sanitizer. `--enable-sanitizer=thread` builds with ThreadSanitizer. Any other value is an error.

Compiler warnings (`-Wall -Wextra`) are always shown. To make them fatal, as the hosted checks do, add `--enable-werror=yes`.

These scripts in `make check` guard the public surface, the installation, the version numbers, this readme and the code style: `tests/check-exports.sh` fails when the shared library exports anything outside `tests/public-symbols.txt`, `tests/check-public-headers.sh` checks that the installed headers are the ones in `tests/public-headers.txt`, compile alone and leak no platform header, `tests/check-install.sh` checks the installed file list, `tests/check-consumer.sh` builds a program against the installed package, `tests/check-versions.sh` checks that the version numbers agree, `tests/check-readme.sh` checks that this readme names real files and options, and `tests/check-style.sh` fails on a member used without `this->` or a member name ending in an underscore. The style check needs `clang-query` (in the `clang-tools` package) and skips itself without it.

### Known gaps

Failures that a defect recorded in the parent project's review of libthe-seed (https://github.com/metaverse-systems/the-seed-dev/blob/main/docs/reviews/libthe-seed-1.md) causes are listed in `tests/known-gaps.txt`, one line per test case, with the finding and the roadmap task that fixes it. A listed failure is reported as `KNOWN GAP:` and does not fail the run; any other failure does. A `STALE KNOWN GAP:` line means a listed test now passes: the change that fixed the defect must remove the entry. A `MALFORMED KNOWN GAP:` line means an entry could not be read or matches no test case, and it fails the run.

### Sample files

The sample executables, DLL, installer and Mach-O files in `tests/fixtures/` are built from sources in `tests/fixtures/src/` and rebuilt only with `tests/fixtures/regenerate.sh`, which rewrites `SHA256SUMS`. `tests/fixtures/PROVENANCE.md` records the tool, version, command, licence and check for every file.

### Fuzzing

`make check` replays the sample files and everything under `tests/fuzz/corpus/<target>/` through the same entry points the fuzzers use (`test_FuzzCorpus`), without mutation. To fuzz, `tests/fuzz/build-fuzzers.sh <build dir>` builds one libFuzzer program per target (`elf`, `pe`, `macho`, `superblob`, `msi`; needs `clang++` with libFuzzer), and `tests/fuzz/run-fuzzers.sh <seconds per target> [seed]` runs them one at a time. Run `tests/fuzz/run-fuzzers.sh 600` before a release. The hosted `fuzz (smoke)` check runs the same targets for 60 seconds each on every pull request and push to `main`. A finding is minimised and added to `tests/fuzz/corpus/<target>/` named by its SHA-1, a named test case is added to the matching `test_Malformed*` program, and then the library is fixed.

### Hosted checks

The workflow in `.github/workflows/ci.yaml` runs `make check` for the plain, the address+undefined, the thread and the plain out-of-tree variants, each configured with `--enable-tests=yes --enable-werror=yes`, on every pull request and on pushes to `main`. It builds libecs-cpp from commit `bd84b5a5bee05bed2006fc9ef9dbbfe3e58f9f7e` (version 2.1.0). Update `LIBECS_CPP_REF` in the workflow when the required libecs-cpp changes. The documentation job builds the reference documentation and runs `tests/check-docs.sh` on every pull request and push; it only reads the repository.

### Documentation

The reference documentation for the public headers is published at https://metaverse-systems.github.io/libthe-seed/ and is not stored in the repository. To build it yourself, install doxygen and graphviz, run `configure`, and run:

```
make doxygen
```

The pages are written to `doxygen/html` in the build directory, which is the source directory for an in-place build. Open `doxygen/html/index.html`. The configuration (`Doxyfile.in`) reads only `include/libthe-seed` and this readme; internal sources and tests are not documented. `make clean` removes the `doxygen` directory. `tests/check-docs.sh` checks the result and skips itself when doxygen is not installed; set `SEED_REQUIRE_DOCS=1` to fail instead.

The workflow `.github/workflows/doxygen.yaml` builds the pages on every push to `main` and publishes them as a Pages artifact. It never commits or pushes to the repository. Publishing needs a one-time setting by the repository owner: in the repository settings, under Pages, set Source to "GitHub Actions", or run `gh api -X PUT repos/metaverse-systems/libthe-seed/pages -f build_type=workflow`. Until then the deploy job reports an error and the workflow still succeeds.

## Dependency listing

`DependencyLister::ListDependencies` builds a reverse map from each library to the input programs that need it, directly or through other libraries.

* Every input is credited with everything it needs, whatever the order of the inputs. Each file is read once. The lists are sorted.
* Keys are the canonical path of the file found in the search folders (links resolve to the real file), or the recorded name when the library is not found. A library that is not found is listed and is not an error.
* Delay-loaded Windows libraries are listed with the others. PE names match regardless of letter case; ELF names match exactly.
* `errors` holds the named inputs that could not be read. `libraryErrors` is an additive field that holds libraries that were found but could not be read; they stay in `dependencies`, and the entry gives the reason and the inputs that need the library.
* Mach-O inputs are now listed (decision of version 0.6.0, replacing the earlier refusal): the dependent library names of every slice of a universal file are read by `MachOParser::ListDependencies`. Names that contain a folder separator (`@rpath/`, `@loader_path/`, `@executable_path/` and absolute install names) are never joined to a search folder and are listed under the recorded name. A shape the parser declines (big-endian or 32-bit) appears in `errors` with the parser's text.
* Release note: packaging output for existing projects can grow because more libraries are now attributed to the programs that need them (including libraries that were previously missing from every program but the first, and delay-loaded Windows libraries). This is the intended correction; check bundle contents and sizes after upgrading. Hard links are not merged: each name keeps its own canonical path.


## Signing Windows programs

`PeSigner` reads and signs 32-bit and 64-bit Windows programs and DLLs (PE files) with an Authenticode signature. It runs on any platform and needs no Windows API. Like the other signers it does not make the signature: the caller builds the PKCS#7 `SignedData` blob outside the library and the library places it in the file.

```cpp
#include <libthe-seed/PeSigner.hpp>

// The fingerprint to sign: SHA-256 of the program without its signature.
auto result = the_seed::PeSigner::ComputeAuthenticodeDigest("app.exe");

// pkcs7_der is the DER-encoded signature that was made over that fingerprint.
the_seed::PeSigner::EmbedSignature("app.exe", pkcs7_der);

if(the_seed::PeSigner::HasEmbeddedSignature("app.exe"))
{
    auto signature = the_seed::PeSigner::ExtractSignature("app.exe");
}

the_seed::PeSigner::StripSignature("app.exe");
```

* `ComputeAuthenticodeDigest` leaves out the CheckSum field, the certificate table entry and any existing certificate, so the fingerprint is the same before and after signing.
* `EmbedSignature` appends the signature as the file's certificate, updates the certificate table entry and recalculates the checksum. A signed program is signed again by stripping first or by calling `EmbedSignature` again; the file is replaced as one step (see "Replacing files").
* `ExtractSignature` returns the stored blob, or nothing when the program is unsigned. `HasEmbeddedSignature` asks the same question without reading the blob.
* A file that is not a PE file, or is malformed, is refused with `std::runtime_error` and left unchanged.

What was checked:

* The fingerprint matches the value osslsigncode 2.14 calculates for the signed sample programs (`tiny.exe` and `test.dll` with 0 to 7 bytes appended), and osslsigncode signs the samples with matching stored and calculated fingerprints.
* A signature embedded and stripped again restores the program, apart from the CheckSum field.
* Malformed programs are refused with the file unchanged.

Nobody has checked a program signed through this library with Microsoft's signtool or WinVerifyTrust, or run a signed program on Windows, because no Windows machine is part of the test setup. Certificate trust is not checked. The tests use made-up signature blobs to check the placement; the cryptography of the blob is the caller's.

## Signing Mac programs

`MachOSigner` signs 64-bit little-endian arm64 and x86-64 programs, alone or in a universal file. Signing is two calls and no other order exists:

1. `PrepareSignature(path, identity, cmsCapacity)` builds the finished layout in memory (the new load command, the header counts, the extended `__LINKEDIT` segment and the region reserved for the signature) and returns the CodeDirectory of each slice. It never writes. The caller signs each CodeDirectory (the CMS is produced outside the library).
2. `CompleteSignature(path, prepared, cmsSignatures)` builds the layout again, requires the CodeDirectories to be the prepared ones, and replaces the file in one step. All slices are signed or none.

* Signing a program that already carries a signature replaces it in place: the one signature command is updated, never duplicated or left with size zero, and the file does not grow. A signature followed by other data is refused. `StripSignature(path)` removes a signature.
* A program without room between its load commands and its first section for the 16-byte signature command is refused and left unchanged. The message gives the bytes needed and available. Relink with extra header space, for example `-headerpad 0x20`.
* Big-endian and 32-bit programs, and a universal file with any such slice, are refused with a message that names the kind; the file is left unchanged.
* `ComputeCodeDirectory` and `EmbedSignature` are removed: removed because hashing before the file is laid out cannot yield a valid signature; use PrepareSignature and CompleteSignature. `BuildSuperBlob` stays as a formatting helper.

What was checked:

* Page fingerprints in a signature produced by this library match the finished file; this is checked by an independent checker and by values recorded from Python's hashlib.
* The checker accepts the ad-hoc signature written by ld64.lld, a different producer.
* The structure of signed files parses with llvm-otool, llvm-objdump and llvm-lipo.
* Signing again replaces the signature in place and does not grow the file; a signature that is larger or smaller is accommodated.
* A program without room for the signature command is refused and left unchanged.

Nobody has checked a signature produced by this library with the platform's own verifier (`codesign --verify`) or by running a signed program on a Mac. What was checked is described above. The CMS part of the signature is the minimal one the-seed has always produced and is not known to be accepted by Apple's tools. Programs signed by earlier versions of the-seed are malformed after re-signing and may need to be rebuilt from their unsigned originals.

## Signing Windows installers

`MsiSigner` reads and signs Windows Installer packages (`.msi`, OLE compound documents).

* `ComputeAuthenticodeDigest` returns the SHA-256 fingerprint by the rule osslsigncode uses. It is the same before signing, after signing and after the signature is removed. It throws on a package that cannot be fully read, on a package that holds two entries with one name in a storage, and on storages nested deeper than 32 levels.
* `EmbedSignature(path, blob, require_matching_digest = false)` rebuilds the whole package in one canonical layout on every call and replaces the file in one step. The blob is stored in the mini stream when it is smaller than the header's cut-off (4,096 bytes) and in ordinary sectors otherwise, and its directory entry is placed where the format's ordering puts it, so a reader that searches by name finds it. `\005MsiDigitalSignatureEx` is dropped and never created. Every other storage and stream keeps its name, bytes, class identifier, state bits and times. The same package and blob always give the same bytes, and signing again does not grow the file. An empty blob is refused. With `require_matching_digest` the blob must hold a SHA-256 fingerprint equal to the fingerprint of the package that is written, otherwise the call is refused and the file is unchanged.
* `StripSignature(path)` now returns `bool`: `true` when `\005DigitalSignature` or `\005MsiDigitalSignatureEx` was removed, `false` when neither existed (the file is then untouched, byte for byte). It never reads the old signature, so a damaged one can be removed.
* `CheckSignature(path)` returns one of four states: `None` (no signature stream, or an empty one), `Matches` (readable, and its stored fingerprint equals the package's), `Mismatch` (readable, fingerprint differs) and `Unreadable` (present but damaged, not a signature structure, or not SHA-256), with the stored and computed fingerprints and a short reason. A bad signature is a state, not an exception. `Matches` compares fingerprints only; it does not say the signature is valid.
* A malformed package, an unsupported shape (sector size other than 512 or 4096, duplicate names in a storage, more sectors than the format allows) or a size limit is refused with the file unchanged.
* Released versions up to 0.6.0 wrote installers that only look signed: the signature was placed in a way that osslsigncode and Wine could not read, and the fingerprint differed from the one osslsigncode calculates. Packages signed by those versions are repaired when signed again, because every operation rebuilds the package; re-sign from the unsigned original when you have it.

What was checked:

The fingerprint and the package structure written by libthe-seed were compared with osslsigncode 2.14, with an independent reader written for the tests, and (for stream lookup by name) with Wine 10's structured-storage implementation. They were not checked with Microsoft's signtool, WinVerifyTrust, the Windows Installer service or Microsoft's own storage code, because no Windows machine is part of the test setup. Certificate trust is not checked. Installers signed by earlier versions of the-seed may be damaged; re-sign them from the unsigned original when you have it.

The signature container that the-seed builds has no signed attributes and its cryptography is not checked by this library, so standard verifiers may reject it.

## Replacing files

`PeSigner`, `MachOSigner` and `MsiSigner` write their result by replacing the file at the given path.

* The file is replaced as one step: after a failure or a crash or kill of the process it holds the complete old or the complete new content, and no working file remains when the call returns, whether it succeeded or failed.
* Permissions are preserved: Linux mode bits (and owner and group where the account may), Windows file attributes. Extended attributes, ACLs and hard-link identity are not preserved.
* Data is flushed to storage before the replacement and the folder is flushed after it, where the platform supports it. This is a request: it protects against loss of power only on storage that honours it (best effort on network shares and some removable media), and is not a guarantee.
* A process killed during the replacement can leave a working file named `.<name>.seedtmp.<pid>.<hex>` in the same folder. It is never reused and can be deleted.
* A link is followed: the file it points to is replaced and the link is kept. Read-only files are refused.
* `PeSigner`: the fingerprint of an unsigned program includes the zero padding that embedding adds, so it is the same before and after embedding. Stripping removes up to seven trailing zero bytes of padding, never below the end of the last section, so a program whose last byte is not zero is restored to its original length and content, except that the CheckSum field holds the calculated checksum (embedding and stripping both recalculate it); one that ends in zeros may lose up to seven when its length was a multiple of eight.
* The tests of the replacement run the PE, Mach-O and MSI signers.


## Public surface

The library exports exactly the classes below, each in the `the_seed` namespace and each installed as `<libthe-seed/Name.hpp>`. Everything else in the shared library is internal (`the_seed::internal`) and is neither installed nor exported. Including a public header introduces no name outside `the_seed`, except the macro `THE_SEED_API`.

| Public name | Header | Nested public names |
|---|---|---|
| `the_seed::ComponentLoader` | `<libthe-seed/ComponentLoader.hpp>` | `ComponentCreator` |
| `the_seed::SystemLoader` | `<libthe-seed/SystemLoader.hpp>` | `SystemCreator` |
| `the_seed::JSONLoader` | `<libthe-seed/JSONLoader.hpp>` | |
| `the_seed::PakLoader` | `<libthe-seed/PakLoader.hpp>` | |
| `the_seed::ResourcePak` | `<libthe-seed/ResourcePak.hpp>` | |
| `the_seed::LoadError` | `<libthe-seed/LoadError.hpp>` | `Reason`, `LocationState`, `Location` |
| `the_seed::LibraryLoader` | `<libthe-seed/LibraryLoader.hpp>` | |
| `the_seed::DependencyLister`, `DependencyResult`, `LibraryError` | `<libthe-seed/DependencyLister.hpp>` | |
| `the_seed::MachOParser` | `<libthe-seed/MachOParser.hpp>` | `Format`, `ArchSlice` |
| `the_seed::PeSigner` | `<libthe-seed/PeSigner.hpp>` | `DigestResult` |
| `the_seed::MachOSigner` | `<libthe-seed/MachOSigner.hpp>` | `PreparedSlice`, `PreparedSignature` |
| `the_seed::MsiSigner` | `<libthe-seed/MsiSigner.hpp>` | `DigestResult`, `SignatureState`, `SignatureCheck` |

`<libthe-seed/Export.hpp>` defines `THE_SEED_API`, which marks these classes as exported. The tests check the list: `tests/public-symbols.txt` names every allowed exported symbol and `tests/check-exports.sh` fails when the library exports anything else. See "Migrating from earlier versions" if your code used the former global names.

## Usage

All loaders are instance-based classes. Each instance owns its own search locations and caches, and is safe for concurrent use from multiple threads. Every failure to load a plugin, a resource pak or a scene is a `LoadError`, which is a `std::runtime_error`.

### Lifetime

No teardown order is required. Every plugin a loader opens is pinned, so components, systems, creators returned by `Get` and scene contents stay valid and destructible after their loader is destroyed, in any order. A plugin's code stays loaded until the process ends; it is never unloaded, so a rebuilt plugin is picked up only by a new process. Programs built against an earlier version must be rebuilt against the new headers (the private members of every loader changed).

### Where plugins are searched

The same rules apply to `ComponentLoader`, `SystemLoader` and `PakLoader`:

* Locations added with `PathAdd` are searched first, in the order they were added. A relative location is resolved against the working directory at the time of the load. A location added after a load applies to the next load.
* The first location that holds the file decides. If that file cannot be loaded, the load fails with the reason and the later locations are not tried; a damaged file is never skipped in favour of another copy.
* The working directory is never searched. To load from it, add it: `loader.PathAdd(".")`.
* The development locations (`../../<library>/src/.libs` and `../node_modules/<org>/<library>/src/.libs` relative to the working directory, and the matching pak folders `../../<library>` and `../node_modules/<org>/<library>`) are searched after the configured ones. They exist for working inside a source tree, where sibling projects' build output is found without installing it.
* The file is opened by its absolute path. The plugin file is `lib<name>.so`, `lib<name>-0.dll` or `lib<name>.dylib`; a pak is `<name>.pak`.
* A name is `library` or `org/library`. Empty parts, `.` or `..` parts, backslashes, `:`, control characters, a leading `/` and more than one `/` are refused with `LoadError::Reason::InvalidName` before any file is touched.
* `SearchPathsGet(name)` returns the ordered locations a load of `name` would search, without touching the file system.

### ComponentLoader

Dynamically loads shared libraries that provide ECS components.

```cpp
#include <libthe-seed/ComponentLoader.hpp>

the_seed::ComponentLoader loader;
loader.PathAdd("/usr/lib/the-seed/components");

auto component = loader.Create("org/my-component");
// Returns std::unique_ptr<ecs::Component>

// Pass initialization data to the component factory
auto component2 = loader.Create("org/my-component", &init_data);

// Get the raw factory function pointer
auto creator = loader.Get("org/my-component");
```

### SystemLoader

Dynamically loads shared libraries that provide ECS systems. Same API pattern as ComponentLoader.

```cpp
#include <libthe-seed/SystemLoader.hpp>

the_seed::SystemLoader loader;
loader.PathAdd("/usr/lib/the-seed/systems");

auto system = loader.Create("org/my-system");
// Returns std::unique_ptr<ecs::System>
```

### Development locations

Working inside a source tree, the loaders find the build output of sibling libraries after the configured locations. `SearchPathsGet` shows the full order:

```cpp
#include <libthe-seed/ComponentLoader.hpp>

the_seed::ComponentLoader loader;
loader.PathAdd("/usr/lib/the-seed/components");

for(const auto &location : loader.SearchPathsGet("org/my-component"))
{
    std::cout << location << "\n";
}
```

### PakLoader

Loads resource paks (bundled asset files).

```cpp
#include <libthe-seed/PakLoader.hpp>

the_seed::PakLoader loader;
loader.PathAdd("/usr/share/the-seed/paks");

// Load all resources from a pak
auto resources = loader.Load("org/my-pak");
// Returns std::unordered_map<std::string, std::shared_ptr<ecs::Resource>>

// Load only specific resources
auto filtered = loader.Load("org/my-pak", {"texture1", "mesh2"});
```

A request reads what it is asked for and no more:

* `Load(name)` reads every resource of the pak, once, straight into its own `ecs::Resource`.
* `Load(name, names)` reads only the resources named. A name listed twice is returned once. An empty list returns an empty map. If any requested name is not in the pak, the whole request fails with `LoadError::Reason::ResourceMissing`, nothing is returned and no resource is read; `MissingGet()` lists every missing name, in request order.
* The pak's description (the list of resource names and sizes, the first line of the file) is read and checked once for each version of the file, then remembered by the loader for the loader's life. Only the description is remembered: no resource bytes and no open file are kept. Every request compares the file it finds with what was remembered (the file's identity, size and modification time) and reads the description again if they differ, so a pak that was rebuilt, replaced or restored is picked up by the next request. A pak that fails is not remembered: the next request examines the file again.
* This differs from plugins on purpose. A plugin stays loaded until the process ends, so a rebuilt plugin is picked up only by a new process. A pak is read afresh by every request, so a rebuilt pak is picked up by the next one.
* The file is opened for each request and closed before the request returns. Another program may replace or delete the pak at any time, on Linux and on Windows. A request sees one version of the file: the one it opened.

The map can be handed to a container as it is (`ecs::Container::Resources` shares the resources, no copy). A request that names a resource the pak lacks is told apart from a damaged pak:

```cpp
the_seed::PakLoader paks;
paks.PathAdd(install_dir + "/paks");

try
{
    auto some = paks.Load("org/art", {"logo", "click"});   // reads the description once per file version,
                                                           // then only these two resources
    world->Resources(some);                                // shared, no copy (libecs-cpp)
}
catch(const the_seed::LoadError &e)
{
    if(e.ReasonGet() == the_seed::LoadError::Reason::ResourceMissing)
    {
        // e.MissingGet() lists the requested names the pak lacks; nothing was returned
    }
    else if(e.ReasonGet() == the_seed::LoadError::Reason::NotLoadable)
    {
        // e.DetailGet() starts with "damaged:" for a broken pak, else the system's reason
    }
}
```

`ResourcePak` reads one pak file by path. Its constructor opens the file, checks the description and remembers it; no resource is read until `Load` or `LoadAll` is called, and `ResourceNames()` answers from the remembered description without touching the file. Like `PakLoader`, it opens the file for each read, checks it against what it remembered, and reads only the resource asked for. `LoadAll(container)` reads every resource of one version of the file and then adds them: if anything fails, nothing is added. Its failures are `LoadError` too.

```cpp
the_seed::ResourcePak pak(install_dir + "/paks/art.pak");
for(const auto &name : pak.ResourceNames())
{
    ecs::Resource resource = pak.Load(name);
    std::cout << name << ": " << resource.Data.size() << " bytes\n";
}
```

#### Resource pak format

A pak is one file: a description line, then the resources one after the other.

```text
offset 0                     description line (UTF-8 JSON, no raw newline)
offset L-1                   '\n'
offset L = headerSize        bytes of resources[0]
offset L + size[0]           bytes of resources[1]
...
offset L + sum(size)         end of file
```

`the-seed resource-pak build` writes the description with `JSON.stringify`, so it has no spaces and the keys are in the order `name`, `headerSize`, `resources`:

```json
{"name":"@scope/pak","headerSize":"0000000123","resources":[{"name":"logo","size":1024},{"name":"click","size":88,"attributes":{"kind":"sound"}}]}
```

* `headerSize` is a string of digits, zero-padded to 10, equal to the length of the line plus one. For an ASCII line this is the byte offset of the first resource.
* `size` is the size of the resource's file when `the-seed resource-pak add` ran. Names are unique.

The reader checks every rule below and reports the first one that is broken, in this order, as a damaged pak (`NotLoadable`, with a detail that starts with `damaged:`):

| Rule | Text after `damaged: ` |
|---|---|
| The file holds a `\n` within its first 64 MiB | `no description line (no newline in the first N bytes)`, `description line is longer than 67108864 bytes` |
| The line is valid JSON nested no deeper than 64 levels | `description is not valid JSON: ...`, `description is nested deeper than 64 levels` |
| The top level is an object | `description is not a JSON object` |
| `headerSize` is a string of 1 to 20 ASCII digits that fits 64 bits | `"headerSize" is missing`, `"headerSize" is not a string of decimal digits`, `"headerSize" is out of range` |
| `headerSize` is the offset of the first `\n` plus one | `stated description length N does not match the description's end at byte M` |
| `resources` is an array | `"resources" is missing`, `"resources" is not an array` |
| Each resource is an object with a string `name` | `resource K is not an object`, `resource K has no "name"`, `resource K "name" is not a string` |
| Each `size` is an integer of 0 or more that fits 64 bits | `resource "x" has no "size"`, `resource "x" "size" is not a non-negative integer` |
| Names are unique | `resource "x" appears more than once` |
| Running offsets do not overflow and each size fits in memory | `size of resource "x" is out of range` |
| `headerSize` plus the sizes is the size of the file | `the description claims N bytes but the file holds M`, `D bytes of unexplained data after the last resource` |

The top-level `name`, `attributes`, unknown keys, key order and the width of the zero padding are not checked. An empty pak (`"resources": []` and a file of exactly `headerSize` bytes) and an empty resource (`size` 0) are valid.

A file that becomes shorter than its description while a request reads it is reported the same way (`damaged: file is shorter than its description: ...`).

Two things the writer can produce are refused as damaged, and both are fixed when the pak is made, not when it is read:

* A non-ASCII character in a resource name. The writer counts the line in UTF-16 units, not bytes, so `headerSize` is wrong. Use ASCII names.
* A resource file that changed size between `add` and `build`. The sizes in the description no longer add up to the file. Run `add` again after changing a file, then `build`.

### JSONLoader

Parses JSON scene files into an ECS container (made by an `ecs::Manager`), using a ComponentLoader to instantiate components.

```cpp
#include <libthe-seed/JSONLoader.hpp>
#include <libthe-seed/ComponentLoader.hpp>

ecs::Manager manager;
ecs::Container *container = manager.Container("world");
the_seed::ComponentLoader comp_loader;
comp_loader.PathAdd("/usr/lib/the-seed/components");

the_seed::JSONLoader json(container, comp_loader);
json.FileParse("scene.json");
// The container, json and comp_loader may be destroyed in any order.
// Or parse from a string:
// json.StringParse(json_string);
```

A scene has an `entities` array; each entity has a non-empty `Handle` string and a `Components` object whose keys are component names and whose values are the configuration passed to the component. An explicit empty `entities` array is valid and adds nothing.

Scenes are all or nothing: the whole scene is read, checked and every component is made before the container is touched, so a failure leaves the container exactly as it was. The failure says which of three things went wrong:

* `SceneUnopenable`: the file could not be opened or read (missing, a directory, not a regular file, permission denied). The message gives the system's reason and never mentions parsing.
* `SceneNotUnderstood`: the text is not a valid scene. A syntax error gives the line and column; a missing `entities`, or an entity without `Handle` or `Components`, names the entity (its index and handle) and the missing item.
* `SceneComponentFailed`: a component could not be made. The message names the entity, the component type and the plugin's own message.

**Data format.** The configuration a plugin receives (the `data` argument of `create_component`) is a `nlohmann::json *` from the JSON library that libecs-cpp provides. A plugin that reads it must be built against the same libecs-cpp, and so the same JSON library version, as libthe-seed. Nothing checks this; a mismatch is undefined behaviour, typically a crash.

`JSONLoader` hands the `std::unique_ptr` returned by `ComponentLoader::Create` to `Entity::Component` unchanged.

### Thread Safety

All loader instances are safe to use from several threads: any method can be called on the same instance concurrently, including `PathAdd` while loads are running. A plugin is loaded once per loader: concurrent requests for one name wait for the same load and get the same result, and different names that resolve to the same file share one open of it. A failed load is reported to every request that waited for it and is not remembered; the next request tries again. No loader lock is held while the file system is searched or a plugin is opened. A request that starts after `PathAdd` has returned uses the new location. A plugin's static initialiser must not load through the same loader that is loading it.

For paks, concurrent requests for one unchanged file check its description once and share the result; requests for different resources of it read them at the same time. No loader lock is held while the file system is searched, a pak is opened and checked, or resources are read. One `ResourcePak` may be used from several threads, and copies of it share the remembered description; adding to an `ecs::Container` is still limited to the container's own thread, as libecs-cpp requires.

```cpp
the_seed::ComponentLoader loader;
loader.PathAdd("/usr/lib/the-seed/components");

std::thread t1([&]() { auto c = loader.Create("org/comp-a"); });
std::thread t2([&]() { auto c = loader.Create("org/comp-b"); });
t1.join();
t2.join();
```

### Error Handling

Every loader throws `LoadError` (`#include <libthe-seed/LoadError.hpp>`, in the `the_seed` namespace) when a plugin, resource pak or scene cannot be loaded. It derives from `std::runtime_error`, so existing `catch (const std::runtime_error &)` code keeps working. `what()` is a summary line followed by indented detail lines; `ReasonGet()`, `NameGet()`, `FileGet()`, `LocationsGet()` and `DetailGet()` give the same facts in structured form.

| `Reason` | Meaning |
|---|---|
| `InvalidName` | The name breaks a name rule; nothing was searched. |
| `NotFound` | No searched location holds the file. The message lists every location and its state, development locations marked as such. |
| `NotLoadable` | The deciding file is present but the platform (or the pak reader) refused it. The message has the absolute file and the platform's reason. For a pak, `DetailGet()` starts with `damaged:` when the file breaks a rule of the format, and is the system's text (for example `Permission denied`) when the file could not be read. |
| `ResourceMissing` | A pak was found and is sound, but does not hold every resource requested. `MissingGet()` lists the missing names; `FileGet()` is the pak. |
| `EntryPointMissing` | The plugin loaded but has no `create_component`, `create_system` or requested function. |
| `NoObject` | The entry point returned no object. |
| `SceneUnopenable`, `SceneNotUnderstood`, `SceneComponentFailed` | See JSONLoader. |

```cpp
try {
    auto component = loader.Create("nonexistent/component");
} catch (const the_seed::LoadError &e) {
    if(e.ReasonGet() == the_seed::LoadError::Reason::NotFound)
    {
        // e.what() lists every location that was searched
    }
}
```

A location whose folder or file cannot be examined for a reason other than not existing (for example `Permission denied`) does not stop the search: it is listed as `could not be examined:` with the system's text, and the next location is tried. This holds for every loader. A pak that is present is never reported as `NotFound`.

```text
resource pak "org/art" not found: no location holds art.pak
  searched: /game/paks
  could not be examined: /locked/paks: Permission denied
  development location, does not exist: ../../art
```

A damaged pak and a pak lacking a resource read:

```text
resource pak "org/art": /game/paks/art.pak is present but could not be read: damaged: the description claims 268467203 bytes but the file holds 1048576
resource pak "org/art": /game/paks/art.pak does not contain "tex_hero", "snd_jump"
```

`LoadError::Location::state` has a fifth value, `LocationState::Unreadable`, with the system's text in `Location::reason`. No exception other than `LoadError` leaves `PakLoader` or `ResourcePak`.

`LibraryLoader::PathsGet` returns the locations added with `PathAdd`, in order, whether or not they exist, and never throws. Earlier versions returned the library files found and threw when there were none.

On Windows, a plugin is opened by absolute path with its own folder first in the dependent-library search, but the operating system still resolves the plugin's dependent DLLs by its own rules. If another copy of a dependent DLL is earlier in that search, it is the one used; keep a plugin's dependencies beside it.

### Migrating from earlier versions

* Every public class is now in the `the_seed` namespace: `the_seed::ComponentLoader`, `the_seed::SystemLoader`, `the_seed::JSONLoader`, `the_seed::PakLoader`, `the_seed::ResourcePak`, `the_seed::LoadError`, `the_seed::LibraryLoader`, `the_seed::DependencyLister` (with `DependencyResult` and `LibraryError`), `the_seed::MachOParser`, `the_seed::PeSigner`, `the_seed::MachOSigner` and `the_seed::MsiSigner`. The old global names clashed with names in applications and platform headers. Update your code: write `the_seed::` before each name, or add a using-declaration at your own scope (`using the_seed::ComponentLoader;`).

  Every symbol of the library has a new name, so applications must be rebuilt; a program built against the old library fails to start with an unresolved-symbol error, never silently. Plugins built before the change keep loading unchanged, because a plugin exports only `create_component` or `create_system` and does not link this library; a plugin that links this library's classes must be rebuilt.
* Only the classes listed above are exported from the shared library. Helpers that were exported by accident are no longer visible.
* `LibraryDeleter` is removed. Code that used it must use its own deleter.
* Code that relied on `<windows.h>` or `<dlfcn.h>` arriving through `<libthe-seed/LibraryLoader.hpp>` must include them itself.
* The working directory is no longer searched. A program that relied on it now sees, for example:

  ```
  component plugin "org/my-component" not found: no location holds libmy-component.so
    development location, does not exist: ../../my-component/src/.libs
    development location, does not exist: ../node_modules/org/my-component/src/.libs
  ```

  with any locations added listed first, `searched:` or `does not exist:` for each. Fix it by adding the folder: `loader.PathAdd(".")`.
* The development locations (`../../<library>/src/.libs`, `../node_modules/<org>/<library>/src/.libs`) are searched after the configured locations instead of before them.
* Failures are `LoadError`. Code that caught `std::invalid_argument` (invalid names) or `nlohmann::json::parse_error` (scenes) must catch `LoadError`; catching `std::runtime_error` keeps working.
* A scene with no `entities`, or an entity without `Components`, is now an error (`SceneNotUnderstood`). Write `"entities": []` for an empty scene.
* A missing scene file is `SceneUnopenable`, not a parse error at line 1, column 1.
* A damaged plugin in an earlier location is reported instead of being skipped for a later copy.
* `LibraryLoader::PathsGet` returns locations and never throws.
* Resource paks: `Load(name, names)` now fails with `ResourceMissing` when a requested name is not in the pak, where it used to return the smaller map. Code that relied on the smaller map calls `ResourcePak(file).ResourceNames()` first, or catches `LoadError` and uses `MissingGet()`.
* `ResourcePak` throws `LoadError` for a missing or damaged file and for an unknown resource name, where it threw `std::runtime_error`, `nlohmann::json` exceptions, `std::length_error` or `std::invalid_argument`. Catch `LoadError`; catching `std::runtime_error` keeps working. Its constructor no longer reads the resources, and `ResourceNames()` is `const`.
* Paks that were silently misread are refused as damaged: a pak with a non-ASCII resource name, or one built after a resource file changed size (see "Resource pak format"). Make them again.
* A pak that cannot be examined in an earlier location (permission denied) no longer ends the search with `NotFound` and no reason: the search continues and the message says which locations could not be examined.
* Plugins are never unloaded. Remove any code that depended on destroying a loader to unload a plugin.
* The private members of every loader, and of `ResourcePak`, changed: rebuild programs against the new headers.

The dependency lister, the Mach-O parser and the PE, Mach-O and MSI signers treat every file and every signature blob as untrusted. Malformed input is rejected with `std::runtime_error`, whose message names the format and the structure at fault, and is never repaired or partly processed. A file that a rejected operation would have modified is left unchanged. `DependencyLister` reports such errors per input in `DependencyResult::errors` instead of throwing.

## Release notes

What changed in each release, and the rules for the version numbers, are in `NEWS`.
