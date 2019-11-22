ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = liblife2.la
liblife2_la_SOURCES = life2.cpp
liblife2_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
liblife2_la_LDFLAGS = -no-undefined
liblife2_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../components/input/src/.libs/libinput.la ../../../components/shape/src/.libs/libshape.la ../../../components/cell/src/.libs/libcell.la
