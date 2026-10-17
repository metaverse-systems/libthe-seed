#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
m4_define([the_seed_libecs_min], [2.1.0])
AC_INIT([libthe-seed], [0.7.0], [tim@metaverse.systems], [libthe-seed], [https://github.com/metaverse-systems/libthe-seed])
LT_INIT
AC_CONFIG_SRCDIR([src/LibraryLoader.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

# Checks for programs.
AC_PROG_CXX
AC_CHECK_TOOL([CXXFILT], [c++filt])
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_exit])

LIBECS_MIN_VERSION=the_seed_libecs_min
AC_SUBST([LIBECS_MIN_VERSION])
PKG_CHECK_MODULES([LIBECS], [ecs-cpp >= the_seed_libecs_min])
# The directory holding libecs-cpp's metadata; the install checks limit
# pkg-config to it and to a staged install.
ECS_PCDIR=`$PKG_CONFIG --variable=pcfiledir ecs-cpp`
AC_SUBST([ECS_PCDIR])
# The test framework is the vendored Catch2 submodule. Building and installing
# the library never needs it; only "make check" does.
# --enable-tests=auto (default) enables the tests when the framework is present,
# yes requires it, no does not look for it.
dnl AC_CHECK_FILE refuses to run when cross compiling, so test the file directly.
AC_ARG_ENABLE([tests],
    [AS_HELP_STRING([--enable-tests@<:@=auto|yes|no@:>@],
        [build and run the tests, which need the framework in external/Catch2 (default: auto, enabled when the framework is present)])],
    [],
    [enable_tests=auto])
case "$enable_tests" in
    yes|no|auto) ;;
    *)
        AC_MSG_ERROR([invalid value '$enable_tests' for --enable-tests; accepted values are: auto, yes, no])
        ;;
esac
tests_status=
AS_IF([test "x$enable_tests" = xno],
    [tests_status="no (--enable-tests=no)"
     tests_enabled=no],
    [AS_IF([test -f "$srcdir/external/Catch2/extras/catch_amalgamated.hpp"],
        [tests_status="yes (found)"
         tests_enabled=yes],
        [AS_IF([test "x$enable_tests" = xyes],
            [AC_MSG_ERROR([--enable-tests=yes requires the test framework in external/Catch2; run: git submodule update --init external/Catch2])],
            [tests_status="no (not found)"
             tests_enabled=no])])])
AM_CONDITIONAL([ENABLE_TESTS], [test "x$tests_enabled" = xyes])
AM_CONDITIONAL([TESTS_DISABLED_BY_OPTION], [test "x$enable_tests" = xno])

# Warnings are always shown. --enable-werror=yes makes them fatal in the
# library, the tests and the test plugins; the default leaves them as warnings
# so that a newer compiler never stops an installation.
AC_ARG_ENABLE([werror],
    [AS_HELP_STRING([--enable-werror@<:@=no|yes@:>@],
        [treat compiler warnings as errors in the library and the tests (default: no)])],
    [],
    [enable_werror=no])
case "$enable_werror" in
    no)
        WERROR_CXXFLAGS=
        ;;
    yes)
        WERROR_CXXFLAGS="-Werror"
        ;;
    *)
        AC_MSG_ERROR([invalid value '$enable_werror' for --enable-werror; accepted values are: no, yes])
        ;;
esac
WARN_CXXFLAGS="-Wall -Wextra"
AC_SUBST([WARN_CXXFLAGS])
AC_SUBST([WERROR_CXXFLAGS])

# Optional runtime checking: --enable-sanitizer=address builds the library and
# the tests with AddressSanitizer and UndefinedBehaviorSanitizer;
# --enable-sanitizer=thread builds them with ThreadSanitizer.
AC_ARG_ENABLE([sanitizer],
    [AS_HELP_STRING([--enable-sanitizer@<:@=no|address|thread@:>@],
        [build with address and undefined-behavior checking, or with thread checking (default: no)])],
    [],
    [enable_sanitizer=no])
case "$enable_sanitizer" in
    no)
        SANITIZER=none
        SANITIZER_CXXFLAGS=
        SANITIZER_LDFLAGS=
        ;;
    address)
        SANITIZER=address
        SANITIZER_CXXFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1"
        SANITIZER_LDFLAGS="-fsanitize=address,undefined"
        ;;
    thread)
        SANITIZER=thread
        SANITIZER_CXXFLAGS="-fsanitize=thread -fno-omit-frame-pointer -g -O1"
        SANITIZER_LDFLAGS="-fsanitize=thread"
        ;;
    *)
        AC_MSG_ERROR([invalid value '$enable_sanitizer' for --enable-sanitizer: use no, address or thread])
        ;;
esac
AC_SUBST([SANITIZER])
AC_SUBST([SANITIZER_CXXFLAGS])
AC_SUBST([SANITIZER_LDFLAGS])

AM_CONDITIONAL([CROSS_COMPILING], [test "x$cross_compiling" = xyes])

# Checks for header files.

AC_CHECK_HEADER([windows.h],
    [LIBS="$LIBS"],
    [
        LIBS="$LIBS -ldl"
    ],
    [])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# The documentation target is optional.
AC_CHECK_PROG([DOXYGEN], [doxygen], [doxygen])

AC_CONFIG_FILES([Makefile
                 the-seed.pc
                 src/Makefile
                 Doxyfile
		 tests/Makefile])
AC_OUTPUT

AC_MSG_NOTICE([tests: $tests_status])
AC_MSG_NOTICE([warnings as errors: $enable_werror])
AC_MSG_NOTICE([sanitizer: $SANITIZER])
