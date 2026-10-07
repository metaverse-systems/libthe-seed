#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libthe-seed], [0.4.1], [tim@metaverse.systems])
LT_INIT
AC_CONFIG_SRCDIR([src/LibraryLoader.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

# Checks for programs.
AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_exit])

PKG_CHECK_MODULES([LIBECS], [ecs-cpp >= 2.0.0])
dnl AC_CHECK_FILE refuses to run when cross compiling, so test the file directly.
AS_IF([test -f "$srcdir/external/Catch2/extras/catch_amalgamated.hpp"],
    [],
    [AC_MSG_ERROR([external/Catch2 is missing: run git submodule update --init])])

# Optional memory-error checking: --enable-sanitizer=address builds the library
# and the tests with AddressSanitizer and UndefinedBehaviorSanitizer.
AC_ARG_ENABLE([sanitizer],
    [AS_HELP_STRING([--enable-sanitizer@<:@=no|address@:>@],
        [build with address and undefined-behavior checking (default: no)])],
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
    *)
        AC_MSG_ERROR([invalid value '$enable_sanitizer' for --enable-sanitizer: use no or address])
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

AC_CONFIG_FILES([Makefile
                 the-seed.pc
                 src/Makefile
                 Doxyfile
		 tests/Makefile])
AC_OUTPUT
