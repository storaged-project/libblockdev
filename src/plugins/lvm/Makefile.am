AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES =
libincludedir = $(includedir)/blockdev

if WITH_LVM
libinclude_HEADERS = lvm.h
else
if WITH_LVM_DBUS
libinclude_HEADERS = lvm.h
endif
endif


if WITH_LVM

lib_LTLIBRARIES += libbd_lvm.la

libbd_lvm_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) $(YAML_CFLAGS) -Wall -Wextra -Werror
libbd_lvm_la_LIBADD = ${builddir}/../../utils/libbd_utils.la -lm $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS) $(YAML_LIBS)
libbd_lvm_la_LDFLAGS = -L${srcdir}/../../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_lvm_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../ -I. -DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"

libbd_lvm_la_SOURCES = \
	lvm.c \
	lvm.h \
	vdo_stats.c \
	vdo_stats.h \
	../check_deps.c \
	../check_deps.h \
	../dm_logging.c \
	../dm_logging.h

endif

if WITH_LVM_DBUS

lib_LTLIBRARIES += libbd_lvm-dbus.la

libbd_lvm_dbus_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) $(YAML_CFLAGS) -Wall -Wextra -Werror
libbd_lvm_dbus_la_LIBADD = ${builddir}/../../utils/libbd_utils.la -lm $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS) $(YAML_LIBS)
libbd_lvm_dbus_la_LDFLAGS = -L${srcdir}/../../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_lvm_dbus_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../ -I. -DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"

libbd_lvm_dbus_la_SOURCES = \
	lvm-dbus.c \
	lvm.h \
	vdo_stats.c \
	vdo_stats.h \
	../check_deps.c \
	../check_deps.h \
	../dm_logging.c \
	../dm_logging.h

endif
