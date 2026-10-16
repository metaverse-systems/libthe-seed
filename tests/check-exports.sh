#!/bin/sh
# Checks that the shared library exports only the public classes.
#
# The exported names are read from the built library (Linux: the dynamic
# symbol table of src/.libs/libthe-seed.so; Windows cross build: the export
# table of src/.libs/libthe-seed-*.dll), demangled, and compared with the
# scopes listed in public-symbols.txt:
#
#   1. a strong symbol matches a listed scope;
#   2. a weak or unique symbol matches a listed scope or is a standard library
#      template instance (std::, __gnu_cxx:: and the matching runtime type
#      information);
#   3. no symbol is in the_seed::internal or has the name of a former internal
#      helper.
#
# Then link probes show that the check can tell the difference:
#
#   4. programs that call the_seed::internal::ReadFileBytes,
#      the_seed::internal::PluginOpenCallCount and the global ReadFileBytes
#      must fail to link against the shared library;
#   5. a program that calls the_seed::PeSigner::HasEmbeddedSignature must link.
#
# Every offending name is printed. Exit status: 0 pass, 1 fail, 77 skipped.
#
# Environment (set by tests/Makefile.am): CXX, NM, OBJDUMP, CXXFILT,
# top_builddir, top_srcdir, host, CROSS_COMPILING.

here=$(cd "$(dirname "$0")" && pwd)
top_srcdir=$(cd "${top_srcdir:-$here/..}" && pwd)
top_builddir=$(cd "${top_builddir:-$top_srcdir}" && pwd)
libdir=$top_builddir/src/.libs
scopes_file=$here/public-symbols.txt

for tool in "${CXX:-}" "${CXXFILT:-}"; do
    if [ -z "$tool" ]; then
        echo "check-exports: skipped, CXX or CXXFILT is not set"
        exit 77
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/check-exports.XXXXXX") || exit 99
trap 'rm -rf "$work"' EXIT HUP INT TERM

cross=no
if [ "${CROSS_COMPILING:-no}" = yes ]; then
    cross=yes
fi

# ---- Collect the exported names: "kind<TAB>demangled name" ----

if [ "$cross" = yes ]; then
    dll=$(ls "$libdir"/libthe-seed-*.dll 2>/dev/null | head -n 1)
    if [ -z "$dll" ]; then
        echo "check-exports: no libthe-seed-*.dll in $libdir"
        exit 1
    fi
    # The name pointer table lists one "[n] +base[m] hint name" line per name.
    $OBJDUMP -p "$dll" |
        sed -n 's/^[[:space:]]*\[[[:space:]]*[0-9]*\][[:space:]]*+base\[[[:space:]]*[0-9]*\][[:space:]]*[0-9a-fA-F]\{4\}[[:space:]]\{1,\}\([^[:space:]]\{1,\}\)$/\1/p' |
        sed 's/^/S	/' >"$work/raw.txt"
    import_lib=$libdir/libthe-seed.dll.a
else
    lib=$libdir/libthe-seed.so
    if [ ! -f "$lib" ]; then
        echo "check-exports: $lib does not exist"
        exit 1
    fi
    $NM -D --defined-only "$lib" |
        awk 'NF >= 3 { kind = "S"; if ($2 == "W" || $2 == "V" || $2 == "u" || $2 == "w" || $2 == "v") kind = "W"; print kind "\t" $3 }' \
            >"$work/raw.txt"
fi

# Symbols of the platform's linker and runtime, not part of the library's surface.
grep -v -E '^[SW]	(_init|_fini|_edata|_end|__bss_start|__data_start|data_start|_IO_stdin_used|__TMC_END__|__dso_handle|_DYNAMIC|_GLOBAL_OFFSET_TABLE_|_gp|DllMain.*|__gxx_personality_.*)$' \
    "$work/raw.txt" >"$work/kinds.txt"

cut -f2 "$work/kinds.txt" | $CXXFILT >"$work/names.txt"
cut -f1 "$work/kinds.txt" >"$work/kind-col.txt"
paste "$work/kind-col.txt" "$work/names.txt" >"$work/symbols.txt"

