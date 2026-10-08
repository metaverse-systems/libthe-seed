AM_CPPFLAGS = -I$(top_srcdir)/include ${LIBECS_CFLAGS} -I$(top_srcdir)/src -I$(top_srcdir)/external/Catch2/extras -I$(srcdir) \
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
seed_test_programs = test_NameParser test_LibraryLoader test_DependencyLister test_DependencyAttribution test_DependencyReporting \
                 test_PeSigner test_MachOParser test_MachOSigner test_MsiSigner test_ResourcePak \
                 test_ComponentLoader test_SystemLoader test_PakLoader \
                 test_LoaderConcurrency test_JSONLoader test_BoundedBytes test_MalformedPe test_PeDigestReference test_PeRoundTrip test_MalformedElf test_MalformedMachO test_MachONoRoom test_MachOLayout test_MachOReference \
                 test_MalformedMsi test_MsiDigestReference test_MsiSignatureSizes test_MsiLayouts test_MsiCheckSignature test_TruncatedSamples test_FuzzCorpus \
                 test_FileReplace test_FileReplaceKill test_SignersUseReplace

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

test_DependencyAttribution_SOURCES = test_DependencyAttribution.cpp
test_DependencyAttribution_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_DependencyReporting_SOURCES = test_DependencyReporting.cpp
test_DependencyReporting_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

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

test_PeDigestReference_SOURCES = test_PeDigestReference.cpp
test_PeDigestReference_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_PeRoundTrip_SOURCES = test_PeRoundTrip.cpp
test_PeRoundTrip_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MalformedElf_SOURCES = test_MalformedElf.cpp
test_MalformedElf_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MalformedMachO_SOURCES = test_MalformedMachO.cpp
test_MalformedMachO_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MachONoRoom_SOURCES = test_MachONoRoom.cpp
test_MachONoRoom_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MachOLayout_SOURCES = test_MachOLayout.cpp
test_MachOLayout_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MachOReference_SOURCES = test_MachOReference.cpp
test_MachOReference_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MalformedMsi_SOURCES = test_MalformedMsi.cpp
test_MalformedMsi_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MsiDigestReference_SOURCES = test_MsiDigestReference.cpp
test_MsiDigestReference_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MsiSignatureSizes_SOURCES = test_MsiSignatureSizes.cpp
test_MsiSignatureSizes_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MsiLayouts_SOURCES = test_MsiLayouts.cpp
test_MsiLayouts_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_MsiCheckSignature_SOURCES = test_MsiCheckSignature.cpp
test_MsiCheckSignature_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_TruncatedSamples_SOURCES = test_TruncatedSamples.cpp fuzz/FuzzTargets.cpp
test_TruncatedSamples_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_FuzzCorpus_SOURCES = test_FuzzCorpus.cpp fuzz/FuzzTargets.cpp
test_FuzzCorpus_CPPFLAGS = $(AM_CPPFLAGS) -DFUZZ_CORPUS_DIR=\"$(abs_srcdir)/fuzz/corpus\"
test_FuzzCorpus_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_FileReplace_SOURCES = test_FileReplace.cpp
test_FileReplace_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}
test_FileReplace_LDFLAGS = $(AM_LDFLAGS) -pthread

test_FileReplaceKill_SOURCES = test_FileReplaceKill.cpp
test_FileReplaceKill_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

test_SignersUseReplace_SOURCES = test_SignersUseReplace.cpp
test_SignersUseReplace_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}

libtestmodule_la_SOURCES = TestModule.cpp
libtestmodule_la_CXXFLAGS = -std=c++20 ${LIBECS_CFLAGS} $(SANITIZER_CXXFLAGS)
libtestmodule_la_LDFLAGS = -no-undefined -rpath /nowhere -version-info 0:0:0 $(SANITIZER_LDFLAGS)
libtestmodule_la_LIBADD = ${LIBECS_LIBS}

test_JSONLoader_SOURCES = test_JSONLoader.cpp
test_JSONLoader_LDADD = libcatch.la ../src/libthe-seed.la ${LIBECS_LIBS}
test_JSONLoader_LDFLAGS = $(AM_LDFLAGS) -pthread

EXTRA_DIST = TestPaths.hpp CfbReference.hpp MsiTestSupport.hpp MachOReference.hpp MachOSigningSupport.hpp HeapCounter.hpp MalformedInput.hpp ReplaceTestSupport.hpp \
             reference/reference-dump.cpp reference/compare-reference.sh \
             reference/make-generated-references.sh run-test.sh run-test-selftest.sh check-fixtures.sh \
             fuzz/FuzzTargets.hpp fuzz/FuzzTargets.cpp fuzz/fuzz_entry.cpp \
             fuzz/build-fuzzers.sh fuzz/run-fuzzers.sh \
             fuzz/corpus \
             known-gaps.txt fixtures
# fixtures is distributed whole: the installer samples (*.msi), msi-reference.txt,
# msi-open.exe, make_cfb.py and src/ are in it.
