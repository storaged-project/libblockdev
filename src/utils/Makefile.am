lib_LTLIBRARIES = libbd_utils.la
libbd_utils_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_utils_la_LDFLAGS = -version-info 0:1:0
libbd_utils_la_LIBADD = $(GLIB_LIBS)
libbd_utils_la_SOURCES = utils.h exec.c exec.h sizes.c sizes.h

libincludedir = $(includedir)/blockdev
libinclude_HEADERS = utils.h exec.h sizes.h

MAINTAINERCLEANFILES = Makefile.in
