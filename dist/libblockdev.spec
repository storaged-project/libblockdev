Name:        libblockdev
Version:     0.12
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
BuildRequires: volume_key-devel >= 0.3.9-7
BuildRequires: nss-devel
BuildRequires: python-devel
BuildRequires: python3-devel
BuildRequires: gtk-doc
BuildRequires: glib2-doc


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
Requires: %{name}-utils%{?_isa} >= 0.11
Requires: btrfs-progs

%description btrfs
The libblockdev library plugin (and in the same time a standalone library)
proving the BTRFS-related functionality.

%package btrfs-devel
Summary:     Development files for the libblockdev-btrfs plugin/library
Requires: %{name}-btrfs%{?_isa} = %{version}-%{release}
Requires: glib2-devel
Requires: %{name}-utils-devel%{?_isa}

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
Requires: %{name}-utils%{?_isa} >= 0.11
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
Requires: %{name}-utils-devel%{?_isa}

%description dm-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-dm plugin/library.


%package loop
Summary:     The loop plugin for the libblockdev library
Requires: %{name}-utils%{?_isa} >= 0.11
Requires: util-linux

%description loop
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to loop devices.

%package loop-devel
Summary:     Development files for the libblockdev-loop plugin/library
Requires: %{name}-loop%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel%{?_isa}
Requires: glib2-devel

%description loop-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-loop plugin/library.


%package lvm
Summary:     The LVM plugin for the libblockdev library
Requires: %{name}-utils%{?_isa} >= 0.11
Requires: lvm2

%description lvm
The libblockdev library plugin (and in the same time a standalone library)
proving the LVM-related functionality.

%package lvm-devel
Summary:     Development files for the libblockdev-lvm plugin/library
Requires: %{name}-lvm%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel%{?_isa}
Requires: glib2-devel

%description lvm-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-lvm plugin/library.


%package mdraid
Summary:     The MD RAID plugin for the libblockdev library
Requires: %{name}-utils%{?_isa} >= 0.11
Requires: mdadm

%description mdraid
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to MD RAID.

%package mdraid-devel
Summary:     Development files for the libblockdev-mdraid plugin/library
Requires: %{name}-mdraid%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel%{?_isa}
Requires: glib2-devel

%description mdraid-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-mdraid plugin/library.


%package mpath
Summary:     The multipath plugin for the libblockdev library
Requires: %{name}-utils%{?_isa} >= 0.11
Requires: device-mapper-multipath

%description mpath
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to multipath devices.

%package mpath-devel
Summary:     Development files for the libblockdev-mpath plugin/library
Requires: %{name}-mpath%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel%{?_isa}
Requires: glib2-devel

%description mpath-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-mpath plugin/library.


%package swap
Summary:     The swap plugin for the libblockdev library
Requires: %{name}-utils%{?_isa} >= 0.11
Requires: util-linux

%description swap
The libblockdev library plugin (and in the same time a standalone library)
proving the functionality related to swap devices.

%package swap-devel
Summary:     Development files for the libblockdev-swap plugin/library
Requires: %{name}-swap%{?_isa} = %{version}-%{release}
Requires: %{name}-utils-devel%{?_isa}
Requires: glib2-devel

%description swap-devel
This package contains header files and pkg-config files needed for development
with the libblockdev-swap plugin/library.


%package plugins-all
Summary:     Meta-package that pulls all the libblockdev plugins as dependencies
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: %{name}-btrfs%{?_isa} = %{version}-%{release}
Requires: %{name}-crypto%{?_isa} = %{version}-%{release}
Requires: %{name}-dm%{?_isa} = %{version}-%{release}
Requires: %{name}-loop%{?_isa} = %{version}-%{release}
Requires: %{name}-lvm%{?_isa} = %{version}-%{release}
Requires: %{name}-mdraid%{?_isa} = %{version}-%{release}
Requires: %{name}-mpath%{?_isa} = %{version}-%{release}
Requires: %{name}-swap%{?_isa} = %{version}-%{release}

%description plugins-all
A meta-package that pulls all the libblockdev plugins as dependencies.


%prep
%setup -q -n %{name}-%{version}

%build
CFLAGS="%{optflags}" make %{?_smp_mflags}

