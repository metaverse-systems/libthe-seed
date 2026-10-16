#!/bin/sh
# Checks that every copy of the package version and of the libecs-cpp minimum
# version agrees with its single source.
#
#   package.json  "version"                      = SEED_VERSION
#   NEWS          first "libthe-seed X.Y.Z" line = SEED_VERSION (a heading
#                 "libthe-seed (unreleased)" above it is allowed)
#   the-seed.pc   Version:                       = SEED_VERSION
#   the-seed.pc   Requires:                      = ecs-cpp >= SEED_LIBECS_MIN
#   package.json  libecs-cpp dependency          = ^SEED_LIBECS_MIN
#   README.md     names "libecs-cpp SEED_LIBECS_MIN or later" and no other
#                 version in "libecs-cpp X.Y.Z or later" / "needs libecs-cpp X.Y.Z";
#                 every "the-seed >= X.Y.Z" names SEED_VERSION
#   ci.yaml       --atleast-version=SEED_LIBECS_MIN (skipped without .github)
#   package-lock.json is absent
#
# Reads SEED_SRCDIR, SEED_BUILDDIR, SEED_VERSION and SEED_LIBECS_MIN from the
# environment; run by hand, they default to the script's parent directory, the
# AC_INIT version and the the_seed_libecs_min definition in configure.ac.
#
# Exit status: 0 all agree, 1 a copy disagrees, 2 tool or usage problem.

here=$(cd "$(dirname "$0")" && pwd)
srcdir=${SEED_SRCDIR:-$(cd "$here/.." && pwd)}
builddir=${SEED_BUILDDIR:-$srcdir}

if [ -z "$SEED_VERSION" ]; then
    SEED_VERSION=$(sed -n 's/^AC_INIT(\[[^]]*\], *\[\([^]]*\)\].*/\1/p' "$srcdir/configure.ac" | head -n 1)
fi
if [ -z "$SEED_LIBECS_MIN" ]; then
    SEED_LIBECS_MIN=$(sed -n 's/^m4_define(\[the_seed_libecs_min\], *\[\([^]]*\)\]).*/\1/p' "$srcdir/configure.ac" | head -n 1)
fi
if [ -z "$SEED_VERSION" ] || [ -z "$SEED_LIBECS_MIN" ]; then
    echo "check-versions: cannot determine the package version or the libecs-cpp minimum" >&2
    exit 2
fi

# json_value FILE KEY [SECTION]: value of a string key in a package.json
# (the first match, or the first inside SECTION when it is given).
json_value() {
    if [ -n "$3" ]; then
        sed -n "/\"$3\"[[:space:]]*:[[:space:]]*{/,/}/p" "$1" |
            sed -n "s#^[[:space:]]*\"$2\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*#\1#p" | head -n 1
    else
        sed -n "s#^[[:space:]]*\"$2\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*#\1#p" "$1" | head -n 1
    fi
}

