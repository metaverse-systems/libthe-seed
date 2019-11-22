ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libcell.la
libcell_la_SOURCES = cell.cpp
libcell_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libcell_la_LDFLAGS = -no-undefined
libcell_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
