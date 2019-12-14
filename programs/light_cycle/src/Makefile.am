ACLOCAL_AMFLAGS=-I m4
bin_PROGRAMS = light_cycle
light_cycle_SOURCES = light_cycle.cpp
light_cycle_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
light_cycle_LDFLAGS = -no-undefined
light_cycle_LDADD = -lpthread ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../loaders/libloaders.la

