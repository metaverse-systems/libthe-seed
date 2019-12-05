ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libgui.la
libgui_la_SOURCES = gui.cpp
libgui_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libgui_la_LDFLAGS = -no-undefined
libgui_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
