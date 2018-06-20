Libblockdev 2.18
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- features

  - New plugin: vdo

      - support for creating and managing VDO volumes

  - Support for building dm plugin without libdmraid support -- configure option ``--without-dmraid``.

**Full list of changes**

Kai Lüke (2):

- Correct arguments for ext4 repair with progress
- Introduce reporting function per thread

Tomas Bzatek (3):

- vdo: Resolve real device file path
- vdo: Implement bd_vdo_grow_physical()
- vdo: Add tests for bd_vdo_grow_physical()

Vojtech Trefny (14):

- Update specs.rst and features.rst
- Fix release number in NEWS.rst
- Add 'bd_dm_is_tech_avail' to header file
- Always check for error when (un)mounting
- Add the VDO plugin
- Add basic VDO plugin functionality
- Add decimal units definition to utils/sizes.h
- Add tests for VDO plugin
- Only require plugins we really need in LVM dbus tests
- Allow compiling libblockdev without libdmraid
- Adjust to new NVDIMM namespace modes
- Do not try to build VDO plugin on Fedora
- Remove roadmap.rst
- Add VDO to features.rst

Vratislav Podzimek (2):

- Use xfs_repair instead of xfs_db in bd_fs_xfs_check()
- Clarify that checking an RW-mounted XFS file system is impossible

segfault (1):

- Fix off-by-one error when counting TCRYPT keyfiles


Libblockdev 2.17
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- features

  - New plugin: nvdimm

    - support for NVDIMM namespaces management
    - requires *libndctl* >= 58.4

  - LUKS2 support

    - support for creating LUKS2 format including authenticated disk encryption
    - multiple new functions for working with LUKS devices (suspend/resume, header backup, metadata size...)

  - Extended support for opening TrueCrypt/VeraCrypt volumes

  - Support for building crypto plugin without escrow device support (removes
    build dependency on *libvolume_key* and *libnss*) -- configure option ``--without-escrow``.

  - Support for building libblockdev without Python 2 support -- configure option
    ``--without-python2``.

**Full list of changes**

Bjorn Pagen (3):

- Fix build against musl libc
- Fix build with clang
- Enforce ZERO_INIT gcc backwards compatibility

Florian Klink (1):

- s390: don't hardcode paths, search PATH

Jan Pokorny (1):

- New function for luks metadata size

Vojtech Trefny (24):

- Sync the spec file with downstream
- Fix python2-gobject-base dependency on Fedora 26 and older
- Add the NVDIMM plugin
- Add tests for the NVDIMM plugin
- Add --without-xyz to DISTCHECK_CONFIGURE_FLAGS for disabled plugins
- Add function for getting NVDIMM namespace name from devname or path
- Fix memory leaks discovered by clang
- Get sector size for non-block NVDIMM namespaces too
- lvm-dbus: Check returned job object for error
- Add functions to suspend and resume a LUKS device
- Add function for killing keyslot on a LUKS device
- Add functions to backup and restore LUKS header
- Require at least libndctl 58.4
- Allow compiling libblockdev crypto plugin without escrow support
- Allow building libblockdev without Python 2 support
- Skip bcache tests on Rawhide
- Add support for creating LUKS 2 format
- Use libblockdev function to create LUKS 2 in tests
- Add a basic test for creating LUKS 2 format
- Add function to get information about a LUKS device
- Add function to get information about LUKS 2 integrity devices
- Add functions to resize LUKS 2
- Add a generic logging function for libblockdev
- Redirect cryptsetup log to libblockdev log

Vratislav Podzimek (1):

- Use '=' instead of '==' to compare using 'test'

segfault (10):

- Support unlocking VeraCrypt volumes
- Support TCRYPT keyfiles
- Support TCRYPT hidden containers
- Support TCRYPT system volumes
- Support VeraCrypt PIM
- Add function bd_crypto_device_seems_encrypted
- Make keyfiles parameter to bd_crypto_tc_open_full zero terminated
- Don't use VeraCrypt PIM if compiled against libcryptsetup < 2.0
- Make a link point to the relevant section
- Add new functions to docs/libblockdev-sections.txt

Libblockdev 2.16
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- features

  - LUKS 2 support for luks_open/close and luks_add/remove/change_key

  - Progress report support for ext filesystem checks


**Full list of changes**

Jan Tulak (4):

- Add a function to test if prog. reporting was initialized
- Add progress reporting infrastructure for Ext fsck
- Add e2fsck progress
- Add tests for progress report

Vojtech Trefny (5):

- Fix link to online documentation
- Update 'Testing libblockdev' section in documentation
- Check if 'journalctl' is available before trying to use it in tests
- Fix few more links for project and documentation website
- Add support for LUKS 2 opening and key management

Vratislav Podzimek (2):

- Fix how the new kernel module functions are added to docs
- Sync the spec file with downstream


Libblockdev 2.15
----------------

New minor release of the libblockdev library with multiple fixes and quite big
refactorization changes (in the file system plugin). See below for details.


**Notable changes**

