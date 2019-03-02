#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([the-seed], [0.0.1], [tim@metaverse.systems])
AC_CONFIG_SRCDIR([Editor/Editor.cpp])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_exit])

PKG_CHECK_MODULES([ECS], ecs-cpp)

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 Editor/Makefile])
AC_OUTPUT
