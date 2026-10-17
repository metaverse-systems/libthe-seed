#!/bin/sh
# Checks that "make install" puts exactly the expected files under the prefix.
#
#   1. The build is installed into a temporary DESTDIR.
#   2. The sorted list of files and symbolic links under the prefix equals the
#      list for the host kind (Windows: mingw, cygwin or msys; otherwise Linux
#      style); every extra and every missing entry is named.
#   3. The only file under bin/ is the DLL on Windows; there is none elsewhere.
#   4. The compatibility version is 1: the Linux soname is libthe-seed.so.1,
#      the Windows DLL name ends in -1.dll, and no installed name carries
#      compatibility version 0.
#   5. "make uninstall" into the same DESTDIR leaves no file.
#
# Nothing is executed from the stage, so the check also runs in cross builds.
# Exit status: 0 pass, 1 fail, 77 skipped.
#
# Environment (set by tests/Makefile.am): SEED_SRCDIR, SEED_BUILDDIR,
# SEED_PREFIX, SEED_HOST_OS, MAKE.

here=$(cd "$(dirname "$0")" && pwd)
srcdir=$(cd "${SEED_SRCDIR:-$here/..}" && pwd)
builddir=$(cd "${SEED_BUILDDIR:-$srcdir}" && pwd)
prefix=${SEED_PREFIX:-/usr/local}
host_os=${SEED_HOST_OS:-linux-gnu}
make_cmd=${MAKE:-make}

if [ ! -f "$builddir/Makefile" ]; then
    echo "check-install: skipped, $builddir is not configured"
    exit 77
fi

stage=$(mktemp -d "${TMPDIR:-/tmp}/check-install.XXXXXX") || exit 99
trap 'rm -rf "$stage"' EXIT HUP INT TERM

root=$stage$prefix
failures=0

fail()
{
    echo "FAIL: $*"
    failures=$((failures + 1))
}

# Sorted names of the files and symbolic links below a directory, relative to it.
file_list()
{
    (cd "$1" 2>/dev/null && find . \( -type f -o -type l \) -print | sed 's|^\./||' | LC_ALL=C sort)
}

headers="ComponentLoader DependencyLister Export JSONLoader LibraryLoader LoadError MachOParser MachOSigner MsiSigner PakLoader PeSigner ResourcePak SystemLoader"

case "$host_os" in
    mingw*|cygwin*|msys*)
        kind=windows
        library_files="bin/libthe-seed-1.dll lib/libthe-seed.a lib/libthe-seed.dll.a lib/libthe-seed.la"
        ;;
    *)
        kind=linux
        library_files="lib/libthe-seed.a lib/libthe-seed.la lib/libthe-seed.so lib/libthe-seed.so.1 lib/libthe-seed.so.1.0.0"
        ;;
esac

{
    for header in $headers; do
        echo "include/libthe-seed/$header.hpp"
    done
    for file in $library_files; do
        echo "$file"
    done
    echo "lib/pkgconfig/the-seed.pc"
} | LC_ALL=C sort >"$stage/expected.txt"

if ! "$make_cmd" -C "$builddir" install DESTDIR="$stage" >"$stage/install.log" 2>&1; then
    tail -n 20 "$stage/install.log"
    fail "make install into the stage did not succeed"
    exit 1
fi

file_list "$root" >"$stage/actual.txt"

LC_ALL=C comm -13 "$stage/expected.txt" "$stage/actual.txt" | while IFS= read -r name; do
    echo "FAIL: unexpected file installed: $name"
done
LC_ALL=C comm -23 "$stage/expected.txt" "$stage/actual.txt" | while IFS= read -r name; do
    echo "FAIL: expected file not installed: $name"
done
if ! cmp -s "$stage/expected.txt" "$stage/actual.txt"; then
    failures=$((failures + 1))
fi

# Programs and libraries other than the DLL do not belong in bin/.
if [ -d "$root/bin" ]; then
    for name in $(file_list "$root/bin"); do
        if [ "$kind" = windows ] && [ "$name" = "libthe-seed-1.dll" ]; then
            continue
        fi
        fail "unexpected file under bin/: $name"
    done
fi

# Compatibility version: no installed library name may carry major 0.
for name in $(file_list "$root"); do
    case "$name" in
        lib/libthe-seed.so.0|lib/libthe-seed.so.0.*|bin/libthe-seed-0.dll)
            fail "installed name carries compatibility version 0: $name"
            ;;
    esac
done

if [ "$kind" = linux ]; then
    library="$root/lib/libthe-seed.so.1.0.0"
    if [ ! -f "$library" ]; then
        fail "installed library is missing: lib/libthe-seed.so.1.0.0"
    else
        if [ -n "${OBJDUMP:-}" ] && "$OBJDUMP" -p "$library" >/dev/null 2>&1; then
            soname=$("$OBJDUMP" -p "$library" | sed -n 's/^ *SONAME *//p')
        else
            soname=$(readelf -d "$library" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p')
        fi
        if [ "$soname" != "libthe-seed.so.1" ]; then
            fail "soname is '$soname', expected 'libthe-seed.so.1'"
        fi
    fi
else
    case "$(file_list "$root/bin")" in
        *-1.dll) ;;
        *) fail "the DLL name does not end in -1.dll" ;;
    esac
fi

if ! "$make_cmd" -C "$builddir" uninstall DESTDIR="$stage" >"$stage/uninstall.log" 2>&1; then
    tail -n 20 "$stage/uninstall.log"
    fail "make uninstall from the stage did not succeed"
fi
file_list "$root" | while IFS= read -r name; do
    echo "FAIL: file left behind by make uninstall: $name"
done
if [ -n "$(file_list "$root")" ]; then
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "check-install: $failures problem(s)"
    exit 1
fi
echo "PASS: the install list is exact for a $kind host and make uninstall removes it"
exit 0
