ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libsong.la
libsong_la_SOURCES = song.cpp
libsong_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libsong_la_LDFLAGS = -no-undefined
libsong_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
