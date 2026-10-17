#!/bin/sh
# Checks that README.md is the one accurate readme.
#
#   - README is absent, README.md is present, ChangeLog is absent.
#   - README.md does not tell the reader to run "make distclean" or
#     "sudo su", and does not mention a "compatibility switch".
#   - Every backticked file name in README.md exists in the source tree: a
#     token starting with src/, include/, tests/, external/ or .github/, or a
#     top-level source file name (no "/", a known source extension or an
#     all-capitals name such as NEWS). Names that only exist after autogen.sh,
#     configure or a build are on a fixed list and are not looked up.
#   - No backticked docs/ path.
#   - Every --enable-X / --disable-X in README.md is defined by an
#     AC_ARG_ENABLE in configure.ac, and every AC_ARG_ENABLE is named.
#   - The main class of each installed header (tests/public-headers.txt, apart
#     from Export.hpp) appears in the "What it offers" section.
#   - Every package in an "apt install" / "apt-get install" line is on the
#     allow-list below.
#   - The "test" script of package.json is not the npm placeholder.
#   - A negative self-test: a planted missing path and a planted unknown
#     option must both be reported.
#
# Environment: SEED_SRCDIR. Run by hand it defaults to the script's parent
# directory.
#
# Exit status: 0 pass, 1 a check failed, 2 usage problem.

here=$(cd "$(dirname "$0")" && pwd)
srcdir=${SEED_SRCDIR:-$(cd "$here/.." && pwd)}

# Packages the readme may name in an apt install line: the hosted build's
# packages, the documentation packages, and the Debian packages behind the
# tools that "the-seed build windows" uses (compiler, binutils, headers and
# runtime of mingw-w64 with POSIX threads).
ci_packages="build-essential autoconf automake libtool pkg-config osslsigncode wine wine64 clang python3 nodejs npm git doxygen graphviz"
cross_packages="g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix-runtime gcc-mingw-w64-base binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev mingw-w64-common"
allowed_packages="$ci_packages $cross_packages"

# Names that exist only after autogen.sh, configure or a build.
generated_names="configure Makefile Makefile.in Doxyfile the-seed.pc config.log test-suite.log doxygen"
source_extensions="ac am in json md txt sh yaml yml cpp hpp h m4 js ts pc"

readme_file=$srcdir/README.md
for f in "$srcdir/configure.ac" "$srcdir/package.json"; do
    [ -f "$f" ] || { echo "check-readme: $f does not exist" >&2; exit 2; }
done

