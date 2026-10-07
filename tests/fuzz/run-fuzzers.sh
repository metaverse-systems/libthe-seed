#!/bin/sh
# Runs every fuzz target for a fixed time:
#
#   run-fuzzers.sh [seconds per target] [seed]
#
# Defaults: 60 seconds and a random seed, which is printed. The fuzz binaries
# come from build-fuzzers.sh.
#
# Environment:
#   FUZZ_BUILD_DIR   directory holding fuzz-<target> (default: ./fuzz-build)
#   FUZZ_OUT_DIR     working corpora, artifacts and logs (default: ./fuzz-out)
#   FUZZ_ELF_SEEDS   extra ELF files to seed the elf corpus (space separated)
#
# Each target gets a fresh working corpus seeded with the samples, the inputs
# built in code and the committed corpus in tests/fuzz/corpus/<target>/.
# Crashes, timeouts, out-of-memory stops and leaks leave their artifacts in
# $FUZZ_OUT_DIR/<target>-* and make the script exit non-zero.

set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
seconds=${1:-60}
seed=${2:-$(awk 'BEGIN { srand(); print int(rand() * 2147483646) + 1 }')}
build=${FUZZ_BUILD_DIR:-$PWD/fuzz-build}
out=${FUZZ_OUT_DIR:-$PWD/fuzz-out}

case "$seconds" in
'' | *[!0-9]* | 0)
    echo "run-fuzzers: seconds must be a positive integer, got '$seconds'" >&2
    exit 2
    ;;
esac
case "$seed" in
'' | *[!0-9]*)
    echo "run-fuzzers: seed must be a number, got '$seed'" >&2
    exit 2
    ;;
esac

mkdir -p "$out"
echo "run-fuzzers: $seconds seconds per target, seed $seed"
echo "run-fuzzers: binaries in $build, output in $out"

failed=0
for target in elf pe macho superblob msi; do
    binary=$build/fuzz-$target
    if [ ! -x "$binary" ]; then
        echo "run-fuzzers: $binary not found; run build-fuzzers.sh first" >&2
        exit 2
    fi

    corpus=$out/corpus-$target
    rm -rf "$corpus"
    mkdir -p "$corpus"

    case "$target" in
    elf)
        for file in ${FUZZ_ELF_SEEDS:-}; do
            [ -f "$file" ] && cp "$file" "$corpus/seed-$(basename "$file")"
        done
        ;;
    pe)
        cp "$top/tests/fixtures/tiny.exe" "$top/tests/fixtures/test.dll" "$corpus/"
        ;;
    macho)
        # Every genuine Mac sample: unsigned, signed by another tool, no room,
        # exactly enough room, 64-bit table, library, data after the signature.
        cp "$top"/tests/fixtures/tiny-macho-* "$corpus/"
        ;;
    msi)
        cp "$top/tests/fixtures/tiny.msi" "$corpus/"
        ;;
    esac
    # Seeds that are built in code (two ELF images, a SuperBlob).
    SEED_FUZZ_WRITE_SEEDS=$corpus "$binary" >/dev/null 2>&1
    # Findings committed to the repository.
    for file in "$top/tests/fuzz/corpus/$target"/*; do
        [ -f "$file" ] && cp "$file" "$corpus/"
    done

    log=$out/$target.log
    echo "run-fuzzers: $target ($(ls "$corpus" | wc -l | tr -d ' ') seed files)"
    "$binary" "$corpus" \
        -max_total_time="$seconds" -seed="$seed" -max_len=65536 -timeout=1 \
        -malloc_limit_mb=32 -rss_limit_mb=2048 -print_final_stats=1 \
        -artifact_prefix="$out/$target-" >"$log" 2>&1
    status=$?
    grep -E '^(stat::number_of_executed_units|stat::peak_rss_mb|#[0-9]+.*(DONE|cov:))' "$log" | tail -n 3
    if [ "$status" -ne 0 ]; then
        echo "run-fuzzers: $target FAILED with status $status; see $log" >&2
        tail -n 20 "$log" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "run-fuzzers: findings are in $out" >&2
    exit 1
fi
echo "run-fuzzers: no findings"
