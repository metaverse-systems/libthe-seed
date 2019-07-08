ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libzzt_engine.la
libzzt_engine_la_SOURCES = zzt_engine.cpp zzt_world.cpp zzt_board.cpp zzt_colors.cpp zzt_elements.cpp
libzzt_engine_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libzzt_engine_la_LDFLAGS = -no-undefined
libzzt_engine_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../Loaders/libloaders.la
