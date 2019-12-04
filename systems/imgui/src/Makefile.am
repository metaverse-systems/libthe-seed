ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libimgui.la
libimgui_la_SOURCES = imgui.cpp
libimgui_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libimgui_la_LDFLAGS = -no-undefined
libimgui_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
