ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libtexture.la
libtexture_la_SOURCES = texture.cpp
libtexture_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libtexture_la_LDFLAGS = -no-undefined
libtexture_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
