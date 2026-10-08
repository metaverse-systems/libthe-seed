#!/bin/sh
# Rebuilds every generated sample in this directory from src/ and rewrites
# SHA256SUMS. Samples are replaced only through this script.
#
# Usage: regenerate.sh                  rebuild the samples and SHA256SUMS
#        regenerate.sh --macho          rebuild only the Mach-O samples and
#                                       rewrite SHA256SUMS from the files present
#        regenerate.sh --msi            rebuild the installer samples derived from
#                                       tiny.msi (tiny-v4.msi, nested.msi, the
#                                       samples signed by osslsigncode,
#                                       two-neighbours.msi), msi-open.exe and
#                                       msi-reference.txt, then rewrite SHA256SUMS.
#                                       tiny.msi and the legacy sample are kept
#        regenerate.sh --verify         check the samples with independent tools,
#                                       and the recorded program fingerprints
#        regenerate.sh --reference FILE [SIGNED_FOLDER]
#                                       write the recorded program fingerprints
#                                       (pe-reference-digests.txt) to FILE, or,
#                                       when FILE is named macho-reference.txt,
#                                       the recorded Mach-O known answers; with
#                                       SIGNED_FOLDER (the folder that
#                                       test_MachOReference fills when
#                                       SEED_MACHO_WRITE_SIGNED names it) the
#                                       answers for the programs signed by the
#                                       library are added, as "== signed <sample>"
#                                       blocks. --verify uses the same folder when
#                                       SEED_MACHO_SIGNED_DIR names it
#
# Tools: gcc, x86_64-w64-mingw32-gcc, wixl, clang, ld64.lld, llvm-lipo for the
# rebuild; osslsigncode, openssl, python3, x86_64-w64-mingw32-gcc for --msi;
# osslsigncode, openssl, llvm-objdump, llvm-otool, file, gsf for --verify;
# osslsigncode and openssl for --reference (llvm-lipo, llvm-otool, llvm-objdump,
# python3 and sha256sum for the Mach-O reference); python3 for --verify of the
# Mach-O signatures (check_pages.py).
# Versioned names such as ld64.lld-21 are found when the plain name is absent.
# The script refuses to run when a needed tool is missing and names it.
#
# plain.txt is hand-written and is not rebuilt. neither is
# legacy-the-seed-0.6.0.msi (made once with libthe-seed at commit 6a70071, see
# PROVENANCE.md), nor tiny.msi by --msi. pe-reference-digests.txt is
# recorded evidence rather than a sample: it is rewritten only with
# --reference, and --verify recomputes it.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/src
macho_samples="tiny-macho-x86_64 tiny-macho-arm64 tiny-macho-universal tiny-macho-arm64-adhoc tiny-macho-x86_64-adhoc tiny-macho-universal-adhoc tiny-macho-x86_64-nospace tiny-macho-x86_64-exactfit tiny-macho-universal64 tiny-macho-dylib-arm64 tiny-macho-x86_64-data-after-sig"
samples="tiny.exe test.dll tiny.msi $macho_samples plain.txt"
# Installer samples made from tiny.msi by --msi (make_cfb.py is the SYNTHETIC
# builder; osslsigncode signs with a throw-away certificate whose key is
# discarded) and the program that looks the signature stream up by name.
msi_samples="tiny-v4.msi tiny-osslsig-small.msi tiny-osslsig-large.msi tiny-osslsig-dse.msi nested.msi nested-osslsig.msi two-neighbours.msi msi-open.exe"
legacy_sample=legacy-the-seed-0.6.0.msi
msi_reference_samples="tiny.msi tiny-v4.msi tiny-osslsig-small.msi tiny-osslsig-large.msi tiny-osslsig-dse.msi nested.msi nested-osslsig.msi two-neighbours.msi $legacy_sample"
# The dependency chain libbaz <- libbar <- libfoo <- appA, appB, built for ELF
# and for PE. Listed in SHA256SUMS and PROVENANCE.md like the other samples.
dep_samples="dep/libbaz.so dep/libbar.so dep/libfoo.so dep/appA dep/appB dep/libbaz.dll dep/libbar.dll dep/libfoo.dll dep/appA.exe dep/appB.exe"

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

