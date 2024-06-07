AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libbd_fs.la

libbd_fs_la_CFLAGS   = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(BLKID_CFLAGS) $(MOUNT_CFLAGS) $(UUID_CFLAGS) $(EXT2FS_CFLAGS) -Wall -Wextra -Werror -Wno-unused-parameter -Wno-shift-count-overflow
libbd_fs_la_LIBADD   = ${builddir}/../../utils/libbd_utils.la $(GLIB_LIBS) $(GIO_LIBS) $(BLKID_LIBS) $(MOUNT_LIBS) $(UUID_LIBS) $(EXT2FS_LIBS)
libbd_fs_la_LDFLAGS	 = -L${srcdir}/../../utils/ -version-info 3:0:0 -Wl,--no-undefined -export-symbols-regex '^bd_.*'
libbd_fs_la_CPPFLAGS = -I${builddir}/../../../include/ -I${srcdir}/../
libbd_fs_la_SOURCES  = ../check_deps.c ../check_deps.h \
						../fs.c    ../fs.h    \
						common.c   common.h   \
						ext.c      ext.h      \
						generic.c  generic.h  \
						mount.c    mount.h    \
						ntfs.c     ntfs.h     \
						vfat.c     vfat.h     \
						xfs.c      xfs.h      \
						f2fs.c     f2fs.h     \
						nilfs.c    nilfs.h    \
						exfat.c    exfat.h    \
						btrfs.c    btrfs.h    \
						udf.c      udf.h

libincludefsdir = $(includedir)/blockdev/fs/
libincludefs_HEADERS = ext.h     \
					generic.h  \
					mount.h    \
					ntfs.h     \
					vfat.h     \
					xfs.h      \
					f2fs.h	   \
					nilfs.h    \
					exfat.h    \
					btrfs.h    \
					udf.h
