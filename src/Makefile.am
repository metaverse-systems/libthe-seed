lib_LTLIBRARIES = libthe-seed.la
pkginclude_HEADERS = \
    ../include/libthe-seed/LoadError.hpp \
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
	ElfParser.hpp \
	PeParser.hpp \
	ByteSwap.hpp \
	internal/BoundedBytes.hpp \
	internal/DependencyGraph.hpp \
	internal/PeCertificate.hpp \
	NameParser.hpp \
	internal/FileIO.hpp \
	internal/PluginSearch.hpp \
	internal/PakFile.hpp \
	internal/PakIndex.hpp \
	internal/PakAccess.hpp \
	internal/PluginCache.hpp \
	internal/FileReplaceHooks.hpp \
	internal/MachODefs.hpp \
	internal/MachOLayout.hpp \
	internal/MachOSuperBlob.hpp \
	internal/MsiSignatureSize.hpp \
	internal/CfbReader.hpp \
	internal/CfbWriter.hpp \
	internal/MsiAuthenticode.hpp \
	internal/AuthenticodeDigestField.hpp

libthe_seed_la_CXXFLAGS = -std=c++20 -I$(top_srcdir)/include -I$(top_srcdir)/external ${LIBECS_CFLAGS} $(SANITIZER_CXXFLAGS)
libthe_seed_la_LIBADD = -lpthread ${LIBECS_LIBS}
libthe_seed_la_LDFLAGS = -no-undefined -version-info 0:0:0 $(SANITIZER_LDFLAGS)
libthe_seed_la_SOURCES = LoadError.cpp \
		LibraryLoader.cpp \
		SystemLoader.cpp \
		ComponentLoader.cpp \
		JSONLoader.cpp \
		ResourcePak.cpp \
		NameParser.cpp \
		DependencyLister.cpp \
		ElfParser.cpp \
		PakLoader.cpp \
		PeParser.cpp \
		PeSigner.cpp \
		MachOParser.cpp \
		MachOSigner.cpp \
		MsiSigner.cpp \
		internal/FileIO.cpp \
		internal/PluginSearch.cpp \
		internal/PakFile.cpp \
		internal/PakIndex.cpp \
		internal/PakAccess.cpp \
		internal/MachOLayout.cpp \
		internal/CfbReader.cpp \
		internal/CfbWriter.cpp \
		internal/MsiAuthenticode.cpp \
		internal/AuthenticodeDigestField.cpp