# Mach-O samples. The first three are the unsigned bases (thin per
# architecture and universal). The others add a signature written by another
# tool (ld64.lld's ad-hoc signature), links with no spare header space and with
# exactly 16 bytes, a 64-bit universal table, a dynamic library, and one
# synthetic refusal sample. Programs are never run.
build_macho() {
    mk_obj() { # arch
        clang -target "$1-apple-macos11" -Os -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -c -o "$scratch/macho-$1.o" "$src/macho.c"
    }
    link() { # arch output extra-flags...
        l_arch=$1
        l_out=$2
        shift 2
        "$T_ld64_lld" -arch "$l_arch" -platform_version macos 11.0 11.0 -e _main "$@" \
            -o "$l_out" "$scratch/macho-$l_arch.o" -L"$src" -lSystem
    }
    mk_obj x86_64
    mk_obj arm64
    for arch in x86_64 arm64; do
        link "$arch" "$here/tiny-macho-$arch" -no_adhoc_codesign
    done
    "$T_llvm_lipo" -create "$here/tiny-macho-x86_64" "$here/tiny-macho-arm64" \
        -output "$here/tiny-macho-universal"

    # Signed by another tool.
    link arm64 "$here/tiny-macho-arm64-adhoc" -adhoc_codesign
    link x86_64 "$here/tiny-macho-x86_64-adhoc" -adhoc_codesign
    "$T_llvm_lipo" -create "$here/tiny-macho-x86_64-adhoc" "$here/tiny-macho-arm64-adhoc" \
        -output "$here/tiny-macho-universal-adhoc"

    # Header space: none, and exactly one load command's worth.
    link x86_64 "$here/tiny-macho-x86_64-nospace" -no_adhoc_codesign -headerpad 0
    link x86_64 "$here/tiny-macho-x86_64-exactfit" -no_adhoc_codesign -headerpad 0x10

    # 64-bit universal table, and a library.
    "$T_llvm_lipo" -create -fat64 "$here/tiny-macho-x86_64" "$here/tiny-macho-arm64" \
        -output "$here/tiny-macho-universal64"
    link arm64 "$here/tiny-macho-dylib-arm64" -no_adhoc_codesign -dylib

    # Synthetic: the signed x86-64 program with 16 bytes after its signature.
    cp "$here/tiny-macho-x86_64-adhoc" "$here/tiny-macho-x86_64-data-after-sig"
    append_bytes 16 >>"$here/tiny-macho-x86_64-data-after-sig"
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

# Writes the Mach-O known answers: for every sample the tool facts (llvm-lipo
# architectures) and, for every sample that carries a signature written by
# another tool, the structure facts and every page SHA-256 as recomputed by
# check_pages.py (Python hashlib, no library code). With a folder of programs
# signed by the library (signed-<sample>, written by test_MachOReference) the
# same facts, the SHA-256 of the whole file and every page hash are added for
# each of them as "== signed <sample>"; the checker must accept each one.
derive_macho_reference() { # output-file [signed-folder]
    need llvm-lipo llvm-otool python3 sha256sum
    mr_out=$1
    mr_signed=${2:-}
    {
        echo "# Known answers for the Mach-O samples."
        echo "# Tools: $("$T_llvm_lipo" --version 2>&1 | sed -n 's/.*LLVM version \(.*\)/LLVM \1/p' | head -n 1), Python $(python3 -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')"
        echo "# Date: $(date -u +%Y-%m-%d)"
        echo "# Method: check_pages.py --pages FILE (Python hashlib over the file as stored)"
        echo "# and llvm-lipo -archs FILE. Page hashes are of the 4096-byte pages of each"
        echo "# slice up to codeLimit. Input SHA-256 of every sample follows."
        for f in $macho_samples; do
            echo "# sample $f $(sha256sum "$here/$f" | cut -d' ' -f1)"
        done
        for f in $macho_samples; do
            case $f in
                *-data-after-sig) continue ;; # synthetic, not a known answer
            esac
            echo "== $f"
            case $f in
                tiny-macho-universal*|tiny-macho-x86_64|tiny-macho-arm64|tiny-macho-dylib-arm64|tiny-macho-x86_64-*|tiny-macho-arm64-*)
                    echo "archs $(echo $("$T_llvm_lipo" -archs "$here/$f" 2>&1))"
                    ;;
            esac
            python3 "$here/check_pages.py" --pages "$here/$f" | sed "s|$here/||"
        done
        if [ -n "$mr_signed" ]; then
            echo "# Signed by the library: identity test-identity, CMS bytes 00 01 ... 3F (64 bytes), capacity 64."
            echo "# The test writes signed-<sample>; check_pages.py accepts each (exit status 0) and"
            echo "# its output follows, then the SHA-256 of the whole file."
            for f in $macho_samples; do
                [ -f "$mr_signed/signed-$f" ] || continue
                echo "== signed $f"
                python3 "$here/check_pages.py" --pages "$mr_signed/signed-$f" >"$scratch/signed-pages.txt" ||
                    { echo "regenerate.sh: check_pages.py rejects the library-signed $f" >&2; exit 1; }
                sed "s|$mr_signed/||" "$scratch/signed-pages.txt"
                echo "sha256 $(sha256sum "$mr_signed/signed-$f" | cut -d' ' -f1)"
            done
        fi
    } >"$mr_out"
}


