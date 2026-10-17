#!/bin/sh
# Checks that the installed build metadata is all a consumer needs.
#
# The build is installed into a temporary DESTDIR. pkg-config is limited to
# that stage and to the directory holding libecs-cpp's metadata, and a copy of
# the installed the-seed.pc with the stage as its prefix stands in for a
# relocated install (the prefix is not redefined on the command line because
# that would also move libecs-cpp). Then:
#
#   1. the version and the Requires line equal the package version and the
#      libecs-cpp minimum;
#   2. the library's own Cflags carry -std=c++20 (libecs-cpp's flags carry it
#      too, so the line is read from the metadata file), and its Libs.private
#      is present;
#   3. the library's own include and library directories are under the stage
#      and its Cflags and Libs are written with them;
#   4. a consumer that includes ComponentLoader.hpp and LoadError.hpp compiles
#      and links with the flags pkg-config gives and nothing else, and when it
#      runs it gets a LoadError with reason NotFound for a missing plugin;
#   5. every installed header compiles alone;
#   6. on Linux the consumer also links statically against libthe-seed.a with
#      "pkg-config --static --libs" and runs.
#
# When cross compiling the consumer is only compiled and linked; the run and
# static steps print SKIP with the reason. Sanitizer flags of the build are
# appended to the compile and link lines. Exit status: 0 pass, 1 fail,
# 77 skipped.
#
# Environment (set by tests/Makefile.am): SEED_SRCDIR, SEED_BUILDDIR,
# SEED_PREFIX, SEED_VERSION, SEED_LIBECS_MIN, SEED_ECS_PCDIR, SEED_HOST_OS,
# SEED_SANITIZER_FLAGS, CXX, MAKE, CROSS_COMPILING.

here=$(cd "$(dirname "$0")" && pwd)
srcdir=$(cd "${SEED_SRCDIR:-$here/..}" && pwd)
builddir=$(cd "${SEED_BUILDDIR:-$srcdir}" && pwd)
prefix=${SEED_PREFIX:-/usr/local}
host_os=${SEED_HOST_OS:-linux-gnu}
make_cmd=${MAKE:-make}
cxx=${CXX:-}
sanitizer_flags=${SEED_SANITIZER_FLAGS:-}
cross=${CROSS_COMPILING:-no}
libecs_min=${SEED_LIBECS_MIN:-}
ecs_pcdir=${SEED_ECS_PCDIR:-}
version=${SEED_VERSION:-}

if [ -z "$cxx" ] || [ -z "$libecs_min" ] || [ -z "$ecs_pcdir" ] || [ -z "$version" ]; then
    echo "check-consumer: skipped, CXX, SEED_VERSION, SEED_LIBECS_MIN or SEED_ECS_PCDIR is not set"
    exit 77
fi
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "check-consumer: skipped, pkg-config is not installed"
    exit 77
fi
if [ ! -f "$builddir/Makefile" ]; then
    echo "check-consumer: skipped, $builddir is not configured"
    exit 77
fi

stage=$(mktemp -d "${TMPDIR:-/tmp}/check-consumer.XXXXXX") || exit 2
trap 'rm -rf "$stage"' EXIT HUP INT TERM

failures=0

fail()
{
    echo "FAIL: $*"
    failures=$((failures + 1))
}

if ! "$make_cmd" -C "$builddir" install DESTDIR="$stage" >"$stage/install.log" 2>&1; then
    tail -n 20 "$stage/install.log"
    fail "make install into the stage did not succeed"
    exit 1
fi

root=$stage$prefix
installed_pc=$root/lib/pkgconfig/the-seed.pc
if [ ! -f "$installed_pc" ]; then
    fail "the-seed.pc is not installed in $root/lib/pkgconfig"
    exit 1
fi

# The stage stands in for the prefix.
mkdir "$stage/pc"
sed "s|^prefix=.*|prefix=$root|" "$installed_pc" >"$stage/pc/the-seed.pc"

PKG_CONFIG_LIBDIR=$stage/pc:$ecs_pcdir
PKG_CONFIG_PATH=
export PKG_CONFIG_LIBDIR PKG_CONFIG_PATH

# ---- Metadata ----

found=$(pkg-config --modversion the-seed 2>&1)
if [ "$found" != "$version" ]; then
    fail "the-seed.pc: Version is '$found', expected '$version'"
fi

found=$(pkg-config --print-requires the-seed 2>&1)
if [ "$found" != "ecs-cpp >= $libecs_min" ]; then
    fail "the-seed.pc: Requires is '$found', expected 'ecs-cpp >= $libecs_min'"
fi

own_cflags=$(sed -n 's/^Cflags:[[:space:]]*//p' "$installed_pc")
case " $own_cflags " in
    *" -std=c++20 "*) ;;
    *) fail "the-seed.pc: Cflags is '$own_cflags', expected it to contain -std=c++20" ;;
esac

if ! grep -q '^Libs.private:[[:space:]]*[^[:space:]]' "$installed_pc"; then
    fail "the-seed.pc: Libs.private is missing or empty"
