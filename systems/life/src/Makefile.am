ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = liblife.la
liblife_la_SOURCES = life.cpp
liblife_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
liblife_la_LDFLAGS = -no-undefined
liblife_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../components/input/src/.libs/libinput.la ../../../components/shape/src/.libs/libshape.la ../../../components/cell/src/.libs/libcell.la