- fixes

  - Fix bd_s390_dasd_format() and bd_s390_dasd_is_ldl().

  - Fix how GPT patition flags are set.

  - Check the *btrfs* module availability as part of checking the *btrfs*
    plugin's dependencies.

  - Fix memory leaks in bd_fs_vfat_get_info()

  - Fix the file system plugin's dependency checking mechanisms.


- features

  - Mark some of the tests as unstable so that their failures are reported, but
    ignored in the overall test suite status.

  - The file system plugin is now split into multiple source files making it
    easier to add support for more file systems and technologies.


**Full list of changes**

Vendula Poncova (2):

- bd_s390_dasd_is_ldl should be true only for LDL DADSs
- Fix bd_s390_dasd_format

Vojtech Trefny (5):

- Use only sgdisk to set flags on GPT
- Add test for setting partition flags on GPT
- Free locale struct in kbd plugin
- Move kernel modules (un)loading and checking into utils
- Check for btrfs module availability in btrfs module

Vratislav Podzimek (11):

- Do not lie about tag creation
- Mark unstable tests as such
- Split the FS plugin source into multiple files
- Split the bd_fs_is_tech_avail() implementation
- Revert the behaviour of bd_fs_check_deps()
- Fix memory leaks in bd_fs_vfat_get_info()
- Mark bcache tests as unstable
- Add a HACKING.rst file
- Move the fs.h file to its original place
- Do not use the btrfs plugin in library tests
- Do not use the 'btrfs' plugin in overrides tests


Libblockdev 2.14
----------------

New minor release of the libblockdev library with important fixes and a few new
features, in particular support for the NTFS file system. See below for details.


**Notable changes**

- fixes

  - Fix BSSize memory leaks

  - Fixes for issues discovered by coverity

  - Support for the 'Legacy boot' GPT flag

- features

  - Added function to get DM device subsystem

  - Support for the NTFS file system

  - pkg-config definitions improvements


**Full list of changes**

Jan Pokorny (1):

- Added function to get DM device subsystem

Kai Lüke (2):

- Add function wrappers for NTFS tools
- Add some test cases for NTFS

Vojtech Trefny (29):

- Skip btrfs subvolume tests with btrfs-progs 4.13.2
- Fix BSSize memory leaks in btrfs and mdraid plugins
- Use system values in KbdTestBcacheStatusTest
- Use libbytesize to parse bcache block size
- blockdev.c.in: Fix unused variables
- fs.c: Fix resource leaks in 'bd_fs_get_fstype'
- fs.c: Check sscanf return value in 'bd_fs_vfat_get_info'
- fs.c: Fix for loop condition in 'bd_fs_get_fstype'
- lvm.c: Fix "use after free" in 'bd_lvm_get_thpool_meta_size'
- mdraid.c: Fix resource leaks
- part.c: Check if file discriptor is >= 0 before closing it
- kbd.c: Fix double free in 'bd_kbd_zram_get_stats'
- exec.c: Fix "use after free" in 'bd_utils_check_util_version'
- crypto.c: Use right key buffer in 'bd_crypto_luks_add_key'
- part.c: Fix possible NULL pointer dereference
- fs.c: Fix "forward null" in 'do_mount' and 'bd_fs_xfs_get_info'
- exec.c: Fix resource leaks in 'bd_utils_exec_and_report_progress'
- kbd.c: Fix potential string overflow in 'bd_kbd_bcache_create'
- part.c: Check if we've found a place to put new logical partitions
- exec.c: Ignore errors from 'g_io_channel_shutdown'
- Ignore some coverity false positive errors
- crypto.c: Fix waiting for enough entropy
- exec.c: Fix error message in 'bd_utils_exec_and_report_progress'
- Fix duplicate 'const' in generated functions
- lvm-dbus.c: Fix multiple "use after free" coverity warnings
- fs.c: Fix multiple "forward NULL" warnings in 'bd_fs_ntfs_get_info'
- dm.c: Check return values of dm_task_set_name/run/get_info functions
- dm.c: Fix uninitialized values in various dm plugin functions
- fs.c: Fix potential NULL pointer dereference

Vratislav Podzimek (3):

- Sync spec with downstream
- Add pkgconfig definitions for the utils library
- Respect the version in the blockdev.pc file

intrigeri (1):

- Support the legacy boot GPT flag


Thanks to all our contributors.

Vratislav Podzimek, 2017-10-31


Libblockdev 2.13
----------------

New minor release of the libblockdev library. Most of the changes are bugfixes
related to building and running tests on the s390 architecture and CentOS 7
aarch64. Other than that a support for checking runtime dependencies (utilities)
on demand and querying available technologies was implemented.


**Notable changes**

- builds

  - various fixes for building on s390

- tests

  - various changes allowing running the test suite on s390

  - various changes allowing running the test suite on CentOS7 aarch64

- features

  - checking for runtime dependencies on demand

  - querying available technologies


**Full list of changes**

Vojtech Trefny (14):

