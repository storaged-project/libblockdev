lib_LTLIBRARIES = libbd_utils.la
libbd_utils_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_utils_la_LDFLAGS = -version-info 4:1:2
libbd_utils_la_LIBADD = $(GLIB_LIBS)
libbd_utils_la_SOURCES = utils.h exec.c exec.h sizes.h extra_arg.c extra_arg.h

libincludedir = $(includedir)/blockdev
libinclude_HEADERS = utils.h exec.h sizes.h extra_arg.h

MAINTAINERCLEANFILES = Makefile.in
