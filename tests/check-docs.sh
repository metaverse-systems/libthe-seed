#!/bin/sh
# Checks the reference documentation.
#
#   1. doxygen runs on the generated Doxyfile, with the output moved to a
#      temporary directory, and exits 0.
#   2. Every class and struct declared "class THE_SEED_API Name" or
#      "struct THE_SEED_API Name" in include/libthe-seed/*.hpp has a page.
#   3. No output file is named after the test framework, a test, or an
#      internal definition.
#   4. No page documents private members.
#   5. The configuration reads only include/libthe-seed and README.md.
#   6. When .github exists: no workflow grants contents: write or runs
#      git push or git commit, doxygen.yaml exists, and only its deploy job
#      holds pages: write.
#
# Every failing item is printed. Exit status: 0 pass, 1 fail, 77 doxygen is
# absent (1 instead when SEED_REQUIRE_DOCS=1), 2 usage problem.
#
# Environment (set by tests/Makefile.am): SEED_SRCDIR, SEED_BUILDDIR. Run by
# hand they default to the script's parent directory.

here=$(cd "$(dirname "$0")" && pwd)
srcdir=${SEED_SRCDIR:-$(cd "$here/.." && pwd)}
builddir=${SEED_BUILDDIR:-$srcdir}

if ! command -v doxygen >/dev/null 2>&1; then
    if [ "${SEED_REQUIRE_DOCS:-0}" = 1 ]; then
        echo "FAIL: check-docs: doxygen is not installed, and SEED_REQUIRE_DOCS=1 requires it"
        exit 1
    fi
    echo "check-docs: skipped, doxygen is not installed"
    exit 77
fi

doxyfile=$builddir/Doxyfile
if [ ! -f "$doxyfile" ]; then
    echo "check-docs: $doxyfile does not exist; run configure first" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/check-docs.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail=0
problem() {
    fail=1
    echo "FAIL: check-docs: $*"
}

# ---- 1. doxygen runs ----

out=$work/out
mkdir -p "$out"
{
    cat "$doxyfile"
    printf '\nOUTPUT_DIRECTORY = %s\n' "$out"
} >"$work/Doxyfile"
if ! (cd "$work" && doxygen Doxyfile) >"$work/doxygen.log" 2>&1; then
    problem "doxygen failed:"
    tail -n 8 "$work/doxygen.log" | sed 's/^/    /'
fi
html=$out/html
if [ ! -d "$html" ]; then
    problem "doxygen wrote no html directory"
fi

# ---- 2. One page per exported class ----

for header in "$srcdir"/include/libthe-seed/*.hpp; do
    sed -n 's/^[[:space:]]*\(class\|struct\)[[:space:]]\{1,\}THE_SEED_API[[:space:]]\{1,\}\([A-Za-z_][A-Za-z0-9_]*\).*/\1 \2/p' "$header"
done | sort -u >"$work/classes.txt"
if [ ! -s "$work/classes.txt" ]; then
    problem "no exported class was found in include/libthe-seed"
fi
while read -r kind name; do
    if [ ! -f "$html/${kind}the__seed_1_1${name}.html" ]; then
        problem "no page ${kind}the__seed_1_1${name}.html for $kind $name"
    fi
done <"$work/classes.txt"

# ---- 3. No test or internal names in the output ----

if [ -d "$out" ]; then
    find "$out" -type f | sed "s|^$out/||" | grep -E 'Catch|catch|test_|internal|picosha2|Cfb|PakIndex|ElfParser|PeParser|MachOLayout' |
        sed 's/^/    /' >"$work/leaked.txt"
    if [ -s "$work/leaked.txt" ]; then
        problem "the output holds files for tests or internal definitions:"
        head -n 8 "$work/leaked.txt"
    fi
fi

# ---- 4. No private members ----

if [ -d "$html" ]; then
    grep -rlE 'pri-methods|pri-attribs' "$html" | sed "s|^$html/|    |" >"$work/private.txt"
    if [ -s "$work/private.txt" ]; then
        problem "pages document private members:"
        head -n 8 "$work/private.txt"
    fi
fi

# ---- 5. The input ----

# INPUT and INPUT += lines, without INPUT_ENCODING and the other INPUT_* tags.
sed -n 's/^INPUT[[:space:]]*+\{0,1\}=[[:space:]]*//p' "$doxyfile" | tr ' ' '\n' | grep -v '^$' >"$work/inputs.txt"
if [ ! -s "$work/inputs.txt" ]; then
    problem "the configuration has no INPUT"
fi
while read -r item; do
    case "$item" in
        */include/libthe-seed | */README.md | include/libthe-seed | README.md) ;;
        *) problem "INPUT names '$item', expected only include/libthe-seed and README.md" ;;
    esac
done <"$work/inputs.txt"
for needed in include/libthe-seed README.md; do
    if ! grep -qE "(^|/)$needed\$" "$work/inputs.txt"; then
        problem "INPUT does not name $needed"
    fi
done

# ---- 6. The workflows ----

workflows=$srcdir/.github/workflows
if [ -d "$srcdir/.github" ]; then
    for workflow in "$workflows"/*.yaml "$workflows"/*.yml; do
        [ -f "$workflow" ] || continue
        name=$(basename "$workflow")
        if grep -nE 'contents:[[:space:]]*write|git push|git commit' "$workflow" >"$work/write.txt"; then
            problem "$name writes to the repository:"
            sed 's/^/    /' "$work/write.txt" | head -n 4
        fi
        # Job in which each "pages: write" appears.
        awk '
            /^jobs:/ { injobs = 1; next }
            injobs && /^  [A-Za-z0-9_-]+:/ { job = $1; sub(/:$/, "", job) }
            /pages:[[:space:]]*write/ { print (job == "" ? "(workflow)" : job) }
        ' "$workflow" >"$work/pages-jobs.txt"
        while read -r job; do
            if [ "$name" != doxygen.yaml ] || [ "$job" != deploy ]; then
                problem "$name grants pages: write to '$job'; only the deploy job of doxygen.yaml may"
            fi
        done <"$work/pages-jobs.txt"
    done
    if [ ! -f "$workflows/doxygen.yaml" ]; then
        problem "$workflows/doxygen.yaml does not exist"
    elif ! awk '/^jobs:/ { injobs = 1; next }
                injobs && /^  [A-Za-z0-9_-]+:/ { job = $1; sub(/:$/, "", job) }
                /pages:[[:space:]]*write/ && job == "deploy" { found = 1 }
                END { exit found ? 0 : 1 }' "$workflows/doxygen.yaml"; then
        problem "doxygen.yaml: the deploy job does not hold pages: write"
    fi
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: check-docs: the documentation builds, covers the public classes and leaves out internals; no workflow writes to the repository"
exit 0
