-include $(INTROSPECTION_MAKEFILE)

SUBDIRS = plugin_apis

lib_LTLIBRARIES = libblockdev.la
libblockdev_la_CFLAGS = $(GLIB_CFLAGS)
libblockdev_la_LIBADD = $(GLIB_LIBS) ${builddir}/../utils/libbd_utils.la
libblockdev_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libblockdev_la_CPPFLAGS = -I${srcdir}/../utils/
libblockdev_la_SOURCES = blockdev.c blockdev.h plugins.c plugins.h

if HAVE_INTROSPECTION
GIHEADERS = ${srcdir}/plugin_apis/kbd.h \
	${srcdir}/plugin_apis/mdraid.h \
	${srcdir}/plugin_apis/swap.h \
	${srcdir}/plugin_apis/btrfs.h \
	${srcdir}/plugin_apis/lvm.h \
	${srcdir}/plugin_apis/crypto.h \
	${srcdir}/plugin_apis/dm.h \
	${srcdir}/plugin_apis/loop.h \
	${srcdir}/plugin_apis/mpath.h

GIHEADERS += $(wildcard ${srcdir}/../utils/*.[ch])
GIHEADERS += blockdev.c blockdev.h plugins.c plugins.h

if ON_S390
GIHEADERS += ${srcdir}/src/lib/plugin_apis/s390.h
endif

BlockDev-1.0.gir: libblockdev.la

BlockDev_1_0_gir_FILES = $(GIHEADERS)
BlockDev_1_0_gir_LIBS = libblockdev.la
BlockDev_1_0_gir_INCLUDES = GObject-2.0
BlockDev_1_0_gir_PACKAGES = devmapper libudev libcryptsetup
BlockDev_1_0_gir_CFLAGS = -I${srcdir}/../utils/
BlockDev_1_0_gir_LDFLAGS = -lm -ldmraid -L${builddir}/../utils/ -lbd_utils
BlockDev_1_0_gir_SCANNERFLAGS = --warn-error --warn-all --identifier-prefix=BD --symbol-prefix=bd
BlockDev_1_0_gir_EXPORT_PACKAGES = blockdev

INTROSPECTION_GIRS = BlockDev-1.0.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ${builddir}/blockdev.pc

libincludedir = $(includedir)/blockdev
libinclude_HEADERS = blockdev.h plugins.h

CLEANFILES = BlockDev-1.0.gir $(typelib_DATA)
MAINTAINERCLEANFILES = Makefile.in blockdev.pc blockdev.c
endif

