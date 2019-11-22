ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libinput.la
libinput_la_SOURCES = input.cpp
libinput_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libinput_la_LDFLAGS = -no-undefined
libinput_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS}