%install
CFLAGS="%{optflags}" make PREFIX=%{buildroot} SITEDIRS=%{buildroot}%{python2_sitearch},%{buildroot}%{python3_sitearch} %{?_smp_mflags} install


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
%{!?_licensedir:%global license %%doc}
%license LICENSE
%{_libdir}/libblockdev.so.*
%{_libdir}/girepository*/BlockDev*.typelib
%{python2_sitearch}/gi/overrides/*
%{python3_sitearch}/gi/overrides/BlockDev*
%{python3_sitearch}/gi/overrides/__pycache__/BlockDev*

%files devel
%doc features.rst specs.rst
%{_libdir}/libblockdev.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/blockdev.h
%{_includedir}/blockdev/plugins.h
%{_libdir}/pkgconfig/blockdev.pc
%{_datadir}/gtk-doc/html/libblockdev
%{_datadir}/gir*/BlockDev*.gir


%files utils
%{_libdir}/libbd_utils.so.*

%files utils-devel
%{_libdir}/libbd_utils.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/utils.h
%{_includedir}/blockdev/sizes.h
%{_includedir}/blockdev/exec.h


%files btrfs
%{_libdir}/libbd_btrfs.so.*

%files btrfs-devel
%{_libdir}/libbd_btrfs.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/btrfs.h


%files crypto
%{_libdir}/libbd_crypto.so.*

%files crypto-devel
%{_libdir}/libbd_crypto.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/crypto.h


%files dm
%{_libdir}/libbd_dm.so.*

%files dm-devel
%{_libdir}/libbd_dm.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/dm.h


%files loop
%{_libdir}/libbd_loop.so.*

%files loop-devel
%{_libdir}/libbd_loop.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/loop.h


%files lvm
%{_libdir}/libbd_lvm.so.*

%files lvm-devel
%{_libdir}/libbd_lvm.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/lvm.h


%files mdraid
%{_libdir}/libbd_mdraid.so.*

%files mdraid-devel
%{_libdir}/libbd_mdraid.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/mdraid.h


%files mpath
%{_libdir}/libbd_mpath.so.*

%files mpath-devel
%{_libdir}/libbd_mpath.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/mpath.h


%files swap
%{_libdir}/libbd_swap.so.*

%files swap-devel
%{_libdir}/libbd_swap.so
%dir %{_includedir}/blockdev
%{_includedir}/blockdev/swap.h

%files plugins-all

%changelog
* Fri Apr 24 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.12-1
- Require minimum version of libblockdev-utils in some plugins (vpodzime)
- Report both stdout and stderr if exit code != 0 (vpodzime)

* Fri Apr 17 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.11-1
- Fix issues with using overriden functions over ErrorProxy (vpodzime)
- Update the roadmap.rst and features.rst with new stuff (vpodzime)
- Fix two minor issues with docs generation (vpodzime)

