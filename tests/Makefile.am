AM_CPPFLAGS = ${CATCH2_CFLAGS} ${LIBECS_CFLAGS} -I../include -I../external/Catch2/extras \
              -DFIXTURES_DIR=\"$(abs_srcdir)/fixtures\"
AM_CXXFLAGS = -std=c++20

check_PROGRAMS = test_NameParser test_LibraryLoader test_DependencyLister \
                 test_PeSigner test_MachOSigner test_MsiSigner test_ResourcePak \
                 test_ComponentLoader test_SystemLoader test_PakLoader \
                 test_LoaderConcurrency

test_NameParser_SOURCES = test_NameParser.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_NameParser_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_LibraryLoader_SOURCES = test_LibraryLoader.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_LibraryLoader_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_DependencyLister_SOURCES = test_DependencyLister.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_DependencyLister_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_PeSigner_SOURCES = test_PeSigner.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_PeSigner_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_MachOSigner_SOURCES = test_MachOSigner.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_MachOSigner_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_MsiSigner_SOURCES = test_MsiSigner.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_MsiSigner_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_ResourcePak_SOURCES = test_ResourcePak.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_ResourcePak_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_ComponentLoader_SOURCES = test_ComponentLoader.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_ComponentLoader_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_SystemLoader_SOURCES = test_SystemLoader.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_SystemLoader_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_PakLoader_SOURCES = test_PakLoader.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_PakLoader_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}

test_LoaderConcurrency_SOURCES = test_LoaderConcurrency.cpp ../external/Catch2/extras/catch_amalgamated.cpp
test_LoaderConcurrency_LDADD = ../src/libthe-seed.la ${LIBECS_LIBS}
test_LoaderConcurrency_LDFLAGS = -pthread
