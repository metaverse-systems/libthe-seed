#!/bin/sh
# Produces the generated members of the reference file set (see
# contracts/reference-files.md in the feature notes): files made by independent
# tools that are used to check a library change against real data. Nothing here
# is committed and `make check` does not run it.
#
# Usage: make-generated-references.sh <out dir>
#
# Tools. Each is taken from its environment variable when set, otherwise the
# unversioned name on PATH, otherwise the highest-versioned "<name>-<N>":
#   CLANG, LD_LLD, LD64_LLD, LLVM_LIPO, OSSLSIGNCODE
# plus openssl and python3 from PATH. The script prints the tools and versions
# it uses and refuses to run, naming them, when one cannot be found.
#
# Optional inputs:
#   WINE_DLL_DIR   directory holding a Windows user32.dll
#                  (default: $HOME/.wine/drive_c/windows/system32)
#   WINE_CACHE     directory holding wine-gecko-*.msi (default: $HOME/.cache/wine)
#   LIBTHESEED_BUILD  an out-of-tree libthe-seed build directory; when set, the
#                  BuildSuperBlob output is produced with it
#   PKG_CONFIG_PATH   where libecs-cpp.pc is, when LIBTHESEED_BUILD is set

set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <out dir>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
fixtures=$here/../fixtures
mkdir -p "$1"
out=$(cd "$1" && pwd)

find_tool() { # name -> prints the command to run, or nothing
    if command -v "$1" >/dev/null 2>&1; then
        echo "$1"
        return
    fi
    v=40
    while [ "$v" -ge 8 ]; do
        if command -v "$1-$v" >/dev/null 2>&1; then
            echo "$1-$v"
            return
        fi
        v=$((v - 1))
    done
}

missing=
resolve() { # variable-name env-override tool-name
    override=$(eval "printf '%s' \"\${$2:-}\"")
    if [ -n "$override" ]; then
        tool=$override
    else
        tool=$(find_tool "$3")
    fi
    if [ -z "$tool" ]; then
        missing="$missing $3"
    else
        eval "$1=\$tool"
    fi
}

resolve CLANG_BIN CLANG clang
resolve LD_LLD_BIN LD_LLD ld.lld
resolve LD64_LLD_BIN LD64_LLD ld64.lld
resolve LIPO_BIN LLVM_LIPO llvm-lipo
resolve OSSL_BIN OSSLSIGNCODE osslsigncode
resolve OPENSSL_BIN OPENSSL openssl
resolve PYTHON_BIN PYTHON python3
if [ -n "$missing" ]; then
    echo "make-generated-references.sh: required tool(s) not found:$missing" >&2
    exit 1
fi

echo "Tools used:"
for t in "$CLANG_BIN" "$LD_LLD_BIN" "$LD64_LLD_BIN" "$LIPO_BIN" "$OSSL_BIN" "$OPENSSL_BIN"; do
    printf '  %s: ' "$(command -v "$t")"
    ("$t" --version 2>&1 || "$t" version 2>&1) | head -n 1
done

scratch=$(mktemp -d "${TMPDIR:-/tmp}/seed-refgen.XXXXXX")
trap 'rm -rf "$scratch"' EXIT INT TERM

cat > "$scratch/lib.c" <<'SRC'
int seed_reference_value(int x) { return x + 1; }
SRC

# ---- ELF: 32-bit x86 and 32-bit big-endian MIPS shared objects with DT_NEEDED
make_elf() { # name clang-target lld-emulation
    name=$1
    "$CLANG_BIN" --target="$2" -fPIC -c -o "$scratch/$name-stub.o" "$scratch/lib.c"
    for dep in libfirst.so.1 libsecond.so.2; do
        "$LD_LLD_BIN" -m "$3" -shared -soname "$dep" -o "$scratch/$dep" "$scratch/$name-stub.o"
    done
    "$LD_LLD_BIN" -m "$3" -shared --no-as-needed -rpath '$ORIGIN' \
        -o "$out/$name.so" "$scratch/$name-stub.o" "$scratch/libfirst.so.1" "$scratch/libsecond.so.2"
}
make_elf elf32-x86 i686-linux-gnu elf_i386
# The MIPS file has one DT_NEEDED.
"$CLANG_BIN" --target=mips-linux-gnu -fPIC -c -o "$scratch/mips-stub.o" "$scratch/lib.c"
"$LD_LLD_BIN" -m elf32btsmip -shared -soname libfirst.so.1 -o "$scratch/libfirst-mips.so.1" "$scratch/mips-stub.o"
"$LD_LLD_BIN" -m elf32btsmip -shared --no-as-needed -o "$out/elf32-mips-be.so" \
    "$scratch/mips-stub.o" "$scratch/libfirst-mips.so.1"

# ---- PE: tiny.exe plus 3 bytes, and osslsigncode-signed files
"$OPENSSL_BIN" req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/k.pem" \
    -out "$scratch/c.pem" -subj /CN=reference-files -days 2 >/dev/null 2>&1
sign() { # input output
    "$OSSL_BIN" sign -certs "$scratch/c.pem" -key "$scratch/k.pem" -in "$1" -out "$2" >/dev/null
}

cp "$fixtures/tiny.exe" "$out/tiny-plus3.exe"
printf '\001\002\003' >> "$out/tiny-plus3.exe"
sign "$fixtures/tiny.exe" "$out/tiny-signed.exe"

