#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([smartplayer], [0.5], [yangle0125@qq.com])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([src/debug.h])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([avformat], [av_register_all])
AC_CHECK_LIB([avcodec], [avcodec_open2])
AC_CHECK_LIB([avutil], [av_freep])
AC_CHECK_LIB([avfilter], [avfilter_register_all])
AC_CHECK_LIB([swscale], [sws_getContext])
AC_CHECK_LIB([SDL2], [SDL_Init])

# Checks for header files.
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_CHECK_FUNCS([getopt])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
