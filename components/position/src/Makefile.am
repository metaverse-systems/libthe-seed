ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libposition.la
libposition_la_SOURCES = position.cpp
libposition_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libposition_la_LDFLAGS = -no-undefined
libposition_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
