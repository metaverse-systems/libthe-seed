#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libthe-seed], [0.0.4], [tim@metaverse.systems])
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

PKG_CHECK_MODULES([LIBECS], ecs-cpp)
PKG_CHECK_MODULES([CATCH2], catch2)

# Checks for header files.

AC_CHECK_HEADER([windows.h],
    [LIBS="$LIBS"],
    [
        LIBS="$LIBS -ldl"
        PKG_CHECK_MODULES([LIEF], LIEF)
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
