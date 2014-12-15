Name:        libblockdev
Version:     0.1
Release:     1%{?dist}
Summary:     A library for low-level manipulation with block devices
License:     LGPLv2+
URL:         https://github.com/vpodzime/libblockdev
Source0:     https://github.com/vpodzime/libblockdev/archive/%{name}-%{version}.tar.gz

BuildRequires: scons
BuildRequires: glib2-devel
BuildRequires: gobject-introspection-devel
BuildRequires: cryptsetup-devel
BuildRequires: device-mapper-devel
BuildRequires: systemd-devel
BuildRequires: dmraid-devel


%description
The libblockdev is a C library with GObject introspection support that can be
used for doing low-level operations with block devices like setting up LVM,
BTRFS, LUKS or MD RAID. The library uses plugins (LVM, BTRFS,...) and serves as
a thin wrapper around its plugins' functionality. All the plugins, however, can
be used as standalone libraries. One of the core principles of libblockdev is
that it is stateless from the storage configuration's perspective (e.g. it has
no information about VGs when creating an LV).

%package devel
Summary:     Development files for libblockdev
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: glib2-devel

%description devel
This package contains header files and pkg-config files needed for development
with the libblockdev library.


%package utils
Summary:     A library with utility functions for the libblockdev library

%description utils
The libblockdev-utils is a library providing utility functions used by the
libblockdev library and its plugins.

%package utils-devel
Summary:     Development files for libblockdev-utils
Requires: %{name}-utils%{?_isa} = %{version}-%{release}
Requires: glib2-devel

%description utils-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-utils library.


%package btrfs
Summary:     The BTRFS plugin for the libblockdev library
Requires: btrfs-progs

%description btrfs
The libblockdev library plugin (and in the same time a standalone library)
proving the BTRFS-related functionality.

%package btrfs-devel
Summary:     Development files for the libblockdev-btrfs plugin/library
Requires: %{name}-btrfs%{?_isa} = %{version}-%{release}
Requires: glib2-devel
Requires: %{name}-utils-devel

%description btrfs-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-btrfs plugin/library.


%package crypto
Summary:     The crypto plugin for the libblockdev library

%description crypto
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to encrypted devices (LUKS).

%package crypto-devel
Summary:     Development files for the libblockdev-crypto plugin/library
Requires: %{name}-crypto%{?_isa} = %{version}-%{release}
Requires: glib2-devel

%description crypto-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-crypto plugin/library.


%package dm
Summary:     The Device Mapper plugin for the libblockdev library
Requires: device-mapper
Requires: dmraid

%description dm
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to Device Mapper.

%package dm-devel
Summary:     Development files for the libblockdev-dm plugin/library
Requires: %{name}-dm%{?_isa} = %{version}-%{release}
Requires: glib2-devel
Requires: device-mapper-devel
Requires: systemd-devel
Requires: dmraid-devel
Requires: %{name}-utils-devel

%description dm-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-dm plugin/library.


%package loop
Summary:     The loop plugin for the libblockdev library
Requires: util-linux

%description loop
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to loop devices.

%package loop-devel
Summary:     Development files for the libblockdev-loop plugin/library
Requires: %{name}-loop%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel
Requires: glib2-devel

%description loop-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-loop plugin/library.


%package lvm
Summary:     The LVM plugin for the libblockdev library
Requires: lvm2

%description lvm
The libblockdev library plugin (and in the same time a standalone library)
proving the LVM-related functionality.

%package lvm-devel
Summary:     Development files for the libblockdev-lvm plugin/library
Requires: %{name}-lvm%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel
Requires: glib2-devel

%description lvm-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-lvm plugin/library.


%package mdraid
Summary:     The MD RAID plugin for the libblockdev library
Requires: mdadm

%description mdraid
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to MD RAID.

%package mdraid-devel
Summary:     Development files for the libblockdev-mdraid plugin/library
Requires: %{name}-mdraid%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel
Requires: glib2-devel

%description mdraid-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-mdraid plugin/library.


%package mpath
Summary:     The multipath plugin for the libblockdev library
Requires: device-mapper-multipath

%description mpath
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to multipath devices.