# One throw-away certificate per run, for the samples signed by osslsigncode.
msi_cert() {
    [ -f "$scratch/msi-k.pem" ] && return 0
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$scratch/msi-k.pem" \
        -out "$scratch/msi-c.pem" -subj /CN=sample-check -days 2 >/dev/null 2>&1
}

# osslsigncode signs: msi_sign IN OUT [extra osslsigncode options...]
msi_sign() {
    ms_in=$1
    ms_out=$2
    shift 2
    msi_cert
    rm -f "$ms_out"
    osslsigncode sign -h sha256 -certs "$scratch/msi-c.pem" -key "$scratch/msi-k.pem" "$@" \
        -in "$ms_in" -out "$ms_out" >"$scratch/msi-sign.log" 2>&1 ||
        { echo "regenerate.sh: osslsigncode could not sign $ms_in" >&2; cat "$scratch/msi-sign.log" >&2; exit 1; }
}

# The value osslsigncode names "Calculated DigitalSignature" for a package:
# the Authenticode fingerprint of its content. For a package that carries a
# signature, the signature (and the extended stream) is removed first, then
# the bare package is signed once more by osslsigncode and verified.
msi_fingerprint() { # file -> lower-case hex
    mf_bare=$scratch/msi-bare.msi
    mf_signed=$scratch/msi-bare-signed.msi
    rm -f "$mf_bare" "$mf_signed"
    if osslsigncode remove-signature -in "$1" -out "$mf_bare" >/dev/null 2>&1; then
        :
    else
        cp "$1" "$mf_bare"
    fi
    msi_sign "$mf_bare" "$mf_signed"
    osslsigncode verify -in "$mf_signed" >"$scratch/msi-verify.log" 2>&1 || true
    sed -n 's/^Calculated DigitalSignature *: *//p' "$scratch/msi-verify.log" | head -n 1 | tr -d ' ' | tr 'A-F' 'a-f'
}

# Values stored in the signature of a package, as osslsigncode reads them.
msi_stored() { # file kind(DigitalSignature|MsiDigitalSignatureEx)
    osslsigncode verify -in "$1" 2>/dev/null | sed -n "s/^Current $2 *: *//p" | head -n 1 | tr -d ' ' | tr 'A-F' 'a-f'
}

# What libgsf (gsf list) sees: the number of entries below the root, and the
# size of the DigitalSignature stream or "none".
msi_gsf_facts() { # file -> "<entries> <signature size|none>"
    gsf list "$1" >"$scratch/msi-gsf.txt" 2>/dev/null || true
    mg_n=$(awk '($1 == "f" || $1 == "d") { n++ } END { print n - 1 }' "$scratch/msi-gsf.txt")
    mg_s=$(awk '$1 == "f" && $NF ~ /DigitalSignature$/ { print $2 }' "$scratch/msi-gsf.txt")
    echo "$mg_n ${mg_s:-none}"
}

msi_file_kind() { # file -> "MSI Installer" or "other"
    case $(file -b "$1") in
        *"MSI Installer"*) echo "MSI Installer" ;;
        *) echo other ;;
    esac
}

