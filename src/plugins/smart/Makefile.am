AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES =
libincludedir = $(includedir)/blockdev

if WITH_SMART
libinclude_HEADERS = smart.h
else
if WITH_SMARTMONTOOLS
libinclude_HEADERS = smart.h
endif
endif


if WITH_SMART

lib_LTLIBRARIES += libbd_smart.la

libbd_smart_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(SMART_CFLAGS) -Wall -Wextra -Werror
libbd_smart_la_LIBADD = ${builddir}/../../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(SMART_LIBS)
libbd_smart_la_LDFLAGS = -L${srcdir}/../../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_smart_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../ -I. -DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"

libbd_smart_la_SOURCES = \
	smart.h \
	smart-private.h \
	smart-common.c \
	libatasmart.c \
	../check_deps.c \
	../check_deps.h

endif


if WITH_SMARTMONTOOLS

lib_LTLIBRARIES += libbd_smartmontools.la

libbd_smartmontools_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(JSON_GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_smartmontools_la_LIBADD = ${builddir}/../../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(JSON_GLIB_LIBS)
libbd_smartmontools_la_LDFLAGS = -L${srcdir}/../../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_smartmontools_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../ -I. -DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"

libbd_smartmontools_la_SOURCES = \
	smart.h \
	smart-private.h \
	smart-common.c \
	smartmontools.c \
	../check_deps.c \
	../check_deps.h

endif
