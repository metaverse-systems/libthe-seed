ACLOCAL_AMFLAGS=-I m4
bin_PROGRAMS = Life
Life_SOURCES = Life.cpp
Life_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
Life_LDADD = -lpthread ${LIBECS_LIBS} ${JSONCPP_LIBS} ../../../loaders/libloaders.la
