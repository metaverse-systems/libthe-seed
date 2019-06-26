AC_PREREQ([2.69])
AC_INIT([wxwidgets_gui], [0.0.1], [tim@metaverse.systems])
AC_CONFIG_HEADERS([config.h])
AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT([disable-static])

AC_CONFIG_MACRO_DIR([m4])

AM_OPTIONS_WXCONFIG
reqwx=3.1.0
AM_PATH_WXCONFIG($reqwx, wxWin=1,,[all])
if test "$wxWin" != 1; then
        AC_MSG_ERROR([
                wxWidgets must be installed on your system.

                Please check that wx-config is in path, the directory
                where wxWidgets libraries are installed (returned by
                'wx-config --libs' or 'wx-config --static --libs' command)
                is in LD_LIBRARY_PATH or equivalent variable and
                wxWidgets version is $reqwx or above.
                ])
fi

CPPFLAGS="$CPPFLAGS $WX_CPPFLAGS"
CXXFLAGS="$CXXFLAGS $WX_CXXFLAGS_ONLY"
CFLAGS="$CFLAGS $WX_CFLAGS_ONLY"
LIBS="$LIBS $WX_LIBS"

PKG_CHECK_MODULES([LIBECS], ecs-cpp)
PKG_CHECK_MODULES([JSONCPP], jsoncpp)

AC_OUTPUT(Makefile src/Makefile)
