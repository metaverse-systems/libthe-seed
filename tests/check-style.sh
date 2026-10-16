#!/bin/sh
# Checks the project's two style rules over the library, its headers and the
# tests:
#
#   - a member is always reached through "this->" (no implicit this);
#   - a member variable, static member or member function name never ends
#     in an underscore.
#
# Every .cpp under src/ and tests/ (including tests/fuzz/) is compiled with
# clang++ -fsyntax-only -Xclang -ast-dump=json and the syntax tree is
# filtered; headers are reached through the sources that include them. Files
# under external/ and system headers are out of scope. Each finding is printed
# once, as "file:line: kind name". Windows-only branches that no Linux
# compilation reaches are covered by review, not by this script.
#
# Exit status: 0 no findings, 1 findings or a source that does not compile,
# 77 skipped (no clang++, no JSON filter, or a cross build).
#
# Environment (set by tests/Makefile.am; defaults suit a run from the source
# tree):
#   CLANGXX         clang++ to use (default: CXX when it is clang, else clang++)
#   CXX             compiler of the build; used only when it is clang
#   LIBECS_CFLAGS   compiler flags for libecs-cpp (default: pkg-config ecs-cpp)
#   top_srcdir      source root (default: parent of this script's directory)
#   top_builddir    build root (unused except for -I of generated headers)
#   CROSS_COMPILING yes when building for another host: the check skips
#   STYLE_JOBS      parallel compilations (default 4, never above 4)

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "${top_srcdir:-$here/..}" && pwd)

if [ "${CROSS_COMPILING:-no}" = yes ]; then
    echo "check-style: skipped, cross build (mingw-only branches are covered by review)"
    exit 77
fi

cxx=${CLANGXX:-}
if [ -z "$cxx" ]; then
    case "${CXX:-}" in
    *clang*) cxx=$CXX ;;
    *) cxx=clang++ ;;
    esac
fi
if ! command -v "${cxx%% *}" >/dev/null 2>&1; then
    echo "check-style: skipped, $cxx not found; install clang"
    exit 77
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "check-style: skipped, python3 not found (needed to filter the syntax tree)"
    exit 77
fi

ecs=${LIBECS_CFLAGS-}
if [ -z "$ecs" ]; then
    ecs=$(pkg-config --cflags ecs-cpp 2>/dev/null)
fi

jobs=${STYLE_JOBS:-4}
case "$jobs" in
'' | *[!0-9]* | 0) jobs=4 ;;
esac
[ "$jobs" -gt 4 ] && jobs=4

work=$(mktemp -d "${TMPDIR:-/tmp}/seed-style.XXXXXX") || exit 99
trap 'rm -rf "$work"' EXIT

filter=$work/filter.py
cat >"$filter" <<'PY'
import json
import os
import re
import sys

scope = [os.path.normpath(p) + "/" for p in sys.argv[2:]]
source = sys.argv[1]


def in_scope(path):
    path = os.path.normpath(path)
    return any(path.startswith(root) for root in scope)


class State:
    file = ""
    line = 0


state = State()
file_re = re.compile(r'^\s*"file": "(.*)",?$')
line_re = re.compile(r'^\s*"line": (\d+),?$')
found = {}


def resolve(node):
    """Fill in the file and line of every location below node, in the order
    the dumper prints them (it omits a file or line that did not change)."""
    if isinstance(node, dict):
        if "spellingLoc" in node or "expansionLoc" in node:
            for key in ("spellingLoc", "expansionLoc"):
                if key in node:
                    resolve(node[key])
            return
        if "offset" in node:
            if "file" in node:
                state.file = node["file"]
            if "line" in node:
                state.line = node["line"]
            node["_file"] = state.file
            node["_line"] = state.line
            return
        for value in node.values():
            resolve(value)
    elif isinstance(node, list):
        for value in node:
            resolve(value)


def spelling(loc):
    if not isinstance(loc, dict):
        return None
    if "spellingLoc" in loc:
        loc = loc["spellingLoc"]
    if "_file" not in loc:
        return None
    return loc["_file"], loc["_line"]


def report(loc, kind, name):
    place = spelling(loc)
    if place is None or not in_scope(place[0]):
        return
    found[(os.path.normpath(place[0]), place[1], kind, name)] = True


RECORDS = ("CXXRecordDecl", "ClassTemplateDecl", "ClassTemplateSpecializationDecl",
           "ClassTemplatePartialSpecializationDecl")


def check(node, parent_kind):
    if isinstance(node, list):
        for value in node:
            check(value, parent_kind)
        return
    if not isinstance(node, dict):
        return
    kind = node.get("kind")
    inner = node.get("inner", [])
    if kind in ("MemberExpr", "CXXDependentScopeMemberExpr") and inner:
        base = inner[0]
        while isinstance(base, dict) and base.get("kind") in ("ImplicitCastExpr", "ParenExpr") and base.get("inner"):
            base = base["inner"][0]
        if isinstance(base, dict) and base.get("kind") == "CXXThisExpr" and base.get("implicit"):
            report(node.get("range", {}).get("begin"), "missing this->", node.get("name", "?"))
    name = node.get("name", "")
    if name.endswith("_") and not node.get("isImplicit"):
        if kind == "FieldDecl":
            report(node.get("loc"), "member variable ends in underscore", name)
        elif kind == "CXXMethodDecl":
            report(node.get("loc"), "member function ends in underscore", name)
        elif kind == "VarDecl" and parent_kind in RECORDS:
            report(node.get("loc"), "static member ends in underscore", name)
    for value in inner:
        check(value, kind)