# Writes msi-reference.txt: for every installer sample the fingerprint
# osslsigncode calculates for it without its signature, what libgsf and file
# report, and the digests stored by the signatures osslsigncode wrote; then the
# deterministic blobs used. The signed-out lines (what the library writes,
# recorded after osslsigncode accepts that exact file) are added when the
# library can write such packages.
derive_msi_reference() { # output-file
    need osslsigncode openssl python3 gsf file sha256sum
    mr_out=$1
    ver=$(osslsigncode --version 2>&1 | head -n 1 | sed 's/, using:$//')
    {
        echo "# Known answers for the installer samples."
        echo "# Tools: $ver, gsf $(gsf --version 2>&1 | head -n 1 | sed 's/^gsf *//'), $(file --version | head -n 1), Python $(python3 -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')"
        echo "# Date: $(date -u +%Y-%m-%d)"
        echo "# fingerprint <sample> <sha256>: osslsigncode \"Calculated DigitalSignature\" for the sample with its"
        echo "#   signature (and extended stream) removed: osslsigncode remove-signature, sign the bare package with a"
        echo "#   throw-away certificate, osslsigncode verify. It is the digest of the contents; it does not depend on"
        echo "#   the sector size or on where the signature is."
        echo "# entries <sample> <count> <signature size|none>: gsf list entries below the root, and the size gsf"
        echo "#   reports for the DigitalSignature stream."
        echo "# file <sample> <kind>: whether file(1) reports an installer."
        echo "# stored-digest / stored-ex <sample> <sha256>: osslsigncode \"Current DigitalSignature\" and"
        echo "#   \"Current MsiDigitalSignatureEx\", the values held in the signature of a signed sample (the"
        echo "#   extended value covers the extended stream, so the digest of a package that has one differs"
        echo "#   from its fingerprint)."
        echo "# blob <name> <size> <sha256>: bytes written by make_cfb.py --blob <name> <size>."
        echo "# Input SHA-256 of every sample follows."
        for f in $msi_reference_samples; do
            echo "# sample $f $(sha256sum "$here/$f" | cut -d' ' -f1)"
        done
        tiny_fp=
        for f in $msi_reference_samples; do
            if [ "$f" = "$legacy_sample" ]; then
                # osslsigncode cannot read the file; the unsigned original is tiny.msi.
                echo "# $f: osslsigncode cannot read this file; the value below is that of its unsigned original tiny.msi"
                echo "fingerprint $f $tiny_fp"
            else
                fp=$(msi_fingerprint "$here/$f")
                [ -n "$fp" ] || { echo "regenerate.sh: no fingerprint reported for $f" >&2; exit 1; }
                [ "$f" != tiny.msi ] || tiny_fp=$fp
                echo "fingerprint $f $fp"
            fi
            echo "entries $f $(msi_gsf_facts "$here/$f")"
            echo "file $f $(msi_file_kind "$here/$f")"
            case $f in
                *osslsig*|two-neighbours.msi)
                    echo "stored-digest $f $(msi_stored "$here/$f" DigitalSignature)"
                    ex=$(msi_stored "$here/$f" MsiDigitalSignatureEx)
                    [ -z "$ex" ] || echo "stored-ex $f $ex"
                    ;;
            esac
        done
        python3 "$here/make_cfb.py" --blob osslsig-large-unauth 6000 "$scratch/blob-large.bin"
        python3 "$here/make_cfb.py" --blob legacy-fixed 1426 "$scratch/blob-legacy.bin"
        echo "blob osslsig-large-unauth 6000 $(sha256sum "$scratch/blob-large.bin" | cut -d' ' -f1)"
        echo "blob legacy-fixed 1426 $(sha256sum "$scratch/blob-legacy.bin" | cut -d' ' -f1)"
    } >"$mr_out"
}

