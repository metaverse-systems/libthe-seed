ACLOCAL_AMFLAGS=-I m4
lib_LTLIBRARIES = libsdl_video.la
libsdl_video_la_SOURCES = sdl_video.cpp
libsdl_video_la_CXXFLAGS = -std=c++17 ${LIBECS_CFLAGS} ${JSONCPP_CFLAGS} ${SDL2_CFLAGS}
libsdl_video_la_LDFLAGS = -no-undefined
libsdl_video_la_LIBADD = ${LIBECS_LIBS} ${JSONCPP_LIBS} ${SDL2_LIBS} ../../../components/position/src/.libs/libposition.la ../../../components/draw/src/.libs/libdraw.la
