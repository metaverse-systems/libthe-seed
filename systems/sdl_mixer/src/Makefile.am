ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libsdl_mixer.la
libsdl_mixer_la_SOURCES = sdl_mixer.cpp
libsdl_mixer_la_CXXFLAGS = -std=c++14 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS} ${SDL2_CFLAGS} ${SDL2_MIXER_CFLAGS}
libsdl_mixer_la_LDFLAGS = -no-undefined
libsdl_mixer_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ${SDL2_LIBS} ${SDL2_MIXER_LIBS} ../../../loaders/libloaders.la ../../../components/song/src/.libs/libsong.la ../../../components/input/src/.libs/libinput.la
