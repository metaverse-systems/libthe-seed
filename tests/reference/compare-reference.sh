#!/bin/sh
# Compares what two builds of libthe-seed do with a list of real files.
#
# Usage: compare-reference.sh <old build dir> <new build dir> <list file>
#
# Each build directory is an out-of-tree libthe-seed build (configured and
# made). reference-dump.cpp is compiled against each build's library, run on
# the list, and the two outputs are compared line by line:
#
#   ok X       -> ok X        pass
#   ok X       -> ok Y        FAIL  (result changed)
#   ok         -> rejected    FAIL  (false rejection)
#   rejected   -> rejected    pass
#   rejected   -> ok          INVESTIGATE
#
# The outputs are kept as reference-dump-old.txt and reference-dump-new.txt in
# the directory named by REFERENCE_OUT (default: the current directory).
# Exit status is 1 when any FAIL line was printed.
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <old build dir> <new build dir> <list file>" >&2
    exit 2
fi

old_build=$(cd "$1" && pwd)
new_build=$(cd "$2" && pwd)
list=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
here=$(cd "$(dirname "$0")" && pwd)
out=${REFERENCE_OUT:-$(pwd)}
CXX=${CXX:-g++}
PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-}
export PKG_CONFIG_PATH

# Source tree of a build directory: the directory holding its configure script.
source_of() {
    sed -n 's/^abs_top_srcdir *= *//p' "$1/Makefile" | head -n 1
}

build_dump() {
    build=$1
    exe=$2
    src=$(source_of "$build")
    libs=$(pkg-config --libs libecs-cpp 2>/dev/null || true)
    cflags=$(pkg-config --cflags libecs-cpp 2>/dev/null || true)
    # shellcheck disable=SC2086
    $CXX -std=c++20 -O1 $cflags -I"$src/include" \
        "$here/reference-dump.cpp" -o "$exe" \
        -L"$build/src/.libs" -lthe-seed -Wl,-rpath,"$build/src/.libs" $libs
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

build_dump "$old_build" "$work/dump-old"
build_dump "$new_build" "$work/dump-new"

"$work/dump-old" "$list" | sort > "$out/reference-dump-old.txt"
"$work/dump-new" "$list" | sort > "$out/reference-dump-new.txt"

# Lines are tab separated: path, operation, ok|rejected, rest. Join on path and operation.
status=0
awk -F'\t' '
    function key(line,   p) { split(line, p, "\t"); return p[1] " " p[2] }
    function state(line,   p) { split(line, p, "\t"); return p[3] }
    function rest(line,   p) { split(line, p, "\t"); return p[4] }
    FNR == NR { old[key($0)] = $0; next }
    {
        k = key($0)
        seen[k] = 1
        if(!(k in old)) { print "FAIL    " k ": operation missing from the old output"; failed = 1; next }
        os = state(old[k]); ns = state($0)
        if(os == "ok" && ns == "ok") {
            if(rest(old[k]) == rest($0)) { same++ }
            else { print "FAIL    " k ": result changed"; print "          old: " rest(old[k]); print "          new: " rest($0); failed = 1 }
        } else if(os == "ok" && ns == "rejected") {
            print "FAIL    " k ": false rejection: " rest($0); failed = 1
        } else if(os == "rejected" && ns == "rejected") {
            same++
        } else {
            print "INVESTIGATE " k ": was rejected (" rest(old[k]) "), now ok"
        }
    }
    END {
        for(k in old) if(!(k in seen)) { print "FAIL    " k ": operation missing from the new output"; failed = 1 }
        print same + 0 " lines identical or acceptable"
        exit failed
    }
' "$out/reference-dump-old.txt" "$out/reference-dump-new.txt" || status=1

exit $status