# compare_tree SRC BUILD: prints one FAIL line per disagreement.
compare_tree() {
    s=$1
    b=$2
    ecs_key='@metaverse-systems/libecs-cpp'

    if [ -f "$s/package.json" ]; then
        found=$(json_value "$s/package.json" version)
        [ "$found" = "$SEED_VERSION" ] ||
            echo "FAIL: package.json: version is '$found', expected '$SEED_VERSION'"
        found=$(json_value "$s/package.json" "$ecs_key" dependencies)
        [ "$found" = "^$SEED_LIBECS_MIN" ] ||
            echo "FAIL: package.json: libecs-cpp dependency is '$found', expected '^$SEED_LIBECS_MIN'"
        if command -v node >/dev/null 2>&1; then
            found=$(node -e 'const p=require(process.argv[1]);console.log(p.version+" "+(p.dependencies||{})[process.argv[2]])' "$s/package.json" "$ecs_key" 2>/dev/null)
            expect="$(json_value "$s/package.json" version) $(json_value "$s/package.json" "$ecs_key" dependencies)"
            [ "$found" = "$expect" ] ||
                echo "FAIL: package.json: node reads '$found', the text search read '$expect'"
        fi
    else
        echo "FAIL: package.json: file is missing, expected 'version $SEED_VERSION'"
    fi

    if [ -f "$s/NEWS" ]; then
        found=$(sed -n 's/^libthe-seed \([0-9][0-9.]*\).*/\1/p' "$s/NEWS" | head -n 1)
        [ "$found" = "$SEED_VERSION" ] ||
            echo "FAIL: NEWS: first release heading is '$found', expected '$SEED_VERSION'"
    else
        echo "FAIL: NEWS: file is missing, expected 'libthe-seed $SEED_VERSION' heading"
    fi

    if [ -f "$b/the-seed.pc" ]; then
        found=$(sed -n 's/^Version:[[:space:]]*//p' "$b/the-seed.pc" | head -n 1)
        [ "$found" = "$SEED_VERSION" ] ||
            echo "FAIL: the-seed.pc: Version is '$found', expected '$SEED_VERSION'"
        found=$(sed -n 's/^Requires:[[:space:]]*//p' "$b/the-seed.pc" | head -n 1)
        [ "$found" = "ecs-cpp >= $SEED_LIBECS_MIN" ] ||
            echo "FAIL: the-seed.pc: Requires is '$found', expected 'ecs-cpp >= $SEED_LIBECS_MIN'"
    else
        echo "FAIL: the-seed.pc: file is missing in '$b', expected 'Version: $SEED_VERSION'"
    fi

    if [ -f "$s/README.md" ]; then
        grep -q "libecs-cpp $SEED_LIBECS_MIN or later" "$s/README.md" ||
            echo "FAIL: README.md: requirement line is missing, expected 'libecs-cpp $SEED_LIBECS_MIN or later'"
        sed -n 's/.*libecs-cpp \([0-9][0-9.]*\) or later.*/\1/p; s/.*needs libecs-cpp \([0-9][0-9.]*\).*/\1/p' "$s/README.md" |
            while read -r found; do
                [ "$found" = "$SEED_LIBECS_MIN" ] ||
                    echo "FAIL: README.md: libecs-cpp requirement is '$found', expected '$SEED_LIBECS_MIN'"
            done
        sed -n 's/.*the-seed >= \([0-9][0-9.]*\).*/\1/p' "$s/README.md" |
            while read -r found; do
                [ "$found" = "$SEED_VERSION" ] ||
                    echo "FAIL: README.md: the-seed requirement is '$found', expected '$SEED_VERSION'"
            done
    else
        echo "FAIL: README.md: file is missing, expected 'libecs-cpp $SEED_LIBECS_MIN or later'"
    fi

    if [ -d "$s/.github" ]; then
        if ! grep -q -e "--atleast-version=$SEED_LIBECS_MIN" "$s/.github/workflows/ci.yaml" 2>/dev/null; then
            found=$(sed -n 's/.*--atleast-version=\([0-9][0-9.]*\).*/\1/p' "$s/.github/workflows/ci.yaml" 2>/dev/null | head -n 1)
            echo "FAIL: .github/workflows/ci.yaml: --atleast-version is '$found', expected '$SEED_LIBECS_MIN'"
        fi
    fi

    if [ -e "$s/package-lock.json" ]; then
        echo "FAIL: package-lock.json: file is present, expected it to be absent"
    fi
}

[ -d "$srcdir/.github" ] || echo "check-versions: no .github directory, skipping the workflow comparison"

out=$(compare_tree "$srcdir" "$builddir")
fail=0
if [ -n "$out" ]; then
    printf '%s\n' "$out"
    fail=1
fi

# Negative self-test: a wrong package.json version and a wrong readme
# requirement in a temporary copy must both be reported.
tmp=$(mktemp -d) || { echo "check-versions: mktemp failed" >&2; exit 2; }
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/src"
for f in package.json NEWS README.md; do
    [ -f "$srcdir/$f" ] && cp "$srcdir/$f" "$tmp/src/$f"
done
[ -f "$builddir/the-seed.pc" ] && cp "$builddir/the-seed.pc" "$tmp/src/the-seed.pc"
sed 's/"version"[[:space:]]*:[[:space:]]*"[^"]*"/"version": "99.99.99"/' "$tmp/src/package.json" > "$tmp/pj" &&
    mv "$tmp/pj" "$tmp/src/package.json"
printf '\nlibecs-cpp 99.99.99 or later\n' >> "$tmp/src/README.md"
selftest=$(compare_tree "$tmp/src" "$tmp/src")
printf '%s\n' "$selftest" | grep -q "^FAIL: package.json: version is '99.99.99'" || {
    echo "FAIL: self-test: a planted wrong package.json version was not reported"
    fail=1
}
printf '%s\n' "$selftest" | grep -q "^FAIL: README.md: libecs-cpp requirement is '99.99.99'" || {
    echo "FAIL: self-test: a planted wrong readme requirement was not reported"
    fail=1
}

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: check-versions: every copy agrees with version $SEED_VERSION and libecs-cpp $SEED_LIBECS_MIN"
exit 0
