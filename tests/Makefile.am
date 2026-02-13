AM_CPPFLAGS = ${CATCH2_CFLAGS} -I../include -I../external/Catch2/extras

check_PROGRAMS = test_NameParser test_LibraryLoader test_DependencyLister

test_NameParser_SOURCES = test_NameParser.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_NameParser_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_LibraryLoader_SOURCES = test_LibraryLoader.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_LibraryLoader_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_DependencyLister_SOURCES = test_DependencyLister.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_DependencyLister_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}