* Thu Apr 16 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.10-1
- Fix return type of the unload_plugins() function (vpodzime)
- Close the DL handle when check() or init() fail (vpodzime)
- Add one more check to the reload test (vpodzime)
- Drop reference to check() and init() functions (vpodzime)
- Add more cats to tests (vpodzime)
- Make regexp for getting btrfs version more generic (vpodzime)
- Merge pull request #8 from vpodzime/master-check_functions (vpodzime)
- Fix parameters passed to unoverridden swapon function (vpodzime)
- Implement and test swap plugin's check function (vpodzime)
- Implement and test MD RAID plugin's check function (vpodzime)
- Implement and test mpath plugin's check function (vpodzime)
- Try harder to get util's version (vpodzime)
- Implement and test loop plugin's check function (vpodzime)
- Implement and test DM plugin's check function (vpodzime)
- Implement and test BTRFS plugin's check function (vpodzime)
- Implement and test LVM plugin's check function (vpodzime)
- Init logging before loading plugins (vpodzime)
- Add function for utility availability checking (vpodzime)
- Fix default value for the fake_utils' path argument (vpodzime)
- Add ErrorProxy instance for the utils functions (vpodzime)
- Add function for version comparison (vpodzime)
- Merge pull request #9 from clumens/master (vpodzime)
- Disable pylint checking on the new exception proxy. (clumens)
- Fix XRules application and add a test for it (vpodzime)
- Raise NotImplementedError when an unavailable function is called (vpodzime)
- Merge pull request #4 from vpodzime/master-error_proxy (vpodzime)
- Merge branch 'master' into master-error_proxy (vpodzime)
- Merge pull request #5 from vpodzime/master-not_implemented_error (vpodzime)
- Add a simple test for unloaded/unavailable functions (vpodzime)
- Unload the plugins properly when reinit() is called (vpodzime)
- Raise error/exception when an unimplemented function is called (#1201475) (vpodzime)
- Do an ugly but necessary hack to make local GI overrides work (vpodzime)
- Add the __dir__ method to ErrorProxy (vpodzime)
- Add a rationale for the ErrorProxy to the overrides' docstring (vpodzime)
- Add some basic info about GI overrides to the documentation (vpodzime)
- Use pylint to check for errors in python overrides (vpodzime)
- Add the first small test for the ErrorProxy (vpodzime)
- Put the GI overrides in a special dir so that they are preferred (vpodzime)
- Add a cache for attributes already resolved by ErrorProxy (vpodzime)
- Implement the ErrorProxy python class and use it (vpodzime)

* Tue Apr 07 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.9-1
- Merge pull request #7 from vpodzime/master-fw_raid_fixes (vpodzime)
- Try a bit harder when trying to determine MD RAID name (#1207317) (vpodzime)
- Don't be naïve about mdadm --detail telling us what we want (#1207317) (vpodzime)
- Ignore libblockdev tarballs (vpodzime)
- Implement a test of btrfs_list_subvolumes on data from bug report (vpodzime)
- Implement a context manager for running tests with fake utils (vpodzime)
- Do not try to cannonicalize MD UUIDs if we didn't get them (#1207317) (vpodzime)
- Fix the table in roadmap.rst (vpodzime)
- Enrich the roadmap.rst file and add info about new plans (vpodzime)
- Sync spec file with downstream (vpodzime)

* Fri Mar 27 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.8-1
- Merge pull request #6 from vpodzime/master-sort_btrfs_subvolumes (vpodzime)
- Don't be naïve about mdadm providing us data we would like (#1206394) (vpodzime)
- Sort BTRFS subvolumes in a way that child never appears before parent (#1201120) (vpodzime)
- Let libcryptsetup handle LUKSname->/dev/mapper/LUKSname for us (vpodzime)
- Fix the crypto_luks_resize and create a test for it (vpodzime)
- Add targets to create the SRPM and RPM files easily (vpodzime)
- Don't round up to multiple of PE size bigger than max value of the rtype (vpodzime)
- Mark majority of MD RAID tests as slow (vpodzime)
- Merge pull request #1 from dashea/file-paths (vpodzime)
- Don't report error for no loop device associated with given file (vpodzime)
- Skip the detail_data.clean check when running tests in Jenkins (vpodzime)
- Make package file paths more specific (dshea)
- Implement and use MD RAID-specific wait for tests (vpodzime)
- Try to give MD RAID time to sync things before querying them (vpodzime)
- Fix the default value of the BDMDDetailData.clean field (vpodzime)
- Do cleanup after every single MD RAID tests (vpodzime)
- Do cleanup after every single LVM test (vpodzime)
- Do cleanup after every single BTRFS test (vpodzime)
- Make sure the LUKS device is closed and removed after tests (vpodzime)
- Make sure DM maps from tests are removed after tests (vpodzime)
- Make sure that loop devices are deactivated after tests (vpodzime)
- Make the tearDown method of the mpath test case better visible (vpodzime)
- Make sure that the swap is deactivated after tests (vpodzime)
- Fix docstrings in tests' utils helper functions (vpodzime)
- Improve the logging tests in utils_test.py (vpodzime)
- Update the features.rst file (vpodzime)
- Update the roadmap (vpodzime)
- Don't check if we get a mountpoint for BTRFS operations (vpodzime)

* Sun Mar 22 2015 Peter Robinson <pbrobinson@fedoraproject.org> 0.7-2
- Ship license as per packaging guidelines
- plugins-all should depend on base library too
- Add dev docs

* Fri Feb 27 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.7-1
- Be ready for mdadm --examine to not provide some of the values we want (vpodzime)
- Add exit code information to exec logging (vpodzime)
- Improve and add tests (vpodzime)
- Mark the test_force_plugin and test_reload as slow (vpodzime)
- Make sure we get some devices when creating btrfs volume (vpodzime)
- Add override for the lvremove function (vpodzime)
- Do not create LUKS format with no passphrase and no key file (vpodzime)
- Make sure we use the /dev/mapper/... path for luks_status (vpodzime)

* Thu Feb 19 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.6-1
- Don't report error when non-existing swap's status is queried (vpodzime)
- Make libblockdev-plugins-all pull the same version of plugins (vpodzime)
- Don't report error when asked for a backing file of an uknown loop (vpodzime)
- Fix accidental change in the spec's changelog (vpodzime)

* Mon Feb 16 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.5-1
- Add tests for what we can easily test from the mpath plugin (vpodzime)
- Add link to sources to the documentation (vpodzime)
- Add missing symbols into the libblockdev-sections.txt file (vpodzime)
- Do not build docs for testing (vpodzime)
- Add the bd_try_init function (vpodzime)
- Log stdout and stderr output when running processes (vpodzime)
- Allow a subset of plugins to be load instead of all (vpodzime)
- Make sure devmapper doesn't spam stdout with tons of messages (vpodzime)
- Let debug messages go to stderr when running ipython (vpodzime)
- Give plugins a way to initialize themselves (vpodzime)
- Give plugins a way how to check if they could run properly (vpodzime)
- Allow a subset of plugins to be load instead of all [TEST NEEDED] (vpodzime)
- Make sure we use the whole /dev/mapper path for cryptsetup (vpodzime)
- Fix vg_pv_count parsing when getting info about PV (vpodzime)
- Set default values to data structures if real values are not available (vpodzime)
- Fix the parameter name specifying pool metadata size (vpodzime)
- Activate LUKS as ReadWrite in luks_open (vpodzime)
- Make sure we pass key_size to cryptsetup in bytes (vpodzime)
- Add the min_entropy parameter to luks_format Python overrides (vpodzime)
- Pass size in KiB instead of B to lvcreate (vpodzime)
- Add underscore into dataalignment and metadatasize parameter names (vpodzime)
- Don't report error if non-mpath device is tested for being mpath member (vpodzime)
- Fix name of the invoked utility in mpath_set_friendly_names (vpodzime)

* Sat Jan 31 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.4-1
- Improve the test for lvm_set_global_config (vpodzime)
- Fix some minor issues in the spec file (vpodzime)
- Fix issues with the LVM global config str (vpodzime)
- Add couple more Python overrides (vpodzime)
- Fix the name of the lvm_thlvpoolname() function in the header file (vpodzime)
- Use assertEqual instead of assertTrue(a == b) (vpodzime)
- Add the min_entropy parameter to luks_format (vpodzime)
- Move internal dmraid-related macros into the source file (vpodzime)
- Add an override for the md_add function (vpodzime)
- Fix parameters in luks_open python overrides (vpodzime)
- Prevent init() from being done multiple times and provide a test function (vpodzime)
- Add the roadmap.rst document (vpodzime)
- Remove an extra parenthesis in one of the docstrings (vpodzime)
- Move the mddetail function next to the mdexamine function (vpodzime)
- Add some more constants required by blivet (vpodzime)

* Wed Jan 21 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.3-1
- Require volume_key-devel in a version that fixes build issues (vpodzime)
- Fix Python 2 devel package name in BuildRequires (vpodzime)
- Generate docs for the library and all plugins (vpodzime)
- Make doc comments better for documentation generation (vpodzime)
- Fix parameter names in function prototypes (vpodzime)
- Add the metadatasize parameter to pvcreate (vpodzime)
- Add the dataalignment parameter to lvm_pvcreate (vpodzime)
- Export non-internal constants via introspection (vpodzime)
- Expand size constants in the GI-scanned files (vpodzime)
- Fix usage printing in the boilerplate_generator (vpodzime)
- Add the build directory to .gitignore (vpodzime)
- Add the md_run function (vpodzime)
- Fix some issues in Python overrides (vpodzime)
- Add the escrow_device function to the crypto plugin (vpodzime)
- Fix version of GI files in the Makefile (vpodzime)
- Make the order of release target's dependencies more explicit (vpodzime)

* Mon Jan 12 2015 Vratislav Podzimek <vpodzime@redhat.com> - 0.2-1
- Fix dependencies of the release target (vpodzime)
- Python overrides for the GI-generated bindings (vpodzime)
- Pass version info to the code and use it to load plugins (vpodzime)

* Wed Dec 10 2014 Vratislav Podzimek <vpodzime@redhat.com> - 0.1-1
- Initial release
