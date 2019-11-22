ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libdraw.la
libdraw_la_SOURCES = draw.cpp
libdraw_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libdraw_la_LDFLAGS = -no-undefined
libdraw_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
