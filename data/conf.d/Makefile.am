libbdconfdir = $(sysconfdir)/libblockdev/@MAJOR_VER@/conf.d
dist_libbdconf_DATA = ${srcdir}/00-default.cfg

if WITH_LVM_DBUS
dist_libbdconf_DATA += ${srcdir}/10-lvm-dbus.cfg
endif

if WITH_SMARTMONTOOLS
dist_libbdconf_DATA += ${srcdir}/10-smartmontools.cfg
endif

MAINTAINERCLEANFILES = Makefile.in
