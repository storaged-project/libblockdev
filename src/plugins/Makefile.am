lib_LTLIBRARIES = libbd_btrfs.la libbd_crypto.la libbd_dm.la libbd_loop.la libbd_lvm.la libbd_lvm-dbus.la libbd_mdraid.la libbd_mpath.la libbd_swap.la libbd_kbd.la

if ON_S390
lib_LTLIBRARIES += libbd_s390.la
endif

libbd_btrfs_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_btrfs_la_LIBADD = $(GLIB_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_btrfs_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_btrfs_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_btrfs_la_SOURCES = btrfs.c btrfs.h

libbd_crypto_la_CFLAGS = $(GLIB_CFLAGS) $(CRYPTSETUP_CFLAGS) $(NSS_CFLAGS) -Wall -Wextra -Werror
libbd_crypto_la_LIBADD = $(GLIB_LIBS) $(CRYTPSETUP_LIBS) $(NSS_LIBS) -lvolume_key ${builddir}/../utils/libbd_utils.la
libbd_crypto_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_crypto_la_CPPFLAGS = -I${srcdir}/../utils/ -I/usr/include/volume_key
libbd_crypto_la_SOURCES = crypto.c crypto.h

libbd_dm_la_CFLAGS = $(GLIB_CFLAGS) $(DEVMAPPER_CFLAGS) $(UDEV_CFLAGS) -Wall -Wextra -Werror
libbd_dm_la_LIBADD = $(GLIB_LIBS) $(DEVMAPPER_LIBS) $(UDEV_LIBS) -ldmraid ${builddir}/../utils/libbd_utils.la
libbd_dm_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
# Dear author of libdmdraid, VERSION really is not a good name for an enum member!
libbd_dm_la_CPPFLAGS = -I${srcdir}/../utils/ -UVERSION
libbd_dm_la_SOURCES = dm.c dm.h

libbd_loop_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_loop_la_LIBADD = $(GLIB_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_loop_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_loop_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_loop_la_SOURCES = loop.c loop.h

libbd_lvm_la_CFLAGS = $(GLIB_CFLAGS) $(DEVMAPPER_CFLAGS) -Wall -Wextra -Werror
libbd_lvm_la_LIBADD = $(GLIB_LIBS) $(DEVMAPPER_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_lvm_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 4:1:4
libbd_lvm_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_lvm_la_SOURCES = lvm.c lvm.h

libbd_lvm_dbus_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) -Wall -Wextra -Werror
libbd_lvm_dbus_la_LIBADD = $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_lvm_dbus_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 1:1:1
libbd_lvm_dbus_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_lvm_dbus_la_SOURCES = lvm-dbus.c lvm.h

libbd_mdraid_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_mdraid_la_LIBADD = $(GLIB_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_mdraid_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_mdraid_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_mdraid_la_SOURCES = mdraid.c mdraid.h

libbd_mpath_la_CFLAGS = $(GLIB_CFLAGS) $(DEVMAPPER_CFLAGS) -Wall -Wextra -Werror
libbd_mpath_la_LIBADD = $(GLIB_LIBS) $(DEVMAPPER_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_mpath_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_mpath_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_mpath_la_SOURCES = mpath.c mpath.h

libbd_swap_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_swap_la_LIBADD = $(GLIB_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_swap_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_swap_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_swap_la_SOURCES = swap.c swap.h

libbd_kbd_la_CFLAGS = $(GLIB_CFLAGS) $(KMOD_CFLAGS) -Wall -Wextra -Werror
libbd_kbd_la_LIBADD = $(GLIB_LIBS) $(KMOD_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_kbd_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_kbd_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_kbd_la_SOURCES = kbd.c kbd.h

if ON_S390
libbd_s390_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_s390_la_LIBADD = $(GLIB_LIBS) ${builddir}/../utils/libbd_utils.la
libbd_s390_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 0:1:0
libbd_s390_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_s390_la_SOURCES = s390.c s390.h
endif


libincludedir = $(includedir)/blockdev
libinclude_HEADERS = crypto.h \
	btrfs.h \
	dm.h \
	kbd.h \
	loop.h \
	lvm.h \
	mdraid.h \
	mpath.h \
	swap.h

if ON_S390
libinclude_HEADERS += s390.h
endif

MAINTAINERCLEANFILES = Makefile.in
