lib_LTLIBRARIES = libthe-seed.la
pkginclude_HEADERS = \
    ../include/libthe-seed/JSONLoader.hpp \
    ../include/libthe-seed/SystemLoader.hpp \
    ../include/libthe-seed/ComponentLoader.hpp \
    ../include/libthe-seed/LibraryLoader.hpp \
    ../include/libthe-seed/ResourcePak.hpp

libthe_seed_la_CXXFLAGS = -std=c++20 ${LIBECS_CFLAGS} -I../include ${LIEF_CFLAGS}
libthe_seed_la_LIBADD = -lpthread ${LIBECS_LIBS} ${LIEF_LIBS}
libthe_seed_la_LDFLAGS = -no-undefined -version-info 0:0:0
libthe_seed_la_SOURCES = LibraryLoader.cpp \
		SystemLoader.cpp \
		ComponentLoader.cpp \
		JSONLoader.cpp \
		ResourcePak.cpp \
		NameParser.cpp \
		DependencyLister.cpp
