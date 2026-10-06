AM_CPPFLAGS = ${LIBECS_CFLAGS} -I$(top_srcdir)/include -I$(top_srcdir)/src -I$(top_srcdir)/external/Catch2/extras -I$(srcdir) \
              -DFIXTURES_DIR=\"$(abs_srcdir)/fixtures\" \
              -DLIBRARY_PATH_UNDER_TEST=\"$(abs_top_builddir)/src/.libs/libthe-seed.so\" \
              -DMODULE_DIR=\"$(abs_builddir)/.libs\"
AM_CXXFLAGS = -std=c++20 $(SANITIZER_CXXFLAGS)
AM_LDFLAGS = $(SANITIZER_LDFLAGS)

LOG_COMPILER = $(SHELL) $(srcdir)/run-test.sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    SEED_SANITIZER='@SANITIZER@'; export SEED_SANITIZER; \
    SEED_KNOWN_GAPS_FILE='$(abs_srcdir)/known-gaps.txt'; export SEED_KNOWN_GAPS_FILE; \
    SEED_TEST_PROGRAMS='$(seed_test_programs)'; export SEED_TEST_PROGRAMS; \
    SEED_FIXTURES_DIR='$(abs_srcdir)/fixtures'; export SEED_FIXTURES_DIR; \
    SEED_TESTS_SRCDIR='$(abs_srcdir)'; export SEED_TESTS_SRCDIR; \
    ASAN_OPTIONS='halt_on_error=1:detect_leaks=1:abort_on_error=0:print_summary=1'; export ASAN_OPTIONS; \
    UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1:print_summary=1'; export UBSAN_OPTIONS;

# Bare program names: run-test.sh compares them with known-gaps.txt, and
# check_PROGRAMS gets $(EXEEXT) appended when cross-compiling for Windows.
seed_test_programs = test_NameParser test_LibraryLoader test_DependencyLister \
                 test_PeSigner test_MachOParser test_MachOSigner test_MsiSigner test_ResourcePak \
                 test_ComponentLoader test_SystemLoader test_PakLoader \
                 test_LoaderConcurrency test_JSONLoader test_BoundedBytes test_MalformedPe test_MalformedElf

check_PROGRAMS = $(seed_test_programs)

if CROSS_COMPILING
TESTS = check-fixtures.sh
check-local:
	@echo "Test programs were built but not run: cross-compiling."
else
TESTS = $(seed_test_programs) run-test-selftest.sh check-fixtures.sh
endif

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

test_BoundedBytes_SOURCES = test_BoundedBytes.cpp
test_BoundedBytes_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MalformedPe_SOURCES = test_MalformedPe.cpp
test_MalformedPe_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MalformedElf_SOURCES = test_MalformedElf.cpp
test_MalformedElf_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

libtestmodule_la_SOURCES = TestModule.cpp
libtestmodule_la_CXXFLAGS = -std=c++20 ${LIBECS_CFLAGS} $(SANITIZER_CXXFLAGS)
libtestmodule_la_LDFLAGS = -no-undefined -rpath /nowhere -version-info 0:0:0 $(SANITIZER_LDFLAGS)
libtestmodule_la_LIBADD = ${LIBECS_LIBS}

test_JSONLoader_SOURCES = test_JSONLoader.cpp
test_JSONLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}
test_JSONLoader_LDFLAGS = $(AM_LDFLAGS) -pthread

EXTRA_DIST = TestPaths.hpp HeapCounter.hpp MalformedInput.hpp \
             reference/reference-dump.cpp reference/compare-reference.sh \
             reference/make-generated-references.sh run-test.sh run-test-selftest.sh check-fixtures.sh \
             known-gaps.txt fixtures