# Builds the installer samples derived from tiny.msi.
build_msi() {
    need osslsigncode openssl python3 x86_64-w64-mingw32-gcc gsf file sha256sum
    bm_blob=$scratch/blob-large.bin
    python3 "$here/make_cfb.py" --blob osslsig-large-unauth 6000 "$bm_blob"
    python3 "$here/make_cfb.py" --convert "$here/tiny.msi" "$here/tiny-v4.msi"
    python3 "$here/make_cfb.py" --nested "$here/tiny.msi" "$here/nested.msi"
    python3 "$here/make_cfb.py" --extra-neighbours "$here/tiny.msi" "$scratch/extra.msi"
    msi_sign "$here/tiny.msi" "$here/tiny-osslsig-small.msi"
    msi_sign "$here/tiny.msi" "$here/tiny-osslsig-large.msi" -addUnauthenticatedBlob -blobFile "$bm_blob"
    msi_sign "$here/tiny.msi" "$here/tiny-osslsig-dse.msi" -add-msi-dse
    msi_sign "$here/nested.msi" "$here/nested-osslsig.msi"
    msi_sign "$scratch/extra.msi" "$scratch/extra-signed.msi"
    python3 "$here/make_cfb.py" --two-neighbours "$here/tiny.msi" "$scratch/extra-signed.msi" "$here/two-neighbours.msi"
    # The lookup program: no C runtime, so it stays small.
    x86_64-w64-mingw32-gcc -Os -s -nostdlib -fno-builtin -fno-asynchronous-unwind-tables \
        -fno-unwind-tables -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp \
        -Wl,-e,entry -o "$here/msi-open.exe" "$src/msi_open.c" -lole32 -luuid -lshell32 -lkernel32
}

