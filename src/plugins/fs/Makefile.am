AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libbd_fs.la

libbd_fs_la_CFLAGS   = $(GLIB_CFLAGS) $(BLKID_CFLAGS) $(PARTED_CFLAGS) $(PARTED_FS_CFLAGS) $(MOUNT_CFLAGS) -Wall -Wextra -Werror
libbd_fs_la_LIBADD   = ${builddir}/../../utils/libbd_utils.la ${builddir}/../libbd_part_err.la $(GLIB_LIBS) $(BLKID_LIBS) $(PARTED_LIBS) $(PARTED_FS_LIBS) $(MOUNT_LIBS)
libbd_fs_la_LDFLAGS	 = -L${srcdir}/../../utils/ -version-info 2:0:0 -Wl,--no-undefined
libbd_fs_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../
libbd_fs_la_SOURCES  = ../check_deps.c ../check_deps.h \
						../fs.c   ../fs.h   \
						common.c  common.h  \
						ext.c     ext.h     \
						generic.c generic.h \
						mount.c   mount.h   \
						ntfs.c    ntfs.h    \
						vfat.c    vfat.h    \
						xfs.c     xfs.h

libincludefsdir = $(includedir)/blockdev/fs/
libincludefs_HEADERS = ext.h     \
					generic.h \
					mount.h   \
					ntfs.h    \
					vfat.h    \
					xfs.h
