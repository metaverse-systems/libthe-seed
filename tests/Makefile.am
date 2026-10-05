AM_CPPFLAGS = ${LIBECS_CFLAGS} -I../include -I../external/Catch2/extras -I$(srcdir) \
              -DFIXTURES_DIR=\"$(abs_srcdir)/fixtures\" \
              -DLIBRARY_PATH_UNDER_TEST=\"$(abs_top_builddir)/src/.libs/libthe-seed.so\" \
              -DMODULE_DIR=\"$(abs_builddir)/.libs\"
AM_CXXFLAGS = -std=c++20

check_PROGRAMS = test_NameParser test_LibraryLoader test_DependencyLister \
                 test_PeSigner test_MachOParser test_MachOSigner test_MsiSigner test_ResourcePak \
                 test_ComponentLoader test_SystemLoader test_PakLoader \
                 test_LoaderConcurrency test_JSONLoader

check_LTLIBRARIES = libtestmodule.la libcatch.la

libcatch_la_SOURCES = ../external/Catch2/extras/catch_amalgamated.cpp

test_NameParser_SOURCES = test_NameParser.cpp
test_NameParser_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_LibraryLoader_SOURCES = test_LibraryLoader.cpp
test_LibraryLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_DependencyLister_SOURCES = test_DependencyLister.cpp
test_DependencyLister_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_PeSigner_SOURCES = test_PeSigner.cpp
test_PeSigner_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MachOSigner_SOURCES = test_MachOSigner.cpp
test_MachOSigner_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MachOParser_SOURCES = test_MachOParser.cpp
test_MachOParser_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MsiSigner_SOURCES = test_MsiSigner.cpp
test_MsiSigner_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_ResourcePak_SOURCES = test_ResourcePak.cpp
test_ResourcePak_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_ComponentLoader_SOURCES = test_ComponentLoader.cpp
test_ComponentLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_SystemLoader_SOURCES = test_SystemLoader.cpp
test_SystemLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_PakLoader_SOURCES = test_PakLoader.cpp
test_PakLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_LoaderConcurrency_SOURCES = test_LoaderConcurrency.cpp
test_LoaderConcurrency_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}
test_LoaderConcurrency_LDFLAGS = $(AM_LDFLAGS) -pthread

libtestmodule_la_SOURCES = TestModule.cpp
libtestmodule_la_CXXFLAGS = -std=c++20 ${LIBECS_CFLAGS}
libtestmodule_la_LDFLAGS = -no-undefined -rpath /nowhere -version-info 0:0:0
libtestmodule_la_LIBADD = ${LIBECS_LIBS}

test_JSONLoader_SOURCES = test_JSONLoader.cpp
test_JSONLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}
test_JSONLoader_LDFLAGS = $(AM_LDFLAGS) -pthread

EXTRA_DIST = TestPaths.hpp run-test.sh run-test-selftest.sh check-fixtures.sh \
             known-gaps.txt fixtures
