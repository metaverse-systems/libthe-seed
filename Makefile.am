pkgconfig_DATA = the-seed.pc
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src tests

# Reference documentation, written to doxygen/html in the build directory.
# Not part of all, check or install.
.PHONY: doxygen
doxygen: Doxyfile
	@if test -z "$(DOXYGEN)"; then \
	  echo "make doxygen needs doxygen (and graphviz for the diagrams); install them and run configure again" >&2; \
	  exit 1; \
	fi
	$(DOXYGEN) Doxyfile

clean-local:
	rm -rf doxygen
