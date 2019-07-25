lib_LTLIBRARIES = libloaders.la
EXTRA_DIST=*.hpp

libloaders_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS}
libloaders_la_LIBADD = -lpthread ${LIBECS_LIBS} ${JSONCPP_LIBS}
libloaders_la_LDFLAGS = -no-undefined -version-info 0:0:0
libloaders_la_SOURCES = LibraryLoader.cpp \
		SystemLoader.cpp \
		ComponentLoader.cpp \
		ResourcePak.cpp
