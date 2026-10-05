#!/bin/sh
# Self-test for run-test.sh. Uses fake shell "Catch2 programs" so the wrapper's
# decisions can be checked without building anything.
#
# Environment (set by the Makefile; defaults allow running by hand):
#   SEED_TESTS_SRCDIR     directory holding run-test.sh and the test sources
#   SEED_TEST_PROGRAMS    the programs the build knows about

SRCDIR=${SEED_TESTS_SRCDIR:-$(cd "$(dirname "$0")" && pwd)}
WRAPPER=$SRCDIR/run-test.sh
START_DIR=$(pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/run-test-selftest.XXXXXX") || exit 99
trap 'rm -rf "$WORK"' EXIT INT TERM

FAILED=0
TOTAL=0

ok() { printf 'ok: %s\n' "$1"; }
bad() {
    printf 'not ok: %s\n' "$1"
    FAILED=$((FAILED + 1))
}

# Build a fake Catch2 program.
#   $1 name  $2 space separated cases  $3 failing cases as case=signature,...
#   $4 extra shell code run before the cases
mkfake() {
    name=$1
    cases=$2
    failing=$3
    extra=$4
    {
        printf '#!/bin/sh\n'
        printf 'CASES="%s"\n' "$cases"
        printf 'FAILING="%s"\n' "$failing"
        printf '%s\n' "$extra"
        cat <<'BODY'
allow=0
if [ "$1" = "--allow-running-no-tests" ]; then
    allow=1
    shift
fi
spec=$1
selected=
case "$spec" in
"") selected=$CASES ;;
\~*)
    excluded=$(printf '%s\n' "$spec" | grep -o '"[^"]*"' | tr -d '"')
    for c in $CASES; do
        skip=0
        for x in $excluded; do [ "$c" = "$x" ] && skip=1; done
        [ $skip -eq 0 ] && selected="$selected $c"
    done
    ;;
*)
    want=$(printf '%s\n' "$spec" | tr -d '"')
    for c in $CASES; do
        [ "$c" = "$want" ] && selected=$c
    done
    ;;
esac
if [ -z "$selected" ]; then
    echo "No test cases matched '$spec'"
    [ $allow -eq 1 ] && exit 0
    exit 2
fi
rc=0
for c in $selected; do
    sig=
    for f in $FAILING; do
        [ "${f%%=*}" = "$c" ] && sig=${f#*=}
    done
    if [ -n "$sig" ]; then
        echo "$c: FAILED: $sig"
        rc=1
    else
        echo "$c: passed"
    fi
done
exit $rc
BODY
    } >"$WORK/bin/$name"
    chmod +x "$WORK/bin/$name"
}

mkdir -p "$WORK/bin"

# Run the wrapper. $1 variant, $2 program name, $3 gap file contents,
# $4 program list. Output goes to $WORK/out, status to RC.
runwrap() {
    variant=$1
    prog=$2
    printf '%s' "$3" >"$WORK/gaps.txt"
    programs=${4-"test_a test_b test_c"}
    SEED_SANITIZER=$variant \
        SEED_KNOWN_GAPS_FILE=$WORK/gaps.txt \
        SEED_TEST_PROGRAMS=$programs \
        sh "$WRAPPER" "$WORK/bin/$prog" >"$WORK/out" 2>&1
    RC=$?
}

# check <description> <expected status: zero|nonzero|N> [output substring]
check() {
    desc=$1
    want=$2
    pattern=$3
    TOTAL=$((TOTAL + 1))
    good=1
    case "$want" in
    zero) [ "$RC" -eq 0 ] || good=0 ;;
    nonzero) [ "$RC" -ne 0 ] || good=0 ;;
    *) [ "$RC" -eq "$want" ] || good=0 ;;
    esac
    if [ -n "$pattern" ] && ! grep -F -q -- "$pattern" "$WORK/out"; then
        good=0
    fi
    if [ $good -eq 1 ]; then
        ok "$desc"
    else
        bad "$desc (status $RC, wanted $want, pattern '$pattern')"
        sed 's/^/    | /' "$WORK/out"
    fi
}

GAP='all | test_a | beta | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'

# 1
mkfake test_a "alpha beta" "alpha=oops" ""
runwrap none test_a ""
check "1 failing program, no entries" nonzero

# 2
mkfake test_a "alpha beta" "" ""
runwrap none test_a ""
check "2 passing program" zero

# 3
mkfake test_a "alpha beta" "beta=boom" ""
runwrap none test_a "$GAP
"
check "3a known gap under none" zero "KNOWN GAP:"
runwrap address test_a "$GAP
"
check "3b known gap under address" zero "KNOWN GAP:"

# 4
mkfake test_a "alpha beta" "beta=heap-use-after-free" ""
runwrap none test_a 'address | test_a | beta | heap-use-after-free | libthe-seed-1 finding 2 | libthe-seed-1: Some task title
'
check "4 address entry ignored under none" nonzero
if grep -q "KNOWN GAP:" "$WORK/out"; then
    bad "4 address entry must not be applied under none"
fi

# 5
mkfake test_a "alpha beta" "beta=other" ""
runwrap none test_a "$GAP
"
check "5 different signature" nonzero "FAIL: listed case failed without expected signature"

# 6
mkfake test_a "alpha beta" "alpha=oops beta=boom" ""
runwrap none test_a "$GAP
"
check "6 non-listed case fails" nonzero "FAIL: non-listed test cases failed"