if [ "${1:-}" = "--verify" ]; then
    need osslsigncode openssl llvm-objdump llvm-otool llvm-lipo file python3 gsf
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

    for s in tiny.exe test.dll tiny.msi $macho_samples; do
        kind=$(file -b "$here/$s")
        case $s:$kind in
            tiny.exe:PE32+*console*|test.dll:PE32+*DLL*|tiny.msi:*"MSI Installer"*) r=ok ;;
            tiny-macho-x86_64*:*"Mach-O 64-bit x86_64 executable"*) r=ok ;;
            tiny-macho-arm64*:*"Mach-O 64-bit arm64 executable"*) r=ok ;;
            tiny-macho-dylib-arm64:*"Mach-O 64-bit arm64 dynamically linked shared library"*) r=ok ;;
            tiny-macho-universal64:data) r=ok ;; # file 5.47 does not know the 64-bit table
            tiny-macho-universal*:*"universal binary with 2 architectures"*) r=ok ;;
            *) r=FAIL ;;
        esac
        report "$s" "$r" "file: $kind" | cut -c 1-110
        [ "$r" = ok ] || status=1
    done

    for s in $dep_samples; do
        kind=$(file -b "$here/$s")
        case $s:$kind in
            dep/*.so:*"ELF 64-bit LSB shared object"*|dep/appA:*"ELF 64-bit LSB"*|dep/appB:*"ELF 64-bit LSB"*) r=ok ;;
            dep/*.dll:PE32+*DLL*|dep/*.exe:PE32+*) r=ok ;;
            *) r=FAIL ;;
        esac
        report "$s" "$r" "file: $kind" | cut -c 1-110
    done

    # llvm-objdump and llvm-otool parse every thin sample; llvm-lipo and
    # llvm-objdump list the slices of every universal one (llvm-otool -f reports
    # the 64-bit table as the 32-bit one, so llvm-objdump is used for it).
    for s in $macho_samples; do
        case $s in
            tiny-macho-universal64)
                if [ "$(echo $("$T_llvm_lipo" -archs "$here/$s" 2>&1))" = "x86_64 arm64" ] &&
                    "$T_llvm_objdump" --macho --universal-headers "$here/$s" 2>&1 | grep -q 'fat_magic FAT_MAGIC_64'; then
                    report "$s" ok "llvm-lipo lists x86_64 arm64; llvm-objdump reads the 64-bit table"
                else
                    report "$s" FAIL "llvm-lipo/llvm-objdump could not read the 64-bit table"
                fi
                ;;
            tiny-macho-universal*)
                if [ "$(echo $("$T_llvm_lipo" -archs "$here/$s" 2>&1))" = "x86_64 arm64" ] &&
                    "$T_llvm_otool" -f "$here/$s" 2>&1 | grep -q 'nfat_arch 2'; then
                    report "$s" ok "llvm-lipo lists x86_64 arm64; llvm-otool lists 2 slices"
                else
                    report "$s" FAIL "llvm-lipo/llvm-otool do not list 2 slices"
                fi
                ;;
            *-data-after-sig)
                report "$s" ok "synthetic: llvm-objdump parses it; the extra bytes are ignored by it"
                "$T_llvm_objdump" --macho -f "$here/$s" >/dev/null 2>&1 || status=1
                ;;
            tiny-macho-dylib-arm64)
                if "$T_llvm_objdump" --macho -f "$here/$s" >/dev/null 2>&1 &&
                    "$T_llvm_otool" -l "$here/$s" 2>&1 | grep -q 'cmd LC_ID_DYLIB'; then
                    report "$s" ok "llvm-objdump and llvm-otool parse it and find LC_ID_DYLIB"
                else
                    report "$s" FAIL "llvm-objdump/llvm-otool could not parse it"
                fi
                ;;
            *)
                if "$T_llvm_objdump" --macho -f "$here/$s" >/dev/null 2>&1 &&
                    "$T_llvm_otool" -l "$here/$s" 2>&1 | grep -q 'cmd LC_MAIN'; then
                    report "$s" ok "llvm-objdump and llvm-otool parse it and find LC_MAIN"
                else
                    report "$s" FAIL "llvm-objdump/llvm-otool could not parse it"
                fi
                ;;
        esac
    done

    # The independent checker: it accepts the signature written by ld64.lld
    # (calibration), finds no signature in the unsigned samples and the refusal
    # sample is the only one it flags.
    for s in $macho_samples; do
        rc=0
        out=$(python3 "$here/check_pages.py" "$here/$s" 2>&1) || rc=$?
        case $s in
            *-adhoc)
                if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -q 'mismatched=0'; then
                    report "$s" ok "check_pages.py recomputed every page hash of the ld64.lld signature"
                else
                    report "$s" FAIL "check_pages.py does not accept the ld64.lld signature"
                fi
                ;;
            *-data-after-sig)
                if [ "$rc" -eq 1 ] && printf '%s\n' "$out" | grep -q 'ends 16 bytes before the end'; then
                    report "$s" ok "check_pages.py flags the 16 bytes after the signature"
                else
                    report "$s" FAIL "check_pages.py does not flag the bytes after the signature"
                fi
                ;;
            *)
                if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -q 'signature none'; then
                    report "$s" ok "check_pages.py reads it and finds no signature"
                else
                    report "$s" FAIL "check_pages.py rejects the unsigned sample"
                fi
                ;;
        esac
    done

    if [ -f "$here/macho-reference.txt" ]; then
        derive_macho_reference "$scratch/macho-reference.txt" "${SEED_MACHO_SIGNED_DIR:-}"
        # Without a folder of library-signed programs only the blocks that do not
        # depend on the library are compared.
        macho_norm() {
            grep -v '^# \(Tools\|Date\)' "$1" |
                if [ -n "${SEED_MACHO_SIGNED_DIR:-}" ]; then cat; else
                    awk '/^# Signed by the library/ { skip = 1 } !skip { print }'; fi
        }
        if [ "$(macho_norm "$here/macho-reference.txt")" = "$(macho_norm "$scratch/macho-reference.txt")" ]; then
            report macho-reference.txt ok "check_pages.py and llvm-lipo reproduce every recorded answer"
        else
            diff "$here/macho-reference.txt" "$scratch/macho-reference.txt" || true
            report macho-reference.txt FAIL "recorded answers differ from the tools"
        fi
    else
        echo "SKIPPED: macho-reference.txt not recomputed, the file is absent"
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
    # Installer samples: each is an installer to file(1), is listed by libgsf,
    # and, when osslsigncode signed it, its stored and calculated digests agree.
    # The synthetic ones are parsed by the independent reader of make_cfb.py
    # (the listing, allocation tables and tree are checked); the legacy sample
    # is damaged on purpose, and only file and gsf are asked about it.
    for s in $msi_reference_samples; do
        facts=$(msi_gsf_facts "$here/$s")
        if [ "$(msi_file_kind "$here/$s")" = "MSI Installer" ] && grep -q 'SummaryInformation' "$scratch/msi-gsf.txt"; then
            report "$s" ok "file reports an installer; gsf list lists ${facts% *} entries (signature: ${facts#* })" | cut -c 1-110
        else
            report "$s" FAIL "file or gsf list does not accept it"
        fi
        case $s in
            *osslsig*|two-neighbours.msi)
                osslsigncode verify -in "$here/$s" >"$scratch/verify.log" 2>&1 || true
                cur=$(sed -n 's/^Current DigitalSignature *: *//p' "$scratch/verify.log" | head -n 1)
                calc=$(sed -n 's/^Calculated DigitalSignature *: *//p' "$scratch/verify.log" | head -n 1)
                if [ -n "$cur" ] && [ "$cur" = "$calc" ]; then
                    report "$s" ok "osslsigncode: stored and calculated digests match"
                else
                    report "$s" FAIL "osslsigncode: digests differ or are missing"
                fi
                ;;
        esac
    done
    for s in tiny-v4.msi nested.msi two-neighbours.msi; do
        if out=$(python3 "$here/make_cfb.py" --check "$here/$s" 2>&1); then
            report "$s" ok "make_cfb.py reader: $(printf '%s' "$out" | sed "s|$here/||; s/: ok\$//" | cut -c 1-90)"
        else
            report "$s" FAIL "make_cfb.py reader rejects it: $out"
        fi
    done
    if python3 "$here/make_cfb.py" --check "$here/two-neighbours.msi" 2>&1 | grep -q 'signature entry with a left and a right neighbour, each with subtrees'; then
        report two-neighbours.msi ok "the signature entry has neighbours on both sides, each with subtrees"
    else
        report two-neighbours.msi FAIL "the signature entry does not have two neighbours with subtrees"
    fi
    case $(file -b "$here/msi-open.exe") in
        PE32+*console*) report msi-open.exe ok "file reports a PE32+ console program" ;;
        *) report msi-open.exe FAIL "file does not report a PE32+ console program" ;;
    esac

    if [ -f "$here/msi-reference.txt" ]; then
        derive_msi_reference "$scratch/msi-reference.txt"
        # Comment lines hold the date, the tools and the hashes of the signed
        # samples, which change with every signing; every other line must match.
        if [ "$(grep -v '^#' "$here/msi-reference.txt")" = "$(grep -v '^#' "$scratch/msi-reference.txt")" ]; then
            report msi-reference.txt ok "osslsigncode, file and gsf reproduce every recorded answer"
        else
            diff "$here/msi-reference.txt" "$scratch/msi-reference.txt" | grep -v '^[<>] #' || true
            report msi-reference.txt FAIL "recorded answers differ from the tools"
        fi
    else
        echo "SKIPPED: msi-reference.txt not recomputed, the file is absent"
    fi
    exit "$status"
