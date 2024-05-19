if WITH_TOOLS
bin_PROGRAMS = lvm-cache-stats vfat-resize

lvm_cache_stats_CFLAGS   = $(GLIB_CFLAGS) $(BYTESIZE_CFLAGS) -Wall -Wextra -Werror
lvm_cache_stats_CPPFLAGS = -I${builddir}/../include/
lvm_cache_stats_LDFLAGS  = -Wl,--no-undefined
lvm_cache_stats_LDADD    = ${builddir}/../src/lib/libblockdev.la $(GLIB_LIBS) $(BYTESIZE_LIBS)

vfat_resize_CFLAGS   = $(GLIB_CFLAGS) $(BYTESIZE_CFLAGS) $(PARTED_CFLAGS) $(PARTED_FS_CFLAGS) -Wall -Wextra -Werror
vfat_resize_CPPFLAGS = -I${builddir}/../include/
vfat_resize_LDFLAGS  = -Wl,--no-undefined
vfat_resize_LDADD    = ${builddir}/../src/lib/libblockdev.la $(GLIB_LIBS) $(BYTESIZE_LIBS) $(PARTED_LIBS) $(PARTED_FS_LIBS) $(UUID_LIBS)
endif
