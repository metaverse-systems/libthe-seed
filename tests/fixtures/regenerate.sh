#!/bin/sh
# Rebuilds every generated sample in this directory from src/ and rewrites
# SHA256SUMS. Samples are replaced only through this script.
#
# Usage: regenerate.sh            rebuild the samples and SHA256SUMS
#        regenerate.sh --verify   check the samples with independent tools
#
# Tools: x86_64-w64-mingw32-gcc, wixl, clang, ld64.lld, llvm-lipo for the
# rebuild; osslsigncode, openssl, llvm-objdump, llvm-otool, file for --verify.
# Versioned names such as ld64.lld-21 are found when the plain name is absent.
# The script refuses to run when a needed tool is missing and names it.
#
# plain.txt is hand-written and is not rebuilt.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/src
samples="tiny.exe test.dll tiny.msi tiny-macho-x86_64 tiny-macho-arm64 tiny-macho-universal plain.txt"

find_tool() { # name -> prints the command to run, or nothing
    if command -v "$1" >/dev/null 2>&1; then
        echo "$1"
        return
    fi
    v=30
    while [ "$v" -ge 11 ]; do
        if command -v "$1-$v" >/dev/null 2>&1; then
            echo "$1-$v"
            return
        fi
        v=$((v - 1))
    done
}

need() { # name...: sets T_<name> for each, or refuses naming every missing tool
    missing=
    for n in "$@"; do
        t=$(find_tool "$n")
        if [ -z "$t" ]; then
            missing="$missing $n"
        else
            var=$(printf '%s' "$n" | tr '.-' '__')
            eval "T_$var=\$t"
        fi
    done
    if [ -n "$missing" ]; then
        echo "regenerate.sh: required tool(s) not found:$missing" >&2
        exit 1
    fi
}

scratch=$(mktemp -d "${TMPDIR:-/tmp}/seed-samples.XXXXXX")
trap 'rm -rf "$scratch"' EXIT INT TERM

if [ "${1:-}" = "--verify" ]; then
    need osslsigncode openssl llvm-objdump llvm-otool file
    status=0
    report() { # sample result detail
        printf '%-22s %-4s %s\n' "$1" "$2" "$3"
        [ "$2" = ok ] || status=1
    }

    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/k.pem" \
        -out "$scratch/c.pem" -subj /CN=sample-check -days 2 >/dev/null 2>&1

    for s in tiny.exe test.dll tiny.msi; do
        if osslsigncode sign -certs "$scratch/c.pem" -key "$scratch/k.pem" \
            -in "$here/$s" -out "$scratch/signed-$s" >"$scratch/sign.log" 2>&1 &&
            osslsigncode verify -in "$scratch/signed-$s" >"$scratch/verify.log" 2>&1 ||
            grep -q "Calculated message digest" "$scratch/verify.log" 2>/dev/null; then
            case $s in
                tiny.msi)
                    grep -q "Calculated message digest" "$scratch/verify.log" &&
                        report "$s" ok "osslsigncode signed it and recomputed the digest" ||
                        report "$s" FAIL "osslsigncode did not process it"
                    ;;
                *)
                    cur=$(sed -n 's/^Current message digest *: *//p' "$scratch/verify.log" | head -n 1)
                    calc=$(sed -n 's/^Calculated message digest *: *//p' "$scratch/verify.log" | head -n 1)
                    if [ -n "$cur" ] && [ "$cur" = "$calc" ]; then
                        report "$s" ok "osslsigncode signed it and the stored and calculated digests match"
                    else
                        report "$s" FAIL "osslsigncode digests differ or are missing"
                    fi
                    ;;
            esac
        else
            report "$s" FAIL "osslsigncode could not sign or verify it"
        fi
    done

    "$T_llvm_objdump" -p "$here/test.dll" >"$scratch/imports.txt" 2>&1 || true
    if grep -qi 'DLL Name: KERNEL32.dll' "$scratch/imports.txt" &&
        grep -qi 'DLL Name: msvcrt.dll' "$scratch/imports.txt"; then
        report test.dll ok "llvm-objdump lists kernel32.dll and msvcrt.dll imports"
    else
        report test.dll FAIL "llvm-objdump does not list kernel32.dll and msvcrt.dll"
    fi

    for s in tiny.exe test.dll tiny.msi tiny-macho-x86_64 tiny-macho-arm64 tiny-macho-universal; do
        kind=$(file -b "$here/$s")
        case $s:$kind in
            tiny.exe:PE32+*console*|test.dll:PE32+*DLL*|tiny.msi:*"MSI Installer"*) r=ok ;;
            tiny-macho-x86_64:*"Mach-O 64-bit x86_64 executable"*) r=ok ;;
            tiny-macho-arm64:*"Mach-O 64-bit arm64 executable"*) r=ok ;;
            tiny-macho-universal:*"universal binary with 2 architectures"*) r=ok ;;
            *) r=FAIL ;;
        esac
        report "$s" "$r" "file: $kind" | cut -c 1-110
        [ "$r" = ok ] || status=1
    done

    for s in tiny-macho-x86_64 tiny-macho-arm64; do
        if "$T_llvm_objdump" --macho -f "$here/$s" >/dev/null 2>&1 &&
            "$T_llvm_otool" -l "$here/$s" 2>&1 | grep -q 'cmd LC_MAIN'; then
            report "$s" ok "llvm-objdump and llvm-otool parse it and find LC_MAIN"
        else
            report "$s" FAIL "llvm-objdump/llvm-otool could not parse it"
        fi
    done
    if "$T_llvm_otool" -f "$here/tiny-macho-universal" 2>&1 | grep -q 'nfat_arch 2'; then
        report tiny-macho-universal ok "llvm-otool lists 2 slices"
    else
        report tiny-macho-universal FAIL "llvm-otool does not list 2 slices"
    fi
    exit "$status"
fi

if [ $# -ne 0 ]; then
    echo "usage: regenerate.sh [--verify]" >&2
    exit 2
fi

need x86_64-w64-mingw32-gcc wixl clang ld64.lld llvm-lipo

# Windows executable and library.
x86_64-w64-mingw32-gcc -Os -s -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp \
    -o "$here/tiny.exe" "$src/tiny.c"
x86_64-w64-mingw32-gcc -Os -shared -s -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp \
    -o "$here/test.dll" "$src/test_dll.c"

# Installer. The package's one file is its own source, so wixl runs in src/.
(cd "$src" && wixl -o "$here/tiny.msi" tiny.wxs)

# Mach-O: one thin file per architecture, linked against the stub library.
for arch in x86_64 arm64; do
    clang -target "$arch-apple-macos11" -Os -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -c -o "$scratch/macho-$arch.o" "$src/macho.c"
    "$T_ld64_lld" -arch "$arch" -platform_version macos 11.0 11.0 -e _main -no_adhoc_codesign \
        -o "$here/tiny-macho-$arch" "$scratch/macho-$arch.o" -L"$src" -lSystem
done
"$T_llvm_lipo" -create "$here/tiny-macho-x86_64" "$here/tiny-macho-arm64" \
    -output "$here/tiny-macho-universal"

# Hashes of every sample, in the order of the contract.
(cd "$here" && sha256sum $samples >SHA256SUMS)

echo "regenerate.sh: rebuilt the samples and rewrote SHA256SUMS."
echo "Update PROVENANCE.md (tool versions, commands, sizes) and run regenerate.sh --verify."
