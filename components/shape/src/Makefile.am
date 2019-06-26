ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libshape.la
libshape_la_SOURCES = shape.cpp
libshape_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS} ${SDL2_IMAGE_CFLAGS}
libshape_la_LDFLAGS = -no-undefined
libshape_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ${SDL2_IMAGE_LIBS}
