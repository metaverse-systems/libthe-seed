#!/bin/sh
# Validates the sample files in tests/fixtures. Host independent: needs only
# sha256sum, od, and (optionally) git.
#
# PROVENANCE.md must hold one table row per sample with the columns
#   | Sample | Tool | Version | Command | Source | Licence | Size | Check |
# where Tool, Version, Command, Licence and Check must be non-empty.
#
# Usage: check-fixtures.sh [fixtures-dir]

here=$(cd "$(dirname "$0")" && pwd)
dir=${1:-$here/fixtures}
root=$(cd "$dir/../.." && pwd)
budget=262144
fail=0

bad() { # sample rule message
    printf 'check-fixtures: FAIL %s: rule %s: %s\n' "$1" "$2" "$3" >&2
    fail=1
}

samples="tiny.exe test.dll tiny.msi tiny-macho-x86_64 tiny-macho-arm64 tiny-macho-universal plain.txt"
samples="$samples tiny-macho-arm64-adhoc tiny-macho-x86_64-adhoc tiny-macho-universal-adhoc"
samples="$samples tiny-macho-x86_64-nospace tiny-macho-x86_64-exactfit tiny-macho-universal64"
samples="$samples tiny-macho-dylib-arm64 tiny-macho-x86_64-data-after-sig"
samples="$samples dep/libbaz.so dep/libbar.so dep/libfoo.so dep/appA dep/appB"
samples="$samples dep/libbaz.dll dep/libbar.dll dep/libfoo.dll dep/appA.exe dep/appB.exe"

# Rule 1: hashes, and no unlisted files.
sums=$dir/SHA256SUMS
if [ ! -f "$sums" ]; then
    bad SHA256SUMS 1 "file is missing"
else
    while read -r hash name; do
        [ -n "$name" ] || continue
        if [ ! -f "$dir/$name" ]; then
            bad "$name" 1 "listed in SHA256SUMS but missing"
            continue
        fi
        actual=$(sha256sum "$dir/$name" | cut -d' ' -f1)
        [ "$actual" = "$hash" ] || bad "$name" 1 "hash differs from SHA256SUMS"
    done < "$sums"
    for s in $samples; do
        grep -q "^[0-9a-f]*  $s\$" "$sums" || bad "$s" 1 "not listed in SHA256SUMS"
    done
fi
for f in "$dir"/* "$dir"/.[!.]* "$dir"/dep/*; do
    [ -e "$f" ] || continue
    n=${f#"$dir"/}
    case $n in
        SHA256SUMS|PROVENANCE.md|regenerate.sh|check_pages.py|src|dep) continue ;;
    esac
    if [ ! -f "$sums" ] || ! grep -q "^[0-9a-f]*  $n\$" "$sums"; then
        bad "$n" 1 "file is not listed in SHA256SUMS"
    fi
done

# Rule 2: provenance rows.
prov=$dir/PROVENANCE.md
for s in $samples; do
    if [ ! -f "$prov" ]; then
        bad "$s" 2 "PROVENANCE.md is missing"
        continue
    fi
    row=$(grep -F "| $s |" "$prov" | head -n 1)
    if [ -z "$row" ]; then
        bad "$s" 2 "no row in PROVENANCE.md"
        continue
    fi
    for pair in 2:tool 3:version 4:command 6:licence 8:check; do
        col=${pair%%:*}
        cell=$(printf '%s\n' "$row" | awk -F'|' -v c="$((col + 1))" '{ gsub(/^[ \t]+|[ \t]+$/, "", $c); print $c }')
        [ -n "$cell" ] || bad "$s" 2 "PROVENANCE.md row lacks ${pair#*:}"
    done
done

# Rule 3: total size.
total=0
for s in $samples; do
    [ -f "$dir/$s" ] || continue
    size=$(wc -c < "$dir/$s")
    total=$((total + size))
done
[ "$total" -le "$budget" ] || bad "samples" 3 "total size $total bytes is over the $budget byte budget"

# Rule 4: magic bytes.
hex() { # file offset count
    od -An -tx1 -j "$2" -N "$3" "$1" | tr -d ' \n'
}
for s in $samples; do
    f=$dir/$s
    [ -f "$f" ] || continue
    case $s in
        tiny.exe|test.dll|dep/*.dll|dep/*.exe)
            if [ "$(hex "$f" 0 2)" != 4d5a ]; then
                bad "$s" 4 "does not begin with MZ"
                continue
            fi
            off=$(od -An -tu4 -j 60 -N 4 "$f" | tr -d ' \n')
            [ -n "$off" ] && [ "$(hex "$f" "$off" 4)" = 50450000 ] ||
                bad "$s" 4 "no PE\\0\\0 signature at the offset in the header"
            ;;
        dep/*.so|dep/appA|dep/appB)
            [ "$(hex "$f" 0 4)" = 7f454c46 ] || bad "$s" 4 "does not begin with the ELF magic"
            ;;
        tiny.msi)
            [ "$(hex "$f" 0 8)" = d0cf11e0a1b11ae1 ] || bad "$s" 4 "does not begin with the Compound File signature"
            ;;
        tiny-macho-universal64)
            [ "$(hex "$f" 0 4)" = cafebabf ] || bad "$s" 4 "does not begin with the 64-bit fat file magic"
            ;;
        tiny-macho-universal*)
            [ "$(hex "$f" 0 4)" = cafebabe ] || bad "$s" 4 "does not begin with the fat file magic"
            ;;
        tiny-macho-*)
            [ "$(hex "$f" 0 4)" = cffaedfe ] || bad "$s" 4 "does not begin with the Mach-O 64-bit magic"
            ;;
    esac
done

# Rule 5: tracked and not ignored.
if command -v git >/dev/null 2>&1 && git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    for s in $samples; do
        [ -f "$dir/$s" ] || continue
        if git -C "$dir" check-ignore --no-index -q "$s"; then
            bad "$s" 5 "is ignored by git"
        fi
        git -C "$dir" ls-files --error-unmatch "$s" >/dev/null 2>&1 ||
            bad "$s" 5 "is not tracked by git"
    done
else
    echo "check-fixtures: note: not inside a git work tree, skipping the ignore and tracking checks (rule 5)"
fi

# Rule 6: marked binary.
attrs=$root/.gitattributes
if [ ! -f "$attrs" ] || ! grep -Eq '^/?tests/fixtures/(\*\*)?[[:space:]].*binary' "$attrs"; then
    bad "tests/fixtures" 6 ".gitattributes does not mark the directory binary"
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check-fixtures: all sample files pass"