%package mpath-devel
Summary:     Development files for the libblockdev-mpath plugin/library
Requires: %{name}-mpath%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel
Requires: glib2-devel

%description mpath-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-mpath plugin/library.


%package swap
Summary:     The swap plugin for the libblockdev library
Requires: util-linux

%description swap
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to swap devices.

%package swap-devel
Summary:     Development files for the libblockdev-swap plugin/library
Requires: %{name}-swap%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel
Requires: glib2-devel

%description swap-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-swap plugin/library.


%package plugins-all
Summary:     Metapackage that pulls all the libblockdev plugins as dependencies
Requires: %{name}-btrfs
Requires: %{name}-crypto
Requires: %{name}-dm
Requires: %{name}-loop
Requires: %{name}-lvm
Requires: %{name}-mdraid
Requires: %{name}-mpath
Requires: %{name}-swap

%description plugins-all
A metapackage that pulls all the libblockdev plugins as dependencies.


%prep
%setup -q -n %{name}-%{version}

%build
CFLAGS="%{optflags}" make %{?_smp_mflags}

%install
CFLAGS="%{optflags}" make PREFIX=%{buildroot} %{?_smp_mflags} install

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig
%post utils -p /sbin/ldconfig
%postun utils -p /sbin/ldconfig
%post btrfs -p /sbin/ldconfig
%postun btrfs -p /sbin/ldconfig
%post crypto -p /sbin/ldconfig
%postun crypto -p /sbin/ldconfig
%post dm -p /sbin/ldconfig
%postun dm -p /sbin/ldconfig
%post loop -p /sbin/ldconfig
%postun loop -p /sbin/ldconfig
%post lvm -p /sbin/ldconfig
%postun lvm -p /sbin/ldconfig
%post mdraid -p /sbin/ldconfig
%postun mdraid -p /sbin/ldconfig
%post mpath -p /sbin/ldconfig
%postun mpath -p /sbin/ldconfig
%post swap -p /sbin/ldconfig
%postun swap -p /sbin/ldconfig


%files
%{_libdir}/libblockdev.so.*
%{_libdir}/girepository*/BlockDev*.typelib
%{_datadir}/gir*/BlockDev*.gir

%files devel
%{_libdir}/libblockdev.so
%{_includedir}/blockdev/blockdev.h
%{_includedir}/blockdev/plugins.h
%{_libdir}/pkgconfig/blockdev.pc


%files utils
%{_libdir}/libbd_utils.so.*

%files utils-devel
%{_libdir}/libbd_utils.so
%{_includedir}/blockdev/utils.h
%{_includedir}/blockdev/sizes.h
%{_includedir}/blockdev/exec.h


%files btrfs
%{_libdir}/libbd_btrfs.so.*

%files btrfs-devel
%{_libdir}/libbd_btrfs.so
%{_includedir}/blockdev/btrfs.h


%files crypto
%{_libdir}/libbd_crypto.so.*

%files crypto-devel
%{_libdir}/libbd_crypto.so
%{_includedir}/blockdev/crypto.h


%files dm
%{_libdir}/libbd_dm.so.*

%files dm-devel
%{_libdir}/libbd_dm.so
%{_includedir}/blockdev/dm.h


%files loop
%{_libdir}/libbd_loop.so.*

%files loop-devel
%{_libdir}/libbd_loop.so
%{_includedir}/blockdev/loop.h


%files lvm
%{_libdir}/libbd_lvm.so.*

%files lvm-devel
%{_libdir}/libbd_lvm.so
%{_includedir}/blockdev/lvm.h


%files mdraid
%{_libdir}/libbd_mdraid.so.*

%files mdraid-devel
%{_libdir}/libbd_mdraid.so
%{_includedir}/blockdev/mdraid.h


%files mpath
%{_libdir}/libbd_mpath.so.*

%files mpath-devel
%{_libdir}/libbd_mpath.so
%{_includedir}/blockdev/mpath.h


%files swap
%{_libdir}/libbd_swap.so.*

%files swap-devel
%{_libdir}/libbd_swap.so
%{_includedir}/blockdev/swap.h

%files plugins-all

%changelog
* Wed Dec 10 2014 Vratislav Podzimek <vpodzime@redhat.com> - 0.1-1
- Initial release
