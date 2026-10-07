#!/bin/sh
# Rebuilds every generated sample in this directory from src/ and rewrites
# SHA256SUMS. Samples are replaced only through this script.
#
# Usage: regenerate.sh                  rebuild the samples and SHA256SUMS
#        regenerate.sh --verify         check the samples with independent tools,
#                                       and the recorded program fingerprints
#        regenerate.sh --reference FILE write the recorded program fingerprints
#                                       (pe-reference-digests.txt) to FILE
#
# Tools: x86_64-w64-mingw32-gcc, wixl, clang, ld64.lld, llvm-lipo for the
# rebuild; osslsigncode, openssl, llvm-objdump, llvm-otool, file for --verify;
# osslsigncode and openssl for --reference.
# Versioned names such as ld64.lld-21 are found when the plain name is absent.
# The script refuses to run when a needed tool is missing and names it.
#
# plain.txt is hand-written and is not rebuilt. pe-reference-digests.txt is
# recorded evidence rather than a sample: it is rewritten only with
# --reference, and --verify recomputes it.

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

# The fixed bytes appended to a program to make its length unaligned: byte k
# (counting from zero) is 0xA5 + k, so n appended bytes are the first n of
# a5 a6 a7 a8 a9 aa ab.
append_bytes() { # count
    n=$1
    k=0
    while [ "$k" -lt "$n" ]; do
        printf "\\$(printf '%03o' $((165 + k)))"
        k=$((k + 1))
    done
}

# Writes the program fingerprints that osslsigncode reports for the signed
# programs: each Windows sample with 0 to 7 fixed bytes appended, signed with
# a throw-away certificate, then the digest osslsigncode calculates for the
# signed file. Also signs the unmodified tiny.exe once for the already signed
# case. Lines: <fixture> <appended-byte-count> <total-length> <sha256>.
derive_reference() { # output-file
    need osslsigncode openssl
    ref_out=$1
    work=$scratch/reference
    mkdir -p "$work"
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$work/k.pem" \
        -out "$work/c.pem" -subj /CN=sample-check -days 2 >/dev/null 2>&1
    ver=$(osslsigncode --version 2>&1 | head -n 1 | sed 's/, using:$//')
    {
        echo "# Program fingerprints (Authenticode SHA-256) for the Windows samples."
        echo "# Tool: $ver"
        echo "# Date: $(date -u +%Y-%m-%d)"
        echo "# Method: each sample with n fixed bytes appended is signed by osslsigncode"
        echo "# and the digest it calculates for the signed file is recorded. The appended"
        echo "# bytes are a5 a6 a7 a8 a9 aa ab (the first n of them). Signing throw-away"
        echo "# certificate: openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=sample-check"
        echo "# Commands, per row:"
        echo "#   cat <fixture> <n appended bytes> > in.exe"
        echo "#   osslsigncode sign -h sha256 -certs c.pem -key k.pem -in in.exe -out signed.exe"
        echo "#   osslsigncode verify -in signed.exe   (line: Calculated message digest)"
        echo "# Input SHA-256 of the unmodified samples:"
        for f in tiny.exe test.dll; do
            echo "#   $f $(sha256sum "$here/$f" | cut -d' ' -f1)"
        done
        echo "# The row tiny-signed.exe is tiny.exe signed once by osslsigncode (the"
        echo "# already signed case); it is not stored, because it would put the samples"
        echo "# over the size budget of check-fixtures.sh. Its digest equals the one for"
        echo "# tiny.exe with 0 bytes appended; its length varies with the signature."
        echo "# Columns: fixture appended-byte-count total-length sha256"
        for f in tiny.exe test.dll; do
            n=0
            while [ "$n" -le 7 ]; do
                rm -f "$work/signed.exe"
                cp "$here/$f" "$work/in.exe"
                append_bytes "$n" >>"$work/in.exe"
                osslsigncode sign -h sha256 -certs "$work/c.pem" -key "$work/k.pem" \
                    -in "$work/in.exe" -out "$work/signed.exe" >"$work/sign.log" 2>&1 ||
                    { echo "regenerate.sh: osslsigncode could not sign $f plus $n bytes" >&2; exit 1; }
                osslsigncode verify -in "$work/signed.exe" >"$work/verify.log" 2>&1 || true
                d=$(sed -n 's/^Calculated message digest *: *//p' "$work/verify.log" | head -n 1 | tr -d ' ' | tr 'A-F' 'a-f')
                [ -n "$d" ] || { echo "regenerate.sh: no digest reported for $f plus $n bytes" >&2; exit 1; }
                echo "$f $n $(wc -c <"$work/in.exe" | tr -d ' ') $d"
                n=$((n + 1))
            done
        done
        rm -f "$work/signed.exe"
        osslsigncode sign -h sha256 -certs "$work/c.pem" -key "$work/k.pem" \
            -in "$here/tiny.exe" -out "$work/signed.exe" >"$work/sign.log" 2>&1 ||
            { echo "regenerate.sh: osslsigncode could not sign tiny.exe" >&2; exit 1; }
        osslsigncode verify -in "$work/signed.exe" >"$work/verify.log" 2>&1 || true
        d=$(sed -n 's/^Calculated message digest *: *//p' "$work/verify.log" | head -n 1 | tr -d ' ' | tr 'A-F' 'a-f')
        echo "tiny-signed.exe 0 $(wc -c <"$work/signed.exe" | tr -d ' ') $d"
    } >"$ref_out"
}

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

    if [ -f "$here/pe-reference-digests.txt" ]; then
        derive_reference "$scratch/pe-reference-digests.txt"
        # The signed program's length varies with each signing, so only its
        # digest is compared; every other row must match exactly.
        norm() { grep -v '^#' "$1" | awk '$1 == "tiny-signed.exe" { $3 = "-" } { print }'; }
        if [ "$(norm "$here/pe-reference-digests.txt")" = "$(norm "$scratch/pe-reference-digests.txt")" ]; then
            report pe-reference-digests.txt ok "osslsigncode reproduces every recorded fingerprint"
        else
            norm "$here/pe-reference-digests.txt" >"$scratch/recorded.txt"
            norm "$scratch/pe-reference-digests.txt" >"$scratch/derived.txt"
            diff "$scratch/recorded.txt" "$scratch/derived.txt" || true
            report pe-reference-digests.txt FAIL "recorded fingerprints differ from osslsigncode"
        fi
    else
        echo "SKIPPED: pe-reference-digests.txt not recomputed, the file is absent"
    fi
    exit "$status"
fi

if [ "${1:-}" = "--reference" ] && [ $# -eq 2 ]; then
    derive_reference "$2"
    echo "regenerate.sh: wrote $2"
    exit 0
fi

if [ $# -ne 0 ]; then
    echo "usage: regenerate.sh [--verify | --reference FILE]" >&2
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
(cd "$here" && sha256sum $samples pe-reference-digests.txt >SHA256SUMS)

echo "regenerate.sh: rebuilt the samples and rewrote SHA256SUMS."
echo "Update PROVENANCE.md (tool versions, commands, sizes) and run regenerate.sh --verify."
