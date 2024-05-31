-include $(INTROSPECTION_MAKEFILE)

SUBDIRS = plugin_apis

lib_LTLIBRARIES = libblockdev.la
libblockdev_la_CFLAGS = $(GLIB_CFLAGS) $(GOBJECT_CFLAGS)
libblockdev_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GOBJECT_LIBS) -ldl

if WITH_BTRFS
libblockdev_la_CFLAGS += $(GOBJECT_CFLAGS)
libblockdev_la_LIBADD += $(GOBJECT_LIBS)
endif
if WITH_LVM
libblockdev_la_CFLAGS += $(GOBJECT_CFLAGS)
libblockdev_la_LIBADD += $(GOBJECT_LIBS)
endif

libblockdev_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libblockdev_la_CPPFLAGS = -I${builddir}/../../include/
libblockdev_la_SOURCES = blockdev.c blockdev.h plugins.c plugins.h

if HAVE_INTROSPECTION
GIHEADERS = ${builddir}/plugin_apis/mdraid.h \
	${builddir}/plugin_apis/swap.h \
	${builddir}/plugin_apis/btrfs.h \
	${builddir}/plugin_apis/lvm.h \
	${builddir}/plugin_apis/crypto.h \
	${builddir}/plugin_apis/dm.h \
	${builddir}/plugin_apis/loop.h \
	${builddir}/plugin_apis/mpath.h \
	${builddir}/plugin_apis/part.h \
	${builddir}/plugin_apis/fs.h \
	${builddir}/plugin_apis/nvdimm.h \
	${builddir}/plugin_apis/nvme.h \
	${builddir}/plugin_apis/smart.h

GIHEADERS += $(wildcard ${srcdir}/../utils/*.[ch])
GIHEADERS += blockdev.c blockdev.h plugins.c plugins.h

if WITH_S390
GIHEADERS += ${builddir}/plugin_apis/s390.h
endif

BlockDev-3.0.gir: libblockdev.la

BlockDev_3_0_gir_FILES = $(GIHEADERS)
BlockDev_3_0_gir_LIBS = libblockdev.la
BlockDev_3_0_gir_INCLUDES = GObject-2.0 Gio-2.0
BlockDev_3_0_gir_CFLAGS = -I${builddir}/../../include/
BlockDev_3_0_gir_LDFLAGS = -L${builddir}/../utils/ -lbd_utils
BlockDev_3_0_gir_SCANNERFLAGS = --warn-error --warn-all --identifier-prefix=BD --symbol-prefix=bd
BlockDev_3_0_gir_EXPORT_PACKAGES = blockdev

INTROSPECTION_GIRS = BlockDev-3.0.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

CLEANFILES = BlockDev-3.0.gir $(typelib_DATA)
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ${builddir}/blockdev.pc

libincludedir = $(includedir)/blockdev
libinclude_HEADERS = blockdev.h plugins.h

MAINTAINERCLEANFILES = Makefile.in blockdev.pc blockdev.c
