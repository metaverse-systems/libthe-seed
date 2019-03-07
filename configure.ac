#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([the-seed], [0.0.1], [tim@metaverse.systems])
LT_INIT
AC_CONFIG_SRCDIR([Editor/Editor.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_exit])

PKG_CHECK_MODULES([LIBECS], ecs-cpp)

# Checks for header files.

AC_CHECK_HEADER([windows.h],
    [LIBS="$LIBS"],
    [LIBS="$LIBS -ldl"],
    [])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 Loaders/Makefile
                 Editor/Makefile])
AC_OUTPUT