# 7
mkfake test_a "alpha beta" "" ""
runwrap none test_a "$GAP
"
check "7 stale entry" zero "STALE KNOWN GAP:"

# 8 malformed lines, each separately
mkfake test_a "alpha beta" "beta=boom" ""
malformed() {
    runwrap none test_a "$2
"
    check "8 malformed: $1" nonzero "MALFORMED KNOWN GAP:"
}
malformed "five fields" 'all | test_a | beta | boom | libthe-seed-1 finding 1'
malformed "variant plain" 'plain | test_a | beta | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'
malformed "variant thread" 'thread | test_a | beta | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'
malformed "bare sanitizer name" 'address | test_a | beta | AddressSanitizer | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'
malformed "FAILED as signature" 'all | test_a | beta | FAILED | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'
malformed "empty signature" 'all | test_a | beta |  | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'
malformed "duplicate key" "$GAP
$GAP"
malformed "program not in list" 'all | test_zzz | beta | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title'
malformed "bad finding" 'all | test_a | beta | boom | finding 1 | libthe-seed-1: Some task title'
malformed "bad fixed by" 'all | test_a | beta | boom | libthe-seed-1 finding 1 | T012'
# malformed entries are reported under the other variant too
runwrap address test_a 'all | test_a | beta | boom | libthe-seed-1 finding 1'
check "8 malformed under address" nonzero "MALFORMED KNOWN GAP:"

# 9
mkfake test_a "alpha beta" "" ""
runwrap none test_a 'all | test_a | nosuchcase | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title
'
check "9 unknown test case" nonzero "MALFORMED KNOWN GAP: test case not found"

# 10
mkfake test_a "alpha beta" "alpha=boom beta=boom" ""
runwrap none test_a 'all | test_a | alpha | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title
all | test_a | beta | boom | libthe-seed-1 finding 1 | libthe-seed-1: Some task title
'
check "10 every case listed and hit" zero "KNOWN GAP:"

# 11 input errors
mkfake test_a "alpha" "" ""
runwrap bogus test_a ""
check "11a unknown SEED_SANITIZER" 99
printf '' >"$WORK/gaps.txt"
SEED_SANITIZER=none SEED_KNOWN_GAPS_FILE=$WORK/missing.txt SEED_TEST_PROGRAMS=test_a \
    sh "$WRAPPER" "$WORK/bin/test_a" >"$WORK/out" 2>&1
RC=$?
check "11b missing known-gap file" 99
runwrap none test_a "" ""
check "11c empty program list" 99

# 12
mkfake test_a "alpha" "" 'touch "$TMPDIR/leftover"'
runwrap none test_a ""
check "12a leftover file in TMPDIR" nonzero "left files"
mkfake test_a "alpha" "" 'touch "$TMPDIR/tmp"; rm -f "$TMPDIR/tmp"'
runwrap none test_a ""
check "12b clean program" zero

# 13
mkfake test_a "alpha" "" 'echo "CWD=$(pwd -P)"; echo "ENTRIES=$(ls -A | wc -l | tr -d " ")"'
cd "$START_DIR" || exit 99
runwrap none test_a ""
check "13a program runs" zero
cwd=$(sed -n 's/^CWD=//p' "$WORK/out" | head -n 1)
entries=$(sed -n 's/^ENTRIES=//p' "$WORK/out" | head -n 1)
TOTAL=$((TOTAL + 1))
if [ -n "$cwd" ] && [ "$cwd" != "$(pwd -P)" ] && [ "$entries" = "0" ]; then
    ok "13b cwd is a fresh empty directory"
else
    bad "13b cwd '$cwd' entries '$entries'"
fi

# 14 two programs together
for n in test_a test_b; do
    mkfake $n "alpha" "" 'touch "$PWD/mine-'"$n"'"; sleep 1; others=$(ls -A | grep -v "^mine-'"$n"'$"); rm -f "$PWD/mine-'"$n"'"; if [ -n "$others" ]; then echo "SAW: $others"; exit 1; fi'
done
printf '' >"$WORK/gaps.txt"
for n in test_a test_b; do
    (
        SEED_SANITIZER=none SEED_KNOWN_GAPS_FILE=$WORK/gaps.txt \
            SEED_TEST_PROGRAMS="test_a test_b" \
            sh "$WRAPPER" "$WORK/bin/$n" >"$WORK/out-$n" 2>&1
        echo $? >"$WORK/rc-$n"
    ) &
done
wait
TOTAL=$((TOTAL + 1))
if [ "$(cat "$WORK/rc-test_a")" = 0 ] && [ "$(cat "$WORK/rc-test_b")" = 0 ]; then
    ok "14 concurrent programs are isolated"
else
    bad "14 concurrent programs see each other's files"
    cat "$WORK/out-test_a" "$WORK/out-test_b"
fi

# 15 every test source is registered
TOTAL=$((TOTAL + 1))
missing=
for src in "$SRCDIR"/test_*.cpp; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .cpp)
    found=0
    for p in $SEED_TEST_PROGRAMS; do
        [ "$p" = "$base" ] && found=1
    done
    [ $found -eq 1 ] || missing="$missing $base"
done
if [ -z "$missing" ]; then
    ok "15 every test source is in SEED_TEST_PROGRAMS"
else
    bad "15 not in SEED_TEST_PROGRAMS:$missing"
fi

printf '%s checks, %s failed\n' "$TOTAL" "$FAILED"
[ $FAILED -eq 0 ]
