#!/bin/sh
# Builds one libFuzzer binary per target into the build directory:
#
#   build-fuzzers.sh <build dir>
#
# Needs clang++ with libFuzzer. The binary-tooling sources are compiled
# directly (no Autotools, no libecs-cpp), so the default and sanitizer builds
# are not affected. Produces <build dir>/fuzz-<target> for elf, pe, macho,
# superblob and msi.

set -eu

if [ $# -ne 1 ]; then
    echo "usage: build-fuzzers.sh <build dir>" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
mkdir -p "$1"
out=$(cd "$1" && pwd)
cxx=${CXX:-clang++}

if ! command -v "$cxx" >/dev/null 2>&1; then
    echo "build-fuzzers: $cxx not found; install clang" >&2
    exit 1
fi

probe=$out/probe.cpp
cat > "$probe" <<'PROBE'
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *, std::size_t) { return 0; }
PROBE
if ! "$cxx" -std=c++20 -fsanitize=fuzzer,address,undefined "$probe" -o "$out/probe" >"$out/probe.log" 2>&1; then
    cat "$out/probe.log" >&2
    echo "build-fuzzers: $cxx cannot build with -fsanitize=fuzzer (libFuzzer missing?)." >&2
    echo "build-fuzzers: on Debian and Ubuntu install the libclang-rt-<version>-dev package matching clang." >&2
    exit 1
fi
rm -f "$probe" "$out/probe" "$out/probe.log"

flags="-std=c++20 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"
includes="-I$top/include -I$top/src -I$top/tests -I$here"

sources="ElfParser.cpp PeParser.cpp PeSigner.cpp MachOParser.cpp MachOSigner.cpp MsiSigner.cpp DependencyLister.cpp internal/FileIO.cpp"

objects=
for source in $sources; do
    object=$out/lib-$(echo "$source" | tr '/' '_').o
    "$cxx" $flags $includes -c "$top/src/$source" -o "$object"
    objects="$objects $object"
done

"$cxx" $flags $includes -c "$here/FuzzTargets.cpp" -o "$out/FuzzTargets.o"

for target in elf:FuzzElf pe:FuzzPe macho:FuzzMachO superblob:FuzzSuperBlob msi:FuzzMsi; do
    name=${target%%:*}
    function=${target#*:}
    "$cxx" $flags $includes -DSEED_FUZZ_TARGET="$function" -c "$here/fuzz_entry.cpp" -o "$out/entry-$name.o"
    "$cxx" $flags "$out/entry-$name.o" "$out/FuzzTargets.o" $objects -o "$out/fuzz-$name"
    echo "built $out/fuzz-$name"
done
