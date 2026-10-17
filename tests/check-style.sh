#!/bin/sh
# Checks the project's two style rules over the library, its headers and the
# tests:
#
#   - a member is always reached through "this->" (no implicit this);
#   - a member variable, static member or member function name never ends
#     in an underscore.
#
# Every .cpp under src/ and tests/ (including tests/fuzz/) is parsed once by
# clang-query, which applies the rules as syntax-tree matchers while the tree
# is in memory and prints only the matches; headers are reached through the
# sources that include them. Files under external/ and system headers are out
# of scope. Each finding is printed once, as "file:line: kind name".
# Windows-only branches that no Linux compilation reaches are covered by
# review, not by this script.
#
# A member of a dependent base reached without "this->" has no syntax-tree
# node for the implicit "this" and is not seen here; the compiler rejects most
# such code anyway, and the rest is covered by review.
#
# Exit status: 0 no findings, 1 findings or a source that does not compile,
# 77 skipped (no clang-query, or a cross build).
#
# Environment (set by tests/Makefile.am; defaults suit a run from the source
# tree):
#   CLANG_QUERY     clang-query to use (default: clang-query on PATH, else the
#                   clang-query-<N> matching the clang in CXX or on PATH, else
#                   the newest clang-query-<N> on PATH)
#   CXX             compiler of the build; used only to pick a version
#   LIBECS_CFLAGS   compiler flags for libecs-cpp (default: pkg-config ecs-cpp)
#   top_srcdir      source root (default: parent of this script's directory)
#   top_builddir    build root (unused except for -I of generated headers)
#   CROSS_COMPILING yes when building for another host: the check skips
#   STYLE_JOBS      parallel parses (default 4, never above 4)

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "${top_srcdir:-$here/..}" && pwd)

if [ "${CROSS_COMPILING:-no}" = yes ]; then
    echo "check-style: skipped, cross build (mingw-only branches are covered by review)"
    exit 77
fi

# The tool: an unversioned clang-query, else the one matching the clang in
# use, else the newest one installed.
query=${CLANG_QUERY:-}
if [ -z "$query" ] && command -v clang-query >/dev/null 2>&1; then
    query=clang-query
fi
if [ -z "$query" ]; then
    case "${CXX:-}" in
    *clang*) cxx=$CXX ;;
    *) cxx=clang++ ;;
    esac
    major=$("${cxx%% *}" --version 2>/dev/null | sed -n 's/.*clang version \([0-9][0-9]*\)\..*/\1/p' | head -n 1)
    if [ -n "$major" ] && command -v "clang-query-$major" >/dev/null 2>&1; then
        query=clang-query-$major
    fi
fi
if [ -z "$query" ]; then
    newest=0
    for dir in $(printf '%s' "$PATH" | tr ':' ' '); do
        for candidate in "$dir"/clang-query-[0-9]*; do
            [ -x "$candidate" ] || continue
            version=${candidate##*-}
            case "$version" in
            *[!0-9]*) continue ;;
            esac
            if [ "$version" -gt "$newest" ]; then
                newest=$version
                query=$candidate
            fi
        done
    done
fi
if [ -z "$query" ] || ! command -v "${query%% *}" >/dev/null 2>&1; then
    echo "check-style: skipped, clang-query not found; install clang-tools"
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

# The matchers. Each is restricted to the files in scope so that the standard
# library and Catch2 are never printed; the filter below checks the scope
# again, exactly, by prefix. The root is quoted for the regular expression.
scope_re=$(printf '%s' "$top" | sed 's/[][\.*^$+?(){}|\\]/\\&/g')
scope_re="^$scope_re/(src|include|tests)/"
cat >"$work/matchers.txt" <<EOF
set bind-root false
set output diag
enable output dump
match memberExpr(isExpansionInFileMatching("$scope_re"), hasObjectExpression(ignoringParenImpCasts(cxxThisExpr()))).bind("this")
match fieldDecl(isExpansionInFileMatching("$scope_re"), matchesName("_\$"), unless(isImplicit())).bind("field")
match cxxMethodDecl(isExpansionInFileMatching("$scope_re"), matchesName("_\$"), unless(anyOf(isImplicit(), cxxConstructorDecl(), cxxDestructorDecl(), cxxConversionDecl()))).bind("method")
match varDecl(isExpansionInFileMatching("$scope_re"), matchesName("_\$"), hasParent(cxxRecordDecl())).bind("static")
EOF

# Turns clang-query's report into "file:line: kind name" lines. A match is a
# "<file>:<line>:<col>: note: "<bind>" binds here" line, the source line with
# a caret, then "Binding for ..." and the dump of the node: the declaration
# line names the member before its quoted type, a member access names it
# after "->" or "." and is followed by the "this" node, which says "implicit"
# when the source did not write it.
cat >"$work/filter.awk" <<'AWK'
function flush() {
    if (kind == "") return
    if (index(path, top "/") == 1 && (kind != "this" || implicit) && name != "") {
        print path ":" line ": " text[kind] " " name
    }
    kind = ""
}
BEGIN {
    text["this"] = "missing this->"
    text["field"] = "member variable ends in underscore"
    text["method"] = "member function ends in underscore"
    text["static"] = "static member ends in underscore"
}
/^Match #[0-9]+:$/ { flush(); next }
/: note: "[a-z]+" binds here$/ {
    flush()
    loc = $0
    sub(/: note: .*$/, "", loc)
    kind = $0
    sub(/^.*: note: "/, "", kind)
    sub(/" binds here$/, "", kind)
    n = split(loc, part, ":")
    line = part[n - 1]
    path = part[1]
    for (i = 2; i <= n - 2; i++) path = path ":" part[i]
    name = ""
    implicit = 0
    node = 0
    next
}
kind != "" && /^Binding for / { node = 1; next }
kind != "" && node == 1 && /^[A-Za-z]/ {
    node = 2
    if (kind == "this") {
        if (match($0, / (->|\.)[A-Za-z_~][A-Za-z0-9_]*/)) {
            name = substr($0, RSTART + 1, RLENGTH - 1)
            sub(/^(->|\.)/, "", name)
        }
    } else {
        head = $0
        sub(/ '.*$/, "", head)
        m = split(head, token, " ")
        name = token[m]
    }
    next
}
kind == "this" && node == 2 && /CXXThisExpr .* implicit this$/ { implicit = 1; next }
END { flush() }
AWK

cases=$work/cases
: >"$cases"
# (fuzz_entry.cpp is compiled once per target in tests/fuzz/build-fuzzers.sh; one target is enough here.)
# Test programs and the library are parsed like tests/Makefile.am and
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
$STYLE_QUERY -f "$STYLE_WORK/matchers.txt" "$file" -- -std=c++20 -w \
    -I "$STYLE_TOP/include" -I "$STYLE_TOP/src" -I "$STYLE_TOP/external" \
    -I "$STYLE_TOP/external/Catch2/extras" -I "$STYLE_TOP/tests" -I "$STYLE_TOP/tests/fuzz" \
    $STYLE_ECS $defs 2>"$out.err" |
    awk -v top="$STYLE_TOP" -f "$STYLE_WORK/filter.awk" >"$out.txt" ||
    echo "filter failed" >>"$out.err"
if grep -q 'error' "$out.err"; then
    { echo "check-style: $file does not compile or could not be filtered:"; cat "$out.err"; } >"$out.fail"
fi
SH
chmod +x "$work/one.sh"

STYLE_WORK=$work STYLE_QUERY=$query STYLE_TOP=$top STYLE_ECS=$ecs
export STYLE_WORK STYLE_QUERY STYLE_TOP STYLE_ECS

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
