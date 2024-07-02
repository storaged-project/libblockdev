SUBDIRS = .

if WITH_FS
SUBDIRS += fs
endif

if WITH_NVME
SUBDIRS += nvme
endif

SUBDIRS += smart

lib_LTLIBRARIES =

if WITH_BTRFS
lib_LTLIBRARIES += libbd_btrfs.la
endif

if WITH_CRYPTO
lib_LTLIBRARIES += libbd_crypto.la
endif

if WITH_DM
lib_LTLIBRARIES += libbd_dm.la
endif

if WITH_LOOP
lib_LTLIBRARIES += libbd_loop.la
endif

if WITH_LVM
lib_LTLIBRARIES += libbd_lvm.la
endif

if WITH_LVM_DBUS
lib_LTLIBRARIES += libbd_lvm-dbus.la
endif

if WITH_MDRAID
lib_LTLIBRARIES += libbd_mdraid.la
endif

if WITH_MPATH
lib_LTLIBRARIES += libbd_mpath.la
endif

if WITH_NVDIMM
lib_LTLIBRARIES += libbd_nvdimm.la
endif

if WITH_SWAP
lib_LTLIBRARIES += libbd_swap.la
endif

if WITH_PART
lib_LTLIBRARIES += libbd_part.la
endif

if WITH_S390
lib_LTLIBRARIES += libbd_s390.la
endif


if WITH_BTRFS
libbd_btrfs_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(BYTESIZE_CFLAGS) -Wall -Wextra -Werror
libbd_btrfs_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(BYTESIZE_LIBS)
libbd_btrfs_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_btrfs_la_CPPFLAGS = -I${builddir}/../../include/
libbd_btrfs_la_SOURCES = btrfs.c btrfs.h check_deps.c check_deps.h
endif

if WITH_CRYPTO
if WITH_ESCROW
libbd_crypto_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(CRYPTSETUP_CFLAGS) $(BLKID_CFLAGS) $(NSS_CFLAGS) -Wall -Wextra -Werror
libbd_crypto_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(CRYPTSETUP_LIBS) $(NSS_LIBS) $(BLKID_LIBS) -lkeyutils -lvolume_key
else
libbd_crypto_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(CRYPTSETUP_CFLAGS) $(BLKID_CFLAGS) -Wall -Wextra -Werror
libbd_crypto_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(CRYPTSETUP_LIBS) $(BLKID_LIBS) -lkeyutils
endif
libbd_crypto_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_crypto_la_CPPFLAGS = -I${builddir}/../../include/
libbd_crypto_la_SOURCES = crypto.c crypto.h
endif

if WITH_DM
libbd_dm_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) -Wall -Wextra -Werror
libbd_dm_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS)
libbd_dm_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_dm_la_CPPFLAGS = -I${builddir}/../../include/
libbd_dm_la_SOURCES = dm.c dm.h check_deps.c check_deps.h dm_logging.c dm_logging.h
endif

if WITH_LOOP
libbd_loop_la_CFLAGS = $(GLIB_CFLAGS) -Wall -Wextra -Werror
libbd_loop_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS)
libbd_loop_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_loop_la_CPPFLAGS = -I${builddir}/../../include/
libbd_loop_la_SOURCES = loop.c loop.h
endif

if WITH_LVM
libbd_lvm_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) $(YAML_CFLAGS) -Wall -Wextra -Werror
libbd_lvm_la_LIBADD = ${builddir}/../utils/libbd_utils.la -lm $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS) $(YAML_LIBS)
libbd_lvm_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_lvm_la_CPPFLAGS = -I${builddir}/../../include/
libbd_lvm_la_SOURCES = lvm.c lvm.h check_deps.c check_deps.h dm_logging.c dm_logging.h vdo_stats.c vdo_stats.h
endif

