#!/bin/sh
# Runs one Catch2 test program, applying the known-gap list.
#
#   run-test.sh <program>
#
# Environment:
#   SEED_SANITIZER         none or address
#   SEED_KNOWN_GAPS_FILE   path to the known-gap list
#   SEED_TEST_PROGRAMS     space separated basenames of the test programs

TAB=$(printf '\t')

if [ $# -lt 1 ]; then
    echo "run-test.sh: no test program given" >&2
    exit 99
fi
case "${SEED_SANITIZER:-}" in
none | address) ;;
*)
    echo "run-test.sh: SEED_SANITIZER must be none or address, got '${SEED_SANITIZER:-}'" >&2
    exit 99
    ;;
esac
if [ ! -f "${SEED_KNOWN_GAPS_FILE:-}" ]; then
    echo "run-test.sh: known-gap file not found: '${SEED_KNOWN_GAPS_FILE:-}'" >&2
    exit 99
fi
if [ -z "${SEED_TEST_PROGRAMS:-}" ]; then
    echo "run-test.sh: SEED_TEST_PROGRAMS is empty" >&2
    exit 99
fi

PROG=$(basename "$1")
ABS=$(cd "$(dirname "$1")" && pwd)/$PROG

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/seed-test.XXXXXX") || exit 99
WORK=$(mktemp -d "${TMPDIR:-/tmp}/seed-wrapper.XXXXXX") || exit 99
trap 'rm -rf "$SCRATCH" "$WORK"' EXIT
trap 'exit 99' INT TERM

# Validate every entry; write the entries that apply to this program and
# variant to $WORK/entries, tab separated: case, signature, finding, fixed by.
awk -v programs="$SEED_TEST_PROGRAMS" -v prog="$PROG" -v san="$SEED_SANITIZER" \
    -v entries="$WORK/entries" '
function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
function bad(msg) { printf "MALFORMED KNOWN GAP: line %d: %s\n", NR, msg; failed = 1 }
BEGIN {
    n = split(programs, plist, " ")
    for (i = 1; i <= n; i++) known[plist[i]] = 1
    bare["FAILED"] = 1
    bare["AddressSanitizer"] = 1
    bare["UndefinedBehaviorSanitizer"] = 1
    bare["LeakSanitizer"] = 1
    bare["ThreadSanitizer"] = 1
    bare["ASan"] = 1
    bare["UBSan"] = 1
}
{
    line = trim($0)
    if (line == "" || substr(line, 1, 1) == "#") next
    count = 0
    rest = line
    while ((p = index(rest, " | ")) > 0) {
        count++
        f[count] = trim(substr(rest, 1, p - 1))
        rest = substr(rest, p + 3)
    }
    count++
    f[count] = trim(rest)
    if (count != 6) { bad("expected 6 fields, found " count); next }
    if (f[1] != "all" && f[1] != "address") { bad("variant must be all or address, got " f[1]); next }
    if (!(f[2] in known)) { bad("program not in the test program list: " f[2]); next }
    if (f[3] == "") { bad("empty test case"); next }
    if (f[4] == "") { bad("empty signature"); next }
    if (f[4] in bare) { bad("signature must be specific, not " f[4]); next }
    if (f[5] !~ /^libthe-seed-1 finding [0-9]+$/) { bad("finding must read: libthe-seed-1 finding <N>"); next }
    if (f[6] !~ /^libthe-seed-1: .+$/) { bad("fixed by must read: libthe-seed-1: <task title>"); next }
    key = f[1] SUBSEP f[2] SUBSEP f[3]
    if (key in seen) { bad("duplicate entry for " f[2] " \"" f[3] "\" (" f[1] ")"); next }
    seen[key] = 1
    if (f[2] == prog && (f[1] == "all" || (f[1] == "address" && san == "address")))
        printf "%s\t%s\t%s\t%s\n", f[3], f[4], f[5], f[6] >> entries
}
END { exit failed ? 1 : 0 }
' "$SEED_KNOWN_GAPS_FILE" >"$WORK/malformed"
if [ $? -ne 0 ]; then
    cat "$WORK/malformed"
    exit 1
fi
[ -f "$WORK/entries" ] || : >"$WORK/entries"

RESULT=0

# Run a program command in the scratch directory with TMPDIR pointing there.
run() {
    (cd "$SCRATCH" && TMPDIR=$SCRATCH "$@")
}

if [ ! -s "$WORK/entries" ]; then
    run "$ABS"
    RESULT=$?
else
    EXCLUDE=
    while IFS="$TAB" read -r tcase sig finding fixedby; do
        EXCLUDE="$EXCLUDE ~\"$tcase\""
    done <"$WORK/entries"
    EXCLUDE=${EXCLUDE# }

    run "$ABS" --allow-running-no-tests "$EXCLUDE"
    if [ $? -ne 0 ]; then
        echo "FAIL: non-listed test cases failed"
        RESULT=1
    fi

    while IFS="$TAB" read -r tcase sig finding fixedby; do
        run "$ABS" "\"$tcase\"" >"$WORK/case-out" 2>&1
        status=$?
        cat "$WORK/case-out"
        if grep -F -q -- "No test cases matched" "$WORK/case-out"; then
            echo "MALFORMED KNOWN GAP: test case not found: $PROG \"$tcase\""
            RESULT=1
        elif [ $status -ne 0 ]; then
            if grep -F -q -- "$sig" "$WORK/case-out"; then
                echo "KNOWN GAP: $PROG \"$tcase\" ($sig): $finding; fixed by $fixedby"
            else
                echo "FAIL: listed case failed without expected signature '$sig'"
                RESULT=1
            fi
        else
            echo "STALE KNOWN GAP: $PROG \"$tcase\" passed under $SEED_SANITIZER; remove this entry ($finding; $fixedby)"
        fi
    done <"$WORK/entries"
fi

LEFT=$(ls -A "$SCRATCH" | tr '\n' ' ')
if [ -n "$LEFT" ]; then
    echo "FAIL: $PROG left files in its temporary directory: $LEFT"
    RESULT=1
fi

exit $RESULT
