AC_PREREQ([2.69])
AC_INIT([sdl_mixer], [0.0.1], [tim@metaverse.systems], [sdl_mixer], [https://metaverse.systems])
AC_CONFIG_HEADERS([config.h])
AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT([disable-static])

AC_CONFIG_MACRO_DIR([m4])

PKG_CHECK_MODULES([LIBECS], ecs-cpp)
PKG_CHECK_MODULES([JSONCPP], jsoncpp)
PKG_CHECK_MODULES([SDL2], sdl2)
PKG_CHECK_MODULES([SDL2_MIXER], SDL2_mixer)

AC_OUTPUT(Makefile src/Makefile)