fi
case "$host_os" in
    linux*)
        private=$(sed -n 's/^Libs.private:[[:space:]]*//p' "$installed_pc")
        for lib in -lpthread -ldl; do
            case " $private " in
                *" $lib "*) ;;
                *) fail "the-seed.pc: Libs.private is '$private', expected it to contain $lib" ;;
            esac
        done
        ;;
esac

# The library's own directories are under the stage. The include and library
# directories in Cflags and Libs are written with these variables.
for variable in includedir libdir; do
    value=$(pkg-config --variable=$variable the-seed 2>&1)
    case "$value" in
        "$root"/*) ;;
        *) fail "the-seed.pc: $variable is '$value', expected a path under the stage $root" ;;
    esac
done
for variable in Cflags Libs; do
    if ! sed -n "s/^$variable:[[:space:]]*//p" "$installed_pc" | grep -q '\${\(include\|lib\)dir}'; then
        fail "the-seed.pc: $variable does not use \${includedir} or \${libdir}"
    fi
done
case " $(pkg-config --libs-only-l the-seed 2>&1) " in
    *" -lthe-seed "*) ;;
    *) fail "pkg-config --libs the-seed does not contain -lthe-seed" ;;
esac

# ---- Consumer ----

cat >"$stage/consumer.cpp" <<'CPP'
#include <libthe-seed/ComponentLoader.hpp>
#include <libthe-seed/LoadError.hpp>

#include <iostream>

int main(int argc, char **argv)
{
    the_seed::ComponentLoader loader;
    loader.PathAdd(argc > 1 ? argv[1] : ".");
    try
    {
        loader.Create("check_consumer_missing_plugin");
    }
    catch (const the_seed::LoadError &error)
    {
        if (error.ReasonGet() == the_seed::LoadError::Reason::NotFound)
        {
            return 0;
        }
        std::cerr << "unexpected reason: " << error.what() << std::endl;
        return 2;
    }
    std::cerr << "no error was thrown" << std::endl;
    return 3;
}
CPP

cflags=$(pkg-config --cflags the-seed 2>&1)
libs=$(pkg-config --libs the-seed 2>&1)

# shellcheck disable=SC2086
if ! $cxx $cflags $sanitizer_flags -o "$stage/consumer" "$stage/consumer.cpp" $libs $sanitizer_flags >"$stage/consumer.log" 2>&1; then
    cat "$stage/consumer.log"
    fail "the consumer does not compile and link with the flags from pkg-config"
else
    echo "PASS: the consumer compiles and links with the flags from pkg-config"
    if [ "$cross" = yes ]; then
        echo "SKIP: running the consumer: cross compiling, the program cannot run on this host"
    else
        ecs_libdir=$(pkg-config --variable=libdir ecs-cpp)
        if LD_LIBRARY_PATH=$root/lib:$ecs_libdir "$stage/consumer" "$stage/no-plugins-here"; then
            echo "PASS: the consumer runs and gets a LoadError with reason NotFound"
        else
            fail "the consumer did not get a LoadError with reason NotFound"
        fi
    fi
fi

# ---- Each installed header alone ----

for header in "$root"/include/libthe-seed/*.hpp; do
    name=$(basename "$header")
    printf '#include <libthe-seed/%s>\n' "$name" >"$stage/header.cpp"
    # shellcheck disable=SC2086
    if ! $cxx $cflags $sanitizer_flags -fsyntax-only "$stage/header.cpp" >"$stage/header.log" 2>&1; then
        cat "$stage/header.log"
        fail "the installed header $name does not compile alone"
    fi
done

# ---- Static link ----

case "$host_os" in
    linux*)
        if [ "$cross" = yes ]; then
            echo "SKIP: static link: cross compiling"
        else
            static_libs=$(pkg-config --static --libs the-seed 2>&1 | sed 's/-lthe-seed/-Wl,-Bstatic -lthe-seed -Wl,-Bdynamic/')
            # shellcheck disable=SC2086
            if ! $cxx $cflags $sanitizer_flags -o "$stage/consumer-static" "$stage/consumer.cpp" $static_libs $sanitizer_flags >"$stage/static.log" 2>&1; then
                cat "$stage/static.log"
                fail "the consumer does not link statically with pkg-config --static --libs"
            elif LD_LIBRARY_PATH=$(pkg-config --variable=libdir ecs-cpp) "$stage/consumer-static" "$stage/no-plugins-here"; then
                echo "PASS: the statically linked consumer runs and gets a LoadError with reason NotFound"
            else
                fail "the statically linked consumer did not get a LoadError with reason NotFound"
            fi
        fi
        ;;
    *)
        echo "SKIP: static link: only checked on Linux hosts"
        ;;
esac

if [ "$failures" -ne 0 ]; then
    echo "check-consumer: $failures problem(s)"
    exit 1
fi
echo "PASS: the installed build metadata is enough for a consumer"
exit 0
