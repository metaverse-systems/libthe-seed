ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libsdl_input.la
libsdl_input_la_SOURCES = sdl_input.cpp
libsdl_input_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS} ${SDL2_CFLAGS}
libsdl_input_la_LDFLAGS = -no-undefined
libsdl_input_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ${SDL2_LIBS} ../../../components/input/src/.libs/libinput.la ../../../loaders/.libs/libloaders.la