- Allow compiling libblockdev without s390 plugin
- Do not run g_clear_error after setting it
- Fix zFCP LUN max length
- Bypass error proxy in s390 test
- Use "AC_CANONICAL_BUILD" to check architecture instead of "uname"
- Do not include s390utils/vtoc.h in s390 plugin
- Add NEWS.rst file
- Fix source URL in spec file
- Use only one git tag for new releases
- Add new function for setting swap label
- Skip btrfs tests on CentOS 7 aarch64
- Better handle old and new zram sysfs api in tests
- Try harder when waiting for lio device to show up
- Use shorter prefix for tempfiles

Vratislav Podzimek (9):

- Add a function for getting plugin name
- Dynamically check for the required utilities
- Add functions for querying available technologies
- Simplify what WITH_BD_BCACHE changes in the KBD plugin
- Add a basic test for the runtime dependency checking
- Add missing items to particular sections in the documentation
- Assign functions to tech-mode categories
- Add a function for enabling/disabling plugins' init checks
- Fix the rpmlog and shortlog targets

Thanks to all our contributors.

Vratislav Podzimek, 2017-09-29


Libblockdev 2.12
----------------

New minor release of libblockdev library. Most changes in this release are related to
improving our test suite and fixing new issues and bugs.

**Notable changes**

- tests

  - various changes allowing running the test suite on Debian

**Full list of changes**

Kai Lüke (1):

- Wait for resized partition

Vojtech Trefny (20):

- Try to get distribution info from "PrettyName" if "CPEName" isn't available
- Require only plugins that are needed for given test
- Try harder to unmount devices in test cleanup
- Fix redirecting command output to /dev/null in tests
- Skip free region tests on Debian too
- Skip the test for device escrow on Debian too
- Skip zRAM tests on Debian
- Skip dependency checking in mpath tests on Debian
- Fix checking for available locales
- Fix names of backing files in tests
- Skip vgremove tests on 32bit Debian
- Use libmount cache when parsing /proc/mounts
- Use mountpoint for "xfs_info" calls
- Close filesystem before closing the partition during FAT resize
- Stop skipping FAT resize tests on rawhide
- Tests: Move library initialization to setUpClass method
- Add a script for running tests
- Use "run_tests" script for running tests from Makefile
- Fix label check in swap_test
- Own directories /etc/libblockdev and /etc/libblockdev/conf.d

Vratislav Podzimek (6):

- Sync spec with downstream
- Use -ff when creating PVs in FS tests
- Confirm the force when creating PVs in FS tests
- Add some space for the CI status
- Make sure the device is opened for libparted
- New version - 2.12

Thanks to all our contributors.

Vratislav Podzimek, 2017-08-30


Libblockdev 2.11
----------------

New minor release of libblockdev library.

**Notable changes**

- library

  - added option to skip dependecy check during library initialization

**Full list of changes**

Kai Lüke (2):

- Link to GObject even if no plugin is activated
- Allow non-source directory builds

Vojtech Trefny (1):

- Use new libmount function to get (un)mount error message

Vratislav Podzimek (6):

- Update the documentation URL
- Keep most utilities available for tests
- Skip zram tests on Rawhide
- Add a way to disable runtime dependency checks
- Make the KbdZRAMDevicesTestCase inherit from KbdZRAMTestCase
- New version - 2.11


Thanks to all our contributors.

Vratislav Podzimek, 2017-07-31


Libblockdev 2.10
----------------

New minor release of libblockdev library adding some new functionality in the
crypto, fs and part plugins and fixing various issues and bugs.

**Notable changes**

- crypto

  - support for opening and closing TrueCrypt/VeraCrypt volumes: ``bd_crypto_tc_open``
    and ``bd_crypto_tc_close``

- fs

  - new functions for checking of filesystem functions availability:  ``bd_fs_can_resize``,
    ``bd_fs_can_check`` and ``bd_fs_can_repair``

  - new generic function for filesystem repair and check: ``bd_fs_repair`` and ``bd_fs_check``

- part

  - newly added support for partition resizing: ``bd_part_resize_part``


**Full list of changes**

Kai Lüke (6):

- Size in bytes for xfs_resize_device
- Query functions for FS resize and repair support
- Generic Check and Repair Functions
- Add partition resize function
- Query setting FS label support and generic relabeling
- Specify tolerance for partition size

Tony Asleson (3):

- kbd.c: Make bd_kbd_bcache_create work without abort
- kbd.c: Code review corrections
- bcache tests: Remove FEELINGLUCKY checks

Tristan Van Berkom (2):

- Fixed include for libvolume_key.h
- src/plugins/Makefile.am: Remove hard coded include path in /usr prefix

Vratislav Podzimek (12):

- Try RO mount also if we get EACCES
- Adapt to a change in behaviour in new libmount
- Add functions for opening/closing TrueCrypt/VeraCrypt volumes
- Update the project/source URL in the spec file
- Compile everything with the C99 standard
- Do not strictly require all FS utilities
- Check resulting FS size in tests for generic FS resize
- Only use the exact constraint if not using any other
- Do not verify vfat FS' size after generic resize
- Limit the requested partition size to maximum possible
- Only enable partition size tolerance with alignment
- New version - 2.10

squimrel (1):

- Ignore parted warnings if possible

Thanks to all our contributors.

Vratislav Podzimek, 2017-07-05