def handle(lines, wanted):
    if not wanted:
        return
    text = "".join(lines).rstrip().rstrip(",")
    chunk = json.loads(text)
    resolve(chunk)
    check(chunk, "TranslationUnitDecl")


chunk = None
expect_include_file = False
files_seen = []
last_line = None
start_file = ""
for raw in sys.stdin:
    if chunk is None:
        if raw.rstrip("\n") == "    {":
            chunk = [raw]
            files_seen = []
            last_line = None
            start_file = state.file
            expect_include_file = False
        continue
    chunk.append(raw)
    if '"includedFrom": {' in raw:
        expect_include_file = True
        continue
    match = file_re.match(raw)
    if match:
        if expect_include_file:
            expect_include_file = False
        else:
            files_seen.append(match.group(1))
        continue
    match = line_re.match(raw)
    if match:
        last_line = int(match.group(1))
        continue
    if raw.startswith("    }"):
        wanted = in_scope(start_file) if start_file else False
        wanted = wanted or any(in_scope(name) for name in files_seen)
        if wanted:
            handle(chunk, True)
        else:
            if files_seen:
                state.file = files_seen[-1]
            if last_line is not None:
                state.line = last_line
        chunk = None

for (path, line, kind, name) in sorted(found):
    print("%s:%d: %s %s" % (path, line, kind, name))
PY

cases=$work/cases
: >"$cases"
# (fuzz_entry.cpp is compiled once per target in tests/fuzz/build-fuzzers.sh; one target is enough here.)
# Test programs and the library are compiled like tests/Makefile.am and
# src/Makefile.am compile them.
tdefs="-DFIXTURES_DIR=\"$top/tests/fixtures\" -DLIBRARY_PATH_UNDER_TEST=\"${top_builddir:-$top}/src/.libs/libthe-seed.so\" -DMODULE_DIR=\"${top_builddir:-$top}/tests/.libs\""
for f in "$top"/src/*.cpp "$top"/src/internal/*.cpp; do
    [ -f "$f" ] && printf '%s\t%s\n' "$f" "" >>"$cases"
done
for f in "$top"/tests/*.cpp "$top"/tests/fuzz/*.cpp; do
    [ -f "$f" ] || continue
    extra=$tdefs
    case "$f" in
    */test_FuzzCorpus.cpp | */test_QueryEquivalence.cpp) extra="$extra -DFUZZ_CORPUS_DIR=\"$top/tests/fuzz/corpus\"" ;;
    */fuzz_entry.cpp) extra="$extra -DSEED_FUZZ_TARGET=FuzzElf" ;;
    */TestModule.cpp)
        printf '%s\t%s\n' "$f" "$extra -DTEST_MODULE_VARIANT=2" >>"$cases"
        ;;
    esac
    printf '%s\t%s\n' "$f" "$extra" >>"$cases"
done

cat >"$work/one.sh" <<'SH'
#!/bin/sh
# one.sh <file> <defines>; the settings arrive in the environment.
file=$1 defs=$2
out=$STYLE_WORK/out.$(printf '%s' "$file$defs" | cksum | tr ' ' _)
# shellcheck disable=SC2086
$STYLE_CXX -std=c++20 -w -fsyntax-only -Xclang -ast-dump=json \
    -I "$STYLE_TOP/include" -I "$STYLE_TOP/src" -I "$STYLE_TOP/external" \
    -I "$STYLE_TOP/external/Catch2/extras" -I "$STYLE_TOP/tests" -I "$STYLE_TOP/tests/fuzz" \
    $STYLE_ECS $defs "$file" 2>"$out.err" |
    python3 -I "$STYLE_WORK/filter.py" "$file" "$STYLE_TOP/src" "$STYLE_TOP/include" "$STYLE_TOP/tests" >"$out.txt" ||
    echo "filter failed" >>"$out.err"
if grep -q 'error' "$out.err"; then
    { echo "check-style: $file does not compile or could not be filtered:"; cat "$out.err"; } >"$out.fail"
fi
SH
chmod +x "$work/one.sh"

STYLE_WORK=$work STYLE_CXX=$cxx STYLE_TOP=$top STYLE_ECS=$ecs
export STYLE_WORK STYLE_CXX STYLE_TOP STYLE_ECS

# A tab separates a source from its defines; xargs gets them NUL separated.
tab=$(printf '\t')
while IFS=$tab read -r file defs; do
    printf '%s\0%s\0' "$file" "$defs"
done <"$cases" | xargs -0 -n 2 -P "$jobs" "$work/one.sh"

status=0
for f in "$work"/out.*.fail; do
    [ -f "$f" ] || continue
    cat "$f" >&2
    status=1
done

cat "$work"/out.*.txt 2>/dev/null | sort -u >"$work/report.txt"
count=$(wc -l <"$work/report.txt")
if [ "$count" -gt 0 ]; then
    sed "s|^$top/||" "$work/report.txt"
    echo "check-style: $count finding(s)" >&2
    status=1
else
    echo "check-style: no findings"
fi
exit $status