fi

if [ "${1:-}" = "--reference" ] && { [ $# -eq 2 ] || [ $# -eq 3 ]; }; then
    case $2 in
        */macho-reference.txt|macho-reference.txt) derive_macho_reference "$2" "${3:-}" ;;
        *) derive_reference "$2" ;;
    esac
    echo "regenerate.sh: wrote $2"
    exit 0
fi

if [ "${1:-}" = "--macho" ] && [ $# -eq 1 ]; then
    need clang ld64.lld llvm-lipo
    build_macho
    (cd "$here" && sha256sum $samples $msi_samples $legacy_sample $dep_samples pe-reference-digests.txt macho-reference.txt msi-reference.txt >SHA256SUMS)
    echo "regenerate.sh: rebuilt the Mach-O samples and rewrote SHA256SUMS."
    exit 0
fi

if [ "${1:-}" = "--msi" ] && [ $# -eq 1 ]; then
    build_msi
    derive_msi_reference "$here/msi-reference.txt"
    (cd "$here" && sha256sum $samples $msi_samples $legacy_sample $dep_samples pe-reference-digests.txt macho-reference.txt msi-reference.txt >SHA256SUMS)
    echo "regenerate.sh: rebuilt the installer samples, msi-open.exe and msi-reference.txt, and rewrote SHA256SUMS."
    exit 0
fi

if [ $# -ne 0 ]; then
    echo "usage: regenerate.sh [--macho | --msi | --verify | --reference FILE [SIGNED_FOLDER]]" >&2
    exit 2
fi

need gcc x86_64-w64-mingw32-gcc wixl clang ld64.lld llvm-lipo

# Windows executable and library.
x86_64-w64-mingw32-gcc -Os -s -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp \
    -o "$here/tiny.exe" "$src/tiny.c"
x86_64-w64-mingw32-gcc -Os -shared -s -Wl,--gc-sections,--file-alignment,512,--no-insert-timestamp \
    -o "$here/test.dll" "$src/test_dll.c"

# Installer. The package's one file is its own source, so wixl runs in src/.
(cd "$src" && wixl -o "$here/tiny.msi" tiny.wxs)

build_macho

# The dependency chain, ELF then PE. Each library names the next one with an
# explicit -l (and --no-as-needed) so that the dependency is recorded, and has
# no other dependency (-nostdlib). The import libraries and the ELF link-time
# search folder are scratch files. Programs are never run.
mkdir -p "$here/dep" "$scratch/elf" "$scratch/pe"
elf_flags="-Os -s -nostdlib -Wl,-z,noseparate-code -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -Wl,--hash-style=gnu -Wl,--build-id=none -Wl,-z,norelro -Wl,--no-eh-frame-hdr -fno-asynchronous-unwind-tables -fno-unwind-tables"
(
    cd "$scratch/elf"
    gcc $elf_flags -shared -fPIC -Wl,-soname,libbaz.so -o libbaz.so "$src/dep/libbaz.c"
    gcc $elf_flags -shared -fPIC -Wl,-soname,libbar.so -o libbar.so "$src/dep/libbar.c" \
        -L. -Wl,--no-as-needed -lbaz
    gcc $elf_flags -shared -fPIC -Wl,-soname,libfoo.so -o libfoo.so "$src/dep/libfoo.c" \
        -L. -Wl,--no-as-needed -lbar
    for app in appA appB; do
        gcc $elf_flags -o "$app" "$src/dep/$app.c" -L. -Wl,--no-as-needed -lfoo \
            -Wl,-rpath-link,. -Wl,-e,_start
    done
)
cp "$scratch/elf/libbaz.so" "$scratch/elf/libbar.so" "$scratch/elf/libfoo.so" \
    "$scratch/elf/appA" "$scratch/elf/appB" "$here/dep/"
pe_flags="-Os -s -nostdlib -fno-asynchronous-unwind-tables -fno-unwind-tables -Wl,--gc-sections,--file-alignment,512,--section-alignment,512,--no-insert-timestamp"
(
    cd "$scratch/pe"
    x86_64-w64-mingw32-gcc $pe_flags -shared -Wl,-e,0 -o libbaz.dll "$src/dep/libbaz.c" \
        -Wl,--out-implib,libbaz.dll.a
    x86_64-w64-mingw32-gcc $pe_flags -shared -Wl,-e,0 -o libbar.dll "$src/dep/libbar.c" \
        -L. -lbaz -Wl,--out-implib,libbar.dll.a
    x86_64-w64-mingw32-gcc $pe_flags -shared -Wl,-e,0 -o libfoo.dll "$src/dep/libfoo.c" \
        -L. -lbar -Wl,--out-implib,libfoo.dll.a
    for app in appA appB; do
        x86_64-w64-mingw32-gcc $pe_flags -Wl,-e,_start -o "$app.exe" "$src/dep/$app.c" -L. -lfoo
    done
)
cp "$scratch/pe/libbaz.dll" "$scratch/pe/libbar.dll" "$scratch/pe/libfoo.dll" \
    "$scratch/pe/appA.exe" "$scratch/pe/appB.exe" "$here/dep/"

# Hashes of every sample, in the order of the contract.
(cd "$here" && sha256sum $samples $msi_samples $legacy_sample $dep_samples pe-reference-digests.txt macho-reference.txt msi-reference.txt >SHA256SUMS)

echo "regenerate.sh: rebuilt the samples and rewrote SHA256SUMS."
echo "tiny.msi has a new creation time: run regenerate.sh --msi to rebuild the samples made from it."
echo "Update PROVENANCE.md (tool versions, commands, sizes) and run regenerate.sh --verify."