wine_dir=${WINE_DLL_DIR:-$HOME/.wine/drive_c/windows/system32}
if [ -f "$wine_dir/user32.dll" ]; then
    sign "$wine_dir/user32.dll" "$out/user32-signed.dll"
else
    echo "warning: no user32.dll in $wine_dir (set WINE_DLL_DIR); skipping user32-signed.dll" >&2
fi

# ---- Mach-O
src=$fixtures/src
macho_obj() { # arch
    "$CLANG_BIN" -target "$1-apple-macos11" -Os -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -c -o "$scratch/macho-$1.o" "$src/macho.c"
    "$CLANG_BIN" -target "$1-apple-macos11" -Os -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -c -o "$scratch/lib-$1.o" "$scratch/lib.c"
}
macho_obj x86_64
macho_obj arm64

# x86_64 dylib and an executable linked against it.
"$LD64_LLD_BIN" -arch x86_64 -platform_version macos 11.0 11.0 -dylib -install_name @rpath/libref.dylib \
    -no_adhoc_codesign -o "$out/libref-x86_64.dylib" "$scratch/lib-x86_64.o" -L"$src" -lSystem
"$LD64_LLD_BIN" -arch x86_64 -platform_version macos 11.0 11.0 -e _main -no_adhoc_codesign \
    -o "$out/exe-x86_64" "$scratch/macho-x86_64.o" -L"$src" -lSystem "$out/libref-x86_64.dylib"

# arm64 files with ld64.lld's ad-hoc signature (the default when not disabled).
"$LD64_LLD_BIN" -arch arm64 -platform_version macos 11.0 11.0 -e _main \
    -o "$out/exe-arm64-adhoc" "$scratch/macho-arm64.o" -L"$src" -lSystem
"$LD64_LLD_BIN" -arch arm64 -platform_version macos 11.0 11.0 -dylib -install_name @rpath/libref.dylib \
    -o "$out/libref-arm64-adhoc.dylib" "$scratch/lib-arm64.o" -L"$src" -lSystem
"$LD64_LLD_BIN" -arch arm64 -platform_version macos 11.0 11.0 -bundle \
    -o "$out/bundle-arm64-adhoc.bundle" "$scratch/lib-arm64.o" -L"$src" -lSystem

"$LIPO_BIN" -create "$out/exe-x86_64" "$out/exe-arm64-adhoc" -output "$out/universal"

# SuperBlob data cut out of each ad-hoc-signed file, padding included.
cut_superblob() { # input output
    "$PYTHON_BIN" - "$1" "$2" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
magic, ncmds = struct.unpack_from('<II', data, 0)[0], struct.unpack_from('<I', data, 16)[0]
offset = 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', data, offset)
    if cmd == 0x1D:
        dataoff, datasize = struct.unpack_from('<II', data, offset + 8)
        open(sys.argv[2], 'wb').write(data[dataoff:dataoff + datasize])
        sys.exit(0)
    offset += size
sys.exit('no LC_CODE_SIGNATURE in ' + sys.argv[1])
PY
}
cut_superblob "$out/exe-arm64-adhoc" "$out/superblob-exe.bin"
cut_superblob "$out/libref-arm64-adhoc.dylib" "$out/superblob-dylib.bin"
cut_superblob "$out/bundle-arm64-adhoc.bundle" "$out/superblob-bundle.bin"

# BuildSuperBlob output, when a library build is available.
if [ -n "${LIBTHESEED_BUILD:-}" ]; then
    build=$(cd "$LIBTHESEED_BUILD" && pwd)
    srcdir=$(sed -n 's/^abs_top_srcdir *= *//p' "$build/Makefile" | head -n 1)
    cat > "$scratch/build-superblob.cpp" <<'SRC'
#include <libthe-seed/MachOSigner.hpp>
#include <fstream>
#include <iostream>
int main(int, char **argv)
{
    auto directory = MachOSigner::ComputeCodeDirectory(argv[1], "reference");
    std::vector<std::uint8_t> cms(1500);
    for(std::size_t i = 0; i < cms.size(); ++i) cms[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
    auto blob = MachOSigner::BuildSuperBlob(directory.code_directory, cms);
    std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char *>(blob.data()), blob.size());
}
SRC
    # shellcheck disable=SC2046
    g++ -std=c++20 $(pkg-config --cflags libecs-cpp 2>/dev/null || true) -I"$srcdir/include" \
        "$scratch/build-superblob.cpp" -o "$scratch/build-superblob" \
        -L"$build/src/.libs" -lthe-seed -Wl,-rpath,"$build/src/.libs" \
        $(pkg-config --libs libecs-cpp 2>/dev/null || true)
    "$scratch/build-superblob" "$fixtures/tiny-macho-arm64" "$out/superblob-built.bin"
else
    echo "note: LIBTHESEED_BUILD not set; superblob-built.bin not produced" >&2
fi

# ---- MSI: signed copies
sign "$fixtures/tiny.msi" "$out/tiny-signed.msi"
cache=${WINE_CACHE:-$HOME/.cache/wine}
gecko=$(ls "$cache"/wine-gecko-*.msi 2>/dev/null | head -n 1 || true)
if [ -n "$gecko" ]; then
    sign "$gecko" "$out/gecko-signed.msi"
else
    echo "warning: no wine-gecko-*.msi in $cache (set WINE_CACHE); skipping gecko-signed.msi" >&2
fi

echo "Generated files in $out:"
ls -l "$out"
