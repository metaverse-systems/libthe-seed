ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libcycle.la
libcycle_la_SOURCES = cycle.cpp
libcycle_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libcycle_la_LDFLAGS = -no-undefined
libcycle_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../components/input/src/.libs/libinput.la \
						../../../components/position/src/.libs/libposition.la \
						../../../components/velocity/src/.libs/libvelocity.la \
						../../../components/cycle_shape/src/.libs/libcycle_shape.la \
						../../../components/draw/src/.libs/libdraw.la
