AM_CPPFLAGS = ${CATCH2_CFLAGS} -I../include

check_PROGRAMS = test_NameParser test_LibraryLoader

test_NameParser_SOURCES = test_NameParser.cpp
test_NameParser_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_LibraryLoader_SOURCES = test_LibraryLoader.cpp
test_LibraryLoader_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}
