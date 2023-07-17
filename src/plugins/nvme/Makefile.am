AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libbd_nvme.la

libbd_nvme_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(NVME_CFLAGS) -Wall -Wextra -Werror
libbd_nvme_la_LIBADD = ${builddir}/../../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(NVME_LIBS)
libbd_nvme_la_LDFLAGS = -L${srcdir}/../../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_nvme_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../ -I. -DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"

libbd_nvme_la_SOURCES = \
	nvme.h \
	nvme.c \
	nvme-private.h \
	nvme-info.c \
	nvme-error.c \
	nvme-op.c \
	nvme-fabrics.c \
	../check_deps.c \
	../check_deps.h

libbd_nvmeincludedir = $(includedir)/blockdev
libbd_nvmeinclude_HEADERS = nvme.h