if [ ! -s "$work/symbols.txt" ]; then
    echo "check-exports: no exported names were read from the library"
    exit 1
fi

# ---- Scopes ----

grep -v -E '^[[:space:]]*(#|$)' "$scopes_file" >"$work/scopes.txt"

fail=0
: >"$work/offenders.txt"

while IFS='	' read -r kind name; do
    allowed=no
    while IFS= read -r scope; do
        case "$name" in
        "$scope"*) allowed=yes; break ;;
        esac
    done <"$work/scopes.txt"

    case "$name" in
    the_seed::internal::* | "typeinfo for the_seed::internal::"* | "vtable for the_seed::internal::"*)
        echo "internal name: $name" >>"$work/offenders.txt"
        continue ;;
    ReadFileBytes* | WriteFileBytes* | NameParser* | ElfParser* | PeParser* | FileReplace* | *CallCount* | *ParseCount* | *ReadCount*)
        echo "former internal name: $name" >>"$work/offenders.txt"
        continue ;;
    esac

    if [ "$allowed" = yes ]; then
        continue
    fi
    if [ "$kind" = W ] || [ "$cross" = yes ]; then
        case "$name" in
        std::* | __gnu_cxx::* | "typeinfo for std::"* | "typeinfo name for std::"* | "vtable for std::"* | "guard variable for std::"* | "typeinfo for __gnu_cxx::"* | "typeinfo name for __gnu_cxx::"*)
            continue ;;
        esac
        # A template instance may begin with its return type.
        case "$name" in
        *' std::'* | *'<std::'*)
            case "$name" in
            the_seed::*) ;;
            *) continue ;;
            esac ;;
        esac
    fi
    echo "outside the public scopes ($kind): $name" >>"$work/offenders.txt"
done <"$work/symbols.txt"

if [ -s "$work/offenders.txt" ]; then
    fail=1
    count=$(wc -l <"$work/offenders.txt" | tr -d ' ')
    echo "check-exports: $count exported name(s) outside the public surface:"
    sort -u "$work/offenders.txt" | sed 's/^/  /'
fi

# ---- Link probes ----

if [ "$cross" = yes ]; then
    link_args="$import_lib"
else
    link_args="-L$libdir -lthe-seed"
fi

probe_links() {
    # $1 probe source; returns 0 when it links
    $CXX -std=c++20 -I"$top_srcdir/include" ${LIBECS_CFLAGS:-} "$1" -o "$work/probe.out" $link_args >"$work/probe.log" 2>&1
}

cat >"$work/probe-readfilebytes.cpp" <<'PROBE'
#include <cstdint>
#include <string>
#include <vector>
namespace the_seed::internal
{
std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path);
}
int main() { return static_cast<int>(the_seed::internal::ReadFileBytes("x").size()); }
PROBE

cat >"$work/probe-pluginopen.cpp" <<'PROBE'
#include <cstdint>
namespace the_seed::internal
{
std::uint64_t PluginOpenCallCount();
}
int main() { return static_cast<int>(the_seed::internal::PluginOpenCallCount()); }
PROBE

cat >"$work/probe-global.cpp" <<'PROBE'
#include <cstdint>
#include <string>
#include <vector>
std::vector<std::uint8_t> ReadFileBytes(const std::string &file_path);
int main() { return static_cast<int>(ReadFileBytes("x").size()); }
PROBE

cat >"$work/probe-public.cpp" <<'PROBE'
#include <libthe-seed/PeSigner.hpp>
int main() { return the_seed::PeSigner::HasEmbeddedSignature("x") ? 1 : 0; }
PROBE

for probe in readfilebytes pluginopen global; do
    if probe_links "$work/probe-$probe.cpp"; then
        fail=1
        echo "check-exports: the link probe '$probe' linked, but the name must not be linkable"
    fi
done

if ! probe_links "$work/probe-public.cpp"; then
    fail=1
    echo "check-exports: the public link probe (the_seed::PeSigner::HasEmbeddedSignature) did not link:"
    sed 's/^/  /' "$work/probe.log" | head -n 8
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check-exports: only the public surface is exported"
exit 0
