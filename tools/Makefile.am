if WITH_TOOLS
bin_PROGRAMS = lvm-cache-stats

lvm_cache_stats_CFLAGS   = $(GLIB_CFLAGS) $(BYTESIZE_CFLAGS) -Wall -Wextra -Werror
lvm_cache_stats_CPPFLAGS = -I${builddir}/../include/
lvm_cache_stats_LDFLAGS  = -Wl,--no-undefined
lvm_cache_stats_LDADD    = ${builddir}/../src/lib/libblockdev.la $(GLIB_LIBS) $(BYTESIZE_LIBS)
endif
