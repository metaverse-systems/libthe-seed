ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = librogue_engine.la
librogue_engine_la_SOURCES = rogue_engine.cpp
librogue_engine_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
librogue_engine_la_LDFLAGS = -no-undefined
librogue_engine_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../components/input/src/.libs/libinput.la