if WITH_LVM_DBUS
libbd_lvm_dbus_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) $(YAML_CFLAGS) -Wall -Wextra -Werror
libbd_lvm_dbus_la_LIBADD = ${builddir}/../utils/libbd_utils.la -lm $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS) $(YAML_LIBS)
libbd_lvm_dbus_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_lvm_dbus_la_CPPFLAGS = -I${builddir}/../../include/
libbd_lvm_dbus_la_SOURCES = lvm-dbus.c lvm.h check_deps.c check_deps.h dm_logging.c dm_logging.h vdo_stats.c vdo_stats.h
endif

if WITH_MDRAID
libbd_mdraid_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(BYTESIZE_CFLAGS) -Wall -Wextra -Werror
libbd_mdraid_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(BYTESIZE_LIBS)
libbd_mdraid_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_mdraid_la_CPPFLAGS = -I${builddir}/../../include/
libbd_mdraid_la_SOURCES = mdraid.c mdraid.h check_deps.c check_deps.h
endif

if WITH_MPATH
libbd_mpath_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(DEVMAPPER_CFLAGS) -Wall -Wextra -Werror
libbd_mpath_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(DEVMAPPER_LIBS)
libbd_mpath_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_mpath_la_CPPFLAGS = -I${builddir}/../../include/
libbd_mpath_la_SOURCES = mpath.c mpath.h check_deps.c check_deps.h
endif

if WITH_NVDIMM
libbd_nvdimm_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(UUID_CFLAGS) $(NDCTL_CFLAGS) -Wall -Wextra -Werror
libbd_nvdimm_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(UUID_LIBS) $(NDCTL_LIBS)
libbd_nvdimm_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_nvdimm_la_CPPFLAGS = -I${builddir}/../../include/
libbd_nvdimm_la_SOURCES = nvdimm.c nvdimm.h check_deps.c check_deps.h
endif

if WITH_SWAP
libbd_swap_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(BLKID_CFLAGS) $(UUID_CFLAGS) -Wall -Wextra -Werror
libbd_swap_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(BLKID_LIBS) $(UUID_LIBS)
libbd_swap_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_swap_la_CPPFLAGS = -I${builddir}/../../include/
libbd_swap_la_SOURCES = swap.c swap.h check_deps.c check_deps.h
endif

if WITH_S390
libbd_s390_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) -Wall -Wextra -Werror
libbd_s390_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS)
libbd_s390_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_s390_la_CPPFLAGS = -I${srcdir}/../utils/
libbd_s390_la_CPPFLAGS = -I${builddir}/../../include/
libbd_s390_la_SOURCES = s390.c s390.h check_deps.c check_deps.h
endif

if WITH_PART
libbd_part_la_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(FDISK_CFLAGS) -Wall -Wextra -Werror
libbd_part_la_LIBADD = ${builddir}/../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(FDISK_LIBS)
libbd_part_la_LDFLAGS = -L${srcdir}/../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_part_la_CPPFLAGS = -I${builddir}/../../include/
libbd_part_la_SOURCES = part.c part.h check_deps.c check_deps.h
endif

libincludedir = $(includedir)/blockdev
libinclude_HEADERS =

if WITH_BTRFS
libinclude_HEADERS += btrfs.h
endif

if WITH_CRYPTO
libinclude_HEADERS += crypto.h
endif

if WITH_DM
libinclude_HEADERS += dm.h
endif

if WITH_LOOP
libinclude_HEADERS += loop.h
endif

if WITH_LVM
libinclude_HEADERS += lvm.h
else
if WITH_LVM_DBUS
libinclude_HEADERS += lvm.h
endif
endif

if WITH_MDRAID
libinclude_HEADERS += mdraid.h
endif

if WITH_MPATH
libinclude_HEADERS += mpath.h
endif

if WITH_NVDIMM
libinclude_HEADERS += nvdimm.h
endif

if WITH_SWAP
libinclude_HEADERS += swap.h
endif

if WITH_PART
libinclude_HEADERS += part.h
endif

if WITH_FS
libinclude_HEADERS += fs.h
endif

if WITH_S390
libinclude_HEADERS += s390.h
endif

MAINTAINERCLEANFILES = Makefile.in