# check_readme README: prints one FAIL line per problem in the readme at the
# given path; file lookups are against $srcdir.
check_readme() {
    rd=$1

    # ---- phrases ----
    grep -q 'make distclean' "$rd" && echo "FAIL: README.md: tells the reader to run 'make distclean'"
    grep -q 'sudo su' "$rd" && echo "FAIL: README.md: tells the reader to use 'sudo su'"
    grep -qi 'compatibility switch' "$rd" && echo "FAIL: README.md: mentions the 'compatibility switch'"

    # ---- backticked names (outside fenced blocks) ----
    awk '/^```/ { fence = !fence; next } !fence { print }' "$rd" |
        grep -o '`[^`]*`' | sed 's/^`//; s/`$//' | sort -u |
        while IFS= read -r tok; do
            case $tok in
                *' '*|*'<'*|*'>'*|*'*'*|*'$'*|*'{'*|*'('*|*'='*|*','*|*':'*|*'['*|'') continue ;;
            esac
            name=${tok%/}
            case $name in
                docs|docs/*)
                    echo "FAIL: README.md: names the path '$tok', and docs/ is not published"
                    continue ;;
            esac
            check=no
            case $name in
                src/*|include/*|tests/*|external/*|.github/*) check=yes ;;
                */*) ;;
                *)
                    ext=${name##*.}
                    if [ "$ext" != "$name" ]; then
                        for e in $source_extensions; do
                            [ "$ext" = "$e" ] && check=yes
                        done
                    else
                        case $name in
                            *[!A-Z]*) ;;
                            *) check=yes ;;
                        esac
                    fi ;;
            esac
            [ "$check" = yes ] || continue
            first=${name%%/*}
            skip=no
            for g in $generated_names; do
                [ "$first" = "$g" ] && skip=yes
            done
            [ "$skip" = yes ] && continue
            [ -e "$srcdir/$name" ] || echo "FAIL: README.md: '$tok' does not exist in the source tree"
        done

    # ---- configure options ----
    defined=$(sed -n 's/^[[:space:]]*AC_ARG_ENABLE(\[\([a-z-]*\)\].*/\1/p' "$srcdir/configure.ac")
    named=$(grep -o -e '--\(enable\|disable\)-[a-z][a-z-]*' "$rd" | sed 's/^--[a-z]*-//' | sort -u)
    for n in $named; do
        found=no
        for d in $defined; do
            [ "$n" = "$d" ] && found=yes
        done
        [ "$found" = yes ] || echo "FAIL: README.md: names the option '--enable-$n', which configure.ac does not define"
    done
    for d in $defined; do
        found=no
        for n in $named; do
            [ "$n" = "$d" ] && found=yes
        done
        [ "$found" = yes ] || echo "FAIL: README.md: does not name the option '--enable-$d' defined in configure.ac"
    done

    # ---- capability overview ----
    section=$(awk '/^## /{ on = ($0 ~ /[Ww]hat it offers/) ; next } on { print }' "$rd")
    if [ -z "$section" ]; then
        echo "FAIL: README.md: has no 'What it offers' section"
    elif [ -f "$srcdir/tests/public-headers.txt" ]; then
        grep -v '^#' "$srcdir/tests/public-headers.txt" | sed 's/\.hpp$//' | while read -r cls; do
            [ -n "$cls" ] || continue
            [ "$cls" = Export ] && continue
            printf '%s\n' "$section" | grep -q "$cls" ||
                echo "FAIL: README.md: the 'What it offers' section does not name $cls"
        done
    else
        echo "FAIL: tests/public-headers.txt does not exist"
    fi

    # ---- apt packages ----
    awk '
        { line = $0 }
        cont { buf = buf " " line; cont = (line ~ /\\$/); if (!cont) print buf; next }
        line ~ /apt(-get)?[[:space:]]+install/ { buf = line; cont = (line ~ /\\$/); if (!cont) print buf }
    ' "$rd" | sed 's/\\//g; s/.*apt\(-get\)\{0,1\}[[:space:]]\{1,\}install//' |
        tr -s ' \t`' '\n\n\n' | while read -r pkg; do
            case $pkg in ''|-*|*'$'*|*'<'*) continue ;; esac
            ok=no
            for a in $allowed_packages; do
                [ "$pkg" = "$a" ] && ok=yes
            done
            [ "$ok" = yes ] || echo "FAIL: README.md: apt package '$pkg' is not on the allow-list of check-readme.sh"
        done
}

out=""
fail=0
problem() { out="$out$1
"; fail=1; }

[ -e "$srcdir/README" ] && problem "FAIL: README: the duplicate readme is present, expected it to be absent"
[ -f "$srcdir/README.md" ] || problem "FAIL: README.md: file is missing"
[ -e "$srcdir/ChangeLog" ] && problem "FAIL: ChangeLog: file is present, expected it to be absent"

if [ -f "$readme_file" ]; then
    res=$(check_readme "$readme_file")
    [ -n "$res" ] && problem "$res"
fi

# package.json: the test script is not the npm placeholder.
if command -v node >/dev/null 2>&1; then
    testcmd=$(node -e 'const p=require(process.argv[1]);console.log((p.scripts||{}).test||"")' "$srcdir/package.json" 2>/dev/null)
else
    testcmd=$(sed -n 's/^[[:space:]]*"test"[[:space:]]*:[[:space:]]*"\(.*\)",\{0,1\}[[:space:]]*$/\1/p' "$srcdir/package.json" | head -n 1)
fi
case $testcmd in
    ''|*'no test specified'*)
        problem "FAIL: package.json: scripts.test is '$testcmd', expected a command that runs the tests" ;;
esac

# Negative self-test: plant a missing path and an unknown option in a copy of
# the readme; both must be reported.
if [ -f "$readme_file" ]; then
    tmp=$(mktemp -d) || { echo "check-readme: mktemp failed" >&2; exit 2; }
    trap 'rm -rf "$tmp"' EXIT HUP INT TERM
    cp "$readme_file" "$tmp/README.md"
    printf '\nSee `src/NoSuchFile.cpp` and use --enable-no-such-option.\n' >> "$tmp/README.md"
    selftest=$(check_readme "$tmp/README.md")
    printf '%s\n' "$selftest" | grep -q "'src/NoSuchFile.cpp' does not exist" ||
        problem "FAIL: self-test: a planted missing path was not reported"
    printf '%s\n' "$selftest" | grep -q "option '--enable-no-such-option'" ||
        problem "FAIL: self-test: a planted unknown option was not reported"
fi

if [ "$fail" -ne 0 ]; then
    printf '%s' "$out"
    exit 1
fi
echo "PASS: check-readme: the readme matches the source tree, the options and the package test command"
exit 0
