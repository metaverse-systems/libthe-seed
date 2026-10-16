lib_LTLIBRARIES = libthe-seed.la
# Every source is compiled once into the convenience library; the installed
# library is built from it and the tests of internals link it statically.
noinst_LTLIBRARIES = libthe-seed-core.la
pkginclude_HEADERS = \
    ../include/libthe-seed/LoadError.hpp \
    ../include/libthe-seed/Export.hpp \
    ../include/libthe-seed/JSONLoader.hpp \
    ../include/libthe-seed/SystemLoader.hpp \
    ../include/libthe-seed/ComponentLoader.hpp \
    ../include/libthe-seed/LibraryLoader.hpp \
	../include/libthe-seed/ResourcePak.hpp \
	../include/libthe-seed/DependencyLister.hpp \
    ../include/libthe-seed/PakLoader.hpp \
	../include/libthe-seed/PeSigner.hpp \
	../include/libthe-seed/MachOParser.hpp \
	../include/libthe-seed/MachOSigner.hpp \
	../include/libthe-seed/MsiSigner.hpp

noinst_HEADERS = \
	internal/ElfParser.hpp \
	internal/PeParser.hpp \
	internal/ByteSwap.hpp \
	internal/BoundedBytes.hpp \
	internal/DependencyGraph.hpp \
	internal/PeCertificate.hpp \
	internal/NameParser.hpp \
	internal/FileIO.hpp \
	internal/PluginSearch.hpp \
	internal/PagedFile.hpp \
	internal/RandomAccessFile.hpp \
	internal/PakIndex.hpp \
	internal/PakAccess.hpp \
	internal/PluginCache.hpp \
	internal/PluginLoaderCore.hpp \
	internal/FileReplaceHooks.hpp \
	internal/MachODefs.hpp \
	internal/MachOLayout.hpp \
	internal/MachOSuperBlob.hpp \
	internal/MsiSignatureSize.hpp \
	internal/CfbReader.hpp \
	internal/CfbWriter.hpp \
	internal/MsiAuthenticode.hpp \
	internal/AuthenticodeDigestField.hpp

# Hidden by default: only classes marked THE_SEED_API are exported.
libthe_seed_core_la_CPPFLAGS = -DTHE_SEED_BUILDING
libthe_seed_core_la_CXXFLAGS = -fvisibility=hidden -std=c++20 -I$(top_srcdir)/include -I$(top_srcdir)/external ${LIBECS_CFLAGS} $(SANITIZER_CXXFLAGS)
libthe_seed_core_la_LIBADD = -lpthread ${LIBECS_LIBS}

# The shared library has no sources of its own. The empty C++ file only makes
# libtool link it with the C++ driver.
libthe_seed_la_SOURCES =
nodist_EXTRA_libthe_seed_la_SOURCES = dummy.cpp
libthe_seed_la_LIBADD = libthe-seed-core.la -lpthread ${LIBECS_LIBS}
libthe_seed_la_LDFLAGS = -no-undefined -version-info 0:0:0 $(SANITIZER_LDFLAGS)
libthe_seed_core_la_SOURCES = LoadError.cpp \
		LibraryLoader.cpp \
		SystemLoader.cpp \
		ComponentLoader.cpp \
		JSONLoader.cpp \
		ResourcePak.cpp \
		internal/NameParser.cpp \
		DependencyLister.cpp \
		internal/ElfParser.cpp \
		PakLoader.cpp \
		internal/PeParser.cpp \
		PeSigner.cpp \
		MachOParser.cpp \
		MachOSigner.cpp \
		MsiSigner.cpp \
		internal/FileIO.cpp \
		internal/PluginSearch.cpp \
		internal/PagedFile.cpp \
		internal/RandomAccessFile.cpp \
		internal/PakIndex.cpp \
		internal/PakAccess.cpp \
		internal/MachOLayout.cpp \
		internal/CfbReader.cpp \
		internal/CfbWriter.cpp \
		internal/MsiAuthenticode.cpp \
		internal/AuthenticodeDigestField.cpp
