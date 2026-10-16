#!/bin/sh
# Checks the headers the library installs.
#
#   1. The installed header names equal public-headers.txt.
#   2. Each installed header compiles on its own.
#   3. Including all of them brings in no platform definitions
#      (_WINDOWS_, _WINDOWS_H, WINAPI, RTLD_NOW, _DLFCN_H).
#   4. Including all of them does not collide with global classes that
#      have the names the library's classes used to have.
#   5. The internal ELF, PE and Mach-O headers sit next to the platform's own
#      (<elf.h> on Linux, <windows.h> in the Windows cross build), in both
#      include orders.
#   6. A source file outside the library cannot reach an internal header
#      with the include path the library's public tests use.
#
# Every failing item is printed. Exit status: 0 pass, 1 fail, 77 skipped.
#
# Environment (set by tests/Makefile.am): CXX, LIBECS_CFLAGS, MAKE,
# top_builddir, top_srcdir, host, CROSS_COMPILING.

here=$(cd "$(dirname "$0")" && pwd)
top_srcdir=$(cd "${top_srcdir:-$here/..}" && pwd)
top_builddir=$(cd "${top_builddir:-$top_srcdir}" && pwd)
expected_file=$here/public-headers.txt

if [ -z "${CXX:-}" ]; then
    echo "check-public-headers: skipped, CXX is not set"
    exit 77
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/check-public-headers.XXXXXX") || exit 99
trap 'rm -rf "$work"' EXIT HUP INT TERM

cross=no
if [ "${CROSS_COMPILING:-no}" = yes ]; then
    cross=yes
fi

fail=0
problem() {
    fail=1
    echo "check-public-headers: $*"
}

# $1 label; remaining arguments: compiler arguments. Output goes to $work/cc.log.
compile() {
    $CXX -std=c++20 -fsyntax-only "$@" >"$work/cc.log" 2>&1
}

show_log() {
    sed 's/^/    /' "$work/cc.log" | head -n "${1:-8}"
}

# ---- 1. The installed set ----

stage=$work/stage
mkdir -p "$stage"
if ! ${MAKE:-make} -C "$top_builddir/src" install-pkgincludeHEADERS DESTDIR="$stage" >"$work/install.log" 2>&1; then
    problem "the headers could not be installed:"
    sed 's/^/    /' "$work/install.log" | tail -n 8
    exit 1
fi
header_dir=$(find "$stage" -type d -name libthe-seed | head -n 1)
if [ -z "$header_dir" ]; then
    problem "no libthe-seed directory was installed"
    exit 1
fi
include_root=$(dirname "$header_dir")

grep -v -E '^[[:space:]]*(#|$)' "$expected_file" | sort >"$work/expected.txt"
(cd "$header_dir" && ls) | sort >"$work/installed.txt"
comm -23 "$work/expected.txt" "$work/installed.txt" | sed 's/^/  not installed: /' >"$work/diff.txt"
comm -13 "$work/expected.txt" "$work/installed.txt" | sed 's/^/  installed but not listed: /' >>"$work/diff.txt"
if [ -s "$work/diff.txt" ]; then
    problem "the installed headers differ from public-headers.txt:"
    cat "$work/diff.txt"
fi

# ---- 2. Each header on its own ----

for header in $(cat "$work/installed.txt"); do
    printf '#include <libthe-seed/%s>\n' "$header" >"$work/one.cpp"
    if ! compile -I"$include_root" ${LIBECS_CFLAGS:-} "$work/one.cpp"; then
        problem "$header does not compile on its own:"
        show_log
    fi
done

# Every installed header, in one file.
: >"$work/all.hpp"
for header in $(cat "$work/installed.txt"); do
    printf '#include <libthe-seed/%s>\n' "$header" >>"$work/all.hpp"
done

# ---- 3. No platform leak ----

{
    cat "$work/all.hpp"
    cat <<'SOURCE'
#if defined(_WINDOWS_) || defined(_WINDOWS_H) || defined(WINAPI) || defined(RTLD_NOW) || defined(_DLFCN_H)
#error a platform header reached the application through the library headers
#endif
int main() { return 0; }
SOURCE
} >"$work/leak.cpp"
if ! compile -I"$include_root" ${LIBECS_CFLAGS:-} "$work/leak.cpp"; then
    problem "platform definitions leak through the public headers:"
    grep -E 'error' "$work/cc.log" | sed 's/^/    /' | head -n 8
fi

# ---- 4. No collision with global names ----

{
    cat "$work/all.hpp"
    cat <<'SOURCE'
struct ComponentLoader { int value = 1; };
struct SystemLoader { int value = 2; };
struct PakLoader { int value = 3; };
struct LibraryLoader { int value = 4; };
struct ResourcePak { int value = 5; };
struct JSONLoader { int value = 6; };
struct LoadError { int value = 7; };
int main()
{
    ComponentLoader a;
    SystemLoader b;
    PakLoader c;
    LibraryLoader d;
    ResourcePak e;
    JSONLoader f;
    LoadError g;
    return a.value + b.value + c.value + d.value + e.value + f.value + g.value;
}
SOURCE
} >"$work/collision.cpp"
if ! compile -I"$include_root" ${LIBECS_CFLAGS:-} "$work/collision.cpp"; then
    problem "the public headers collide with global names of the same spelling:"
    grep -E 'error' "$work/cc.log" | sed 's/^/    /' | head -n 8
fi

# ---- 5. Internal definitions next to the platform's ----

if [ "$cross" = yes ]; then
    system_header='<windows.h>'
else
    system_header='<elf.h>'
fi
for internal in ElfParser PeParser MachODefs; do
    for order in library-first system-first; do
        {
            if [ "$order" = system-first ]; then
                printf '#include %s\n' "$system_header"
            fi
            printf '#include "internal/%s.hpp"\n' "$internal"
            if [ "$order" = library-first ]; then
                printf '#include %s\n' "$system_header"
            fi
            echo 'int main() { return 0; }'
        } >"$work/coexist.cpp"
        if ! compile -I"$top_srcdir/src" -I"$top_srcdir/include" ${LIBECS_CFLAGS:-} "$work/coexist.cpp"; then
            problem "internal/$internal.hpp does not coexist with $system_header ($order):"
            grep -E 'error' "$work/cc.log" | sed 's/^/    /' | head -n 4
        fi
    done
done

# ---- 6. The public route does not reach internal headers ----

# The include path of the library's public tests: the public headers, the
# libecs-cpp headers and the Catch2 extras, but not src/.
cat >"$work/route.cpp" <<'SOURCE'
#include "internal/FileIO.hpp"
int main() { return 0; }
SOURCE
if compile -I"$top_srcdir/include" ${LIBECS_CFLAGS:-} -I"$top_srcdir/external/Catch2/extras" -I"$here" "$work/route.cpp"; then
    problem "a source file can include internal/FileIO.hpp with the public include path"
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check-public-headers: the installed headers are as listed, self-contained and free of platform definitions"
exit 0
