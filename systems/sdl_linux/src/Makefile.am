ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libsdl_linux.la
libsdl_linux_la_SOURCES = sdl_linux.cpp
libsdl_linux_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS} ${SDL2_CFLAGS} ${SDL2_IMAGE_CFLAGS}
libsdl_linux_la_LDFLAGS = -no-undefined
libsdl_linux_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ${SDL2_LIBS} ${SDL2_IMAGE_LIBS}
