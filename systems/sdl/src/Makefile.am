ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libsdl.la
libsdl_la_SOURCES = sdl.cpp
libsdl_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS} ${SDL2_CFLAGS} ${SDL2_IMAGE_CFLAGS}
libsdl_la_LDFLAGS = -no-undefined
libsdl_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ${SDL2_LIBS} ${SDL2_IMAGE_LIBS} ../../../components/position/src/.libs/libposition.la ../../../components/shape/src/.libs/libshape.la ../../../components/texture/src/.libs/libtexture.la 
