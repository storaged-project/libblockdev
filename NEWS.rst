Libblockdev 3.4.0

New minor release of the libblockdev library with multiple fixes and new features. See below
for details.

**Notable changes**

- bd_nvme_connect() now defaults to port 4420 or 8009 for discovery NQN respectively when
the transport_svcid argument is not specified.

**Full list of changes**

Rodrigo Cox (1):

- tests: Simplify NVMe controller waiting with basic polling

Sam James (1):

- configure.ac: fix bashism

Tomas Bzatek (4):

- smart: Use drive self-assessment as an overall status
- tests: Fix NVMe sanitize log return codes
- nvme: Default to well-known tr_svcid values when not specified
- nvme: Handle memory allocation failures from _nvme_alloc()

Vojtech Trefny (41):

- tests: Skip LVM VDO tests if 'vdoformat' is not available
- crypto: Add a function to set persistent flags for LUKS
- docs: Add missing functions to docs/libblockdev-sections.txt
- tests: Skip NVDIMM reconfigure tests on rawhide
- ci: Switch branch for Blivet revdeps tests to 'main'
- tests: Skip NVDIMM tests completely
- Move LVM plugin to a separate directory
- Move common LVM code to a separate file
- Adjust library tests to the new LVM plugin directory structure
- tests: Fix printing skipped test cases without docstring
- lvm: Add support for running vgcfgbackup/restore
- New version - 3.3.1
- tests: Temporarily skip CryptoTestOpenClose with LUKS1 on rawhide
- packit: Limit Fedora builds and tests to the 'master' branch
- packit: Add RPM build for pull requests against the rhel10-branch
- packit: Add tmt tests for rhel10-branch running on C10S
- packit: Remove obsolete metadata dictionary from copr_build job
- tests: Skip MDTestNominateDenominate in RHEL/CentOS 10
- Fix parsing subvolumes with spaces in name
- tests: Skip MDTestNominateDenominate on RHEL/CentOS 9 too
- tests: Fix skipping escrow tests in FIPS mode
- tests: Remove code duplication in LVM and LVM DBus tests
- tests: Create more devices only for tests that need more devices
- tests: Merge some of the LUKS test cases
- crypto: Allow setting PBKDF arguments for LUKS1 too
- tests: Use fast PBKDF parameters when creating LUKS devices
- tests: Do not create two disks for all FS tests
- tests: Do not require extra disks for spares for all MD tests
- tests: Merge MD (de)activation test cases into one
- tests: Use RAID0 for MD (de)activation test case
- tests: Do not create two disks for all part tests
- tests: Create a proper single "NoDev" test case for part tests
- tests: Fix issues found by pylint
- tests: Merge swap label and UUID test cases
- misc: Do not try to install VDO on 32bit Debian
- tests: Fix SCSI debug disks setup and cleanup in SMART tests
- ci: Enable tests on aarch64 with packit and tmt
- lvm-dbus: Fix calling lvcreate with empty list of PVs
- Add a function to re-read size of the loop device
- Fix issues discovered by static analysis
- More static analysis issues fixes

dependabot[bot] (1):

- infra: bump actions/checkout from 4 to 5


Libblockdev 3.3.1
------------------

New bugfix release of libblockdev library fixing the LPE issue CVE-2025-6019.

**Full list of changes**

Thomas Blume (1):

- Don't allow suid and dev set on fs resize


Libblockdev 3.3.0
------------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Nikola Forró (1):

- packit: Fix replacement of placeholders in post-upstream-clone

Tomas Bzatek (4):

- tests: Add KINGSTON SA400S37240G SSD skdump

Vojtech Trefny (26):

- tests: Enable LVM VDO tests on Debian
- Skip tests for plugins disabled during compile time
- fs: Add filesystem size limits to BDFSFeatures
- Makefile: Fix generating RPM log during bumpver
- New version - 3.2.1
- tests: Add a simple test case for bd_crypto_device_seems_encrypted
- lvm-dbus: Add support for repairing RAID LVs
- tests: Skip lvm_dbus_tests.LvmTestPartialLVs for now
- tests: Skip escrow tests in FIPS mode
- misc: Separate Ansible tasks into a different file
- misc: Add build and test dependecies for CentOS
- ci: Manually download blivet playbooks for revdeps tests
- ci: Remove amazon-ec2-utils if installed
- ci: Manually download udisks playbooks for revdeps tests
- misc: Do not use "with_items" when installing packages
- lvm: Add support for reading lvm.conf
- lvm: Require higher version of LVM for lvm.conf parsing
- lvm: Fix check for BD_LVM_TECH_CONFIG availability in CLI plugin


Libblockdev 3.2.1
------------------

New bugfix release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

James Hilliard (1):

- crypto: check that IOC_OPAL_GET_STATUS is defined

Tomas Bzatek (3):

- smart: Clarify use of ID_ATA_SMART_ACCESS udev property
- smart: Clarify ID_ATA_SMART_ACCESS udev property values
- nvme: Avoid element-type g-i annotations

Vojtech Trefny (9):

- README: Update supported technologies
- dist: Fix source URL in spec
- packit: Fix generating spec from template
- dist: Sync spec with downstream
- misc: Fix installing test dependencies on Debian/Ubuntu
- ci: Do not try to install test dependencies for CodeQL analysis
- lvm: Clarify the global config functionallity in libblockdev
- ci: Install 'python3-libdnf5' for TMT test plans
- Makefile: Fix generating RPM log during bumpver

Libblockdev 3.2.0
------------------

This release brings new smart plugin with two actual plugin implementations:
libatasmart (default) and smartmontools (experimental). Please check the smart
plugin documentation for specifics and limitations.

Note to distributors: the default smart plugin is based on libatasmart and
requires no extra dependencies. The smartmontools plugin needs a runtime
dependency on 'smartctl' executable besides a json-glib-1.0 build dep.
A path to drivedb.h database can be specified with the --with-drivedb
option. The latter two features are mostly provided for testing.

**Full list of changes**

Giulio Benetti (1):

- Use glib2 G_GNUC_UNUSED in place of UNUSED locally defined

Jelle van der Waa (3):

- tests: split multi device tests into a new testcase class
- tests: introduce setup_test_device helper function
- btrfs: make btrfs subvolume listing consistent

Michal Rostecki (1):

- build: Fix linking with LLD

Stepan Yakimovich (1):

- crypto: Add support for conversion between different LUKS formats

Tomas Bzatek (35):

- Port to G_GNUC_INTERNAL for controlling symbols visibility
- Fix some more occurrences of missing port to G_GNUC_UNUSED
- dm_logging: Annotate redirect_dm_log() printf format
- tests: Add NVMe persistent discovery controller tests
- tests: Add NVMe controller type checks
- New SMART plugin
- smart: Add bd_smart_set_enabled()
- smart: Add bd_smart_device_self_test()
- tests: Add basic SMART tests
- tests: Add SMART tests over supplied JSON dumps
- tests: Add tests for bd_smart_set_enabled()
- tests: Add tests for bd_smart_device_self_test()
- smart: Add SCSI/SAS status retrieval
- tests: Add SCSI SMART tests
- smart: Remove the ATA low-power mode detection
- smart: Introduce well-known attribute names, validation and pretty values
- smart: Refactor and split into libbd_smartmontools
- smart: Introduce new libatasmart plugin
- smart: Implement bd_smart_ata_get_info_from_data()
- smart: Use smartmontools drivedb.h for libatasmart validation
- build: Install lvm.h when only lvm_dbus enabled
- tests: Add SiliconPower SSD skdump reporting incorrect temp
- smart: Rework libatasmart temperature reporting
- tests: Split libatasmart and smartmontool tests
- utils/exec: Refactor extra args append out
- utils/exec: Add bd_utils_exec_and_capture_output_no_progress()
- tests: Add bd_utils_exec_and_capture_output_no_progress() tests
- smart: Add BDExtraArg arguments
- tests: Adapt smart plugin tests for the added extra arguments
- tests: Fix smartmontools plugin parsing of /dev/random
- tests: Add more libatasmart skdump samples
- nvme: Fix potential memory leak
- smart: Mark drivedb integration as experimental
- smart: Add documentation
- NEWS: add preliminary release notes for the smart plugin

Vojtech Trefny (60):

- Makefile: Do not include release in the tag
- Makefile: Fix bumpver to work with micro versions
- tests: Manually remove removed PVs from LVM devices file
- tests: Ignore LVM devices file for non-LVM tests
- tests: Fix removing custom LVM devices file
- nvme: Add bd_nvme_is_tech_avail to the API file
- lvm-dbus: Fix passing size for pvresize over DBus
- lvm-dbus: Fix potential segfault in bd_lvm_init
- lvm-dbus: Fix leaking error in bd_lvm_init
- crypto: Fix double free in bd_crypto_luks_remove_key
- utils: Clarify usage of version in bd_utils_check_util_version
- Bump version to 3.1.1
- ci: Set custom release number for Packit
- tests: Fix running tests without ntfsprogs
- ci: Get version for packit from the SPEC file
- ci: Fix bumping release for Packit builds
- tests: Skip filesystem tests if kernel module is not available
- misc: Vagrantfile update
- misc: Remove CentOS 8 Stream from Vagrantfile and test dependencies
- Fix pylint possibly-used-before-assignment warning in BlockDev.py
- utils: Check also for aliases in bd_utils_have_kernel_module
- tests: Skip ExFAT UUID tests with recent exfatprogs
- fs: Ignore unused-parameter warning in the FS plugin
- fs: Ignore shift-count-overflow warning in FS plugin
- fs: Fix ignoring errors from libext2fs
- ci: Use Ubuntu 24.04 in GitHub actions
- misc: Fix enabling source repositories on latest Ubuntu
- ci: Run Blivet reverse dependency tests on pull requests
- ci: Add a simple tmt test and run it via packit
- misc: Add kernel-modules-extra to test dependencies
- ci: Run UDisks reverse dependency tests on pull requests
- tests: Skip exFAT UUID tests also on Fedora 39
- docs: Fix link to Python bindings documentation
- part: Fix copy-paste bug in bd_part_spec_copy
- infra: Add dependabot to automatically update GH actions
- lvm: Check for dm-vdo instead of kvdo module for VDO support
- lvm: Get VDO stats from device mapper instead of /sys/kvdo
- misc: Add vdo to test dependencies on Fedora
- tests: Temporarily skip LVM VDO tests on RHEL/CentOS 10
- crypto: Show error when trying using an invalid DM name
- part: Add human readable partition type to BDPartSpec
- fs: Fix docstring for bd_fs_ext?_get_min_size functions
- tests: No longer need to skip exfat UUID tests on Fedora
- crypto: Add a function to check for OPAL support for a device
- crypto: Add a function to wipe a LUKS HW-OPAL device
- crypto: Add information about HW encryption to BDCryptoLUKSInfo
- crypto: Add support for creating new LUKS HW-OPAL devices
- tests: Add a simple test case for LUKS HW-OPAL support
- crypto: Check for kernel SED OPAL support for OPAL operations
- ci: Remove priority from Testing farm repositories
- crypto: Add a function to run OPAL PSID reset
- tests: Fix skipping VDO tests on Debian and CentOS 10
- crypto: Fix name of bd_crypto_opal_wipe_device in crypto.h
- crypto: Fixing missing quotation marks in some error messages
- docs: Add BDCryptoLUKSHWEncryptionType to libblockdev-sections.txt
- docs: Fix documentation for the SMART plugin
- part: Document type_name in BDPartSpec docstring
- misc: Fix typos
- crypto: Fix GType macro for crypto context
- ci: Add a simple GH action to run spelling tools on our code

Vratislav Podzimek (1):

- Add cache size ratio to the output of lvm-cache-stats

dependabot[bot] (2):

- infra: bump actions/upload-artifact from 3 to 4
- infra: bump github/codeql-action from 2 to 3

guazhang (1):

- fixed md_create issue #1013

Libblockdev 3.1.1
------------------

New bugfix release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Giulio Benetti (1):

- Use glib2 G_GNUC_UNUSED in place of UNUSED locally defined

Tomas Bzatek (5):

- Port to G_GNUC_INTERNAL for controlling symbols visibility
- Fix some more occurrences of missing port to G_GNUC_UNUSED
- dm_logging: Annotate redirect_dm_log() printf format
- tests: Add NVMe persistent discovery controller tests
- tests: Add NVMe controller type checks

Vojtech Trefny (6):

- Makefile: Fix bumpver to work with micro versions
- tests: Manually remove removed PVs from LVM devices file
- tests: Ignore LVM devices file for non-LVM tests
- tests: Fix removing custom LVM devices file
- nvme: Add bd_nvme_is_tech_avail to the API file
- lvm-dbus: Fix passing size for pvresize over DBus

Libblockdev 3.1.0
------------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Tomas Bzatek (7):

- tests: Default to /tmp for create_sparse_tempfile()
- tests: Avoid setting up intermediary loop device for the nvme target
- tests: Remove unreliable nvme attribute checks
- lvm-dbus: Fix leaking error
- lvm-dbus: Avoid using already-freed memory
- utils: Add expected printf string annotation
- fs: Report reason for open() and ioctl() failures

Vojtech Trefny (18):

- ci: Add an action to compile libblockdev with different compilers
- Sync spec with downstream
- Add BDPluginSpec constructor and use it in plugin_specs_from_names
- overrides: Remove unused 'sys' import
- ci: Manually prepare spec file for Packit
- ci: Remove the custom version command for Packit
- swap: Add support for checking label and UUID format
- fs: Add a function to check label format for F2FS
- fs: Add a generic function to check for fs info availability
- fs: Fix allowed UUID for generic mkfs with VFAT
- fs: Add support for getting filesystem min size for NTFS and Ext
- tests: Remove some obsolete rules to skip tests
- Mark NVDIMM plugin as deprecated since 3.1
- part: Fix potential double free when getting parttype
- tests: Use BDPluginSpec constructor in LVM DBus plugin tests
- python: Add a deepcopy function to our structs
- Fix missing progress initialization in bd_crypto_luks_add_key
- tests: Skip some checks for btrfs errors with btrfs-progs 6.6.3

Libblockdev 3.0.4
------------------

New bugfix release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Jelle van der Waa (3):

- plugins: use g_autofree for free'ing g_char's
- plugins: btrfs: use g_autofree where possible for g_free
- fs: correct btrfs set label description

Tomas Bzatek (1):

- nvme: Rework memory allocation for device ioctls

Vojtech Trefny (11):

- spec: Obsolete vdo plugin packages
- spec: Move obsoleted devel subpackages to libblockdev-devel
- ci: Bump actions/checkout from v3 to v4
- part: Do not open disk read-write for read only operations
- fs: Disable progress for ntfsresize
- packit: Add configuration for downstream builds
- logging: Default to DEBUG log level if compiled with --enable-debug
- Use log function when calling a plugin function that is not loaded
- lvm-dbus: Replace g_critical calls with bd_utils_log_format
- tests: Fail early when recompilation fails in library_test
- tests: Fix "invalid escape sequence '\#'" warning from Python 3.12

Libblockdev 3.0.3
------------------

New bugfix release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Marius Vollmer (1):

- Always use "--fs ignore" with lvresize

Michael Biebl (1):

- tests: Specificy required versions when importing GLib and BlockDev
  introspection

Tomas Bzatek (3):

- nvme: Use interim buffer for nvme_get_log_sanitize()
- nvme: Generate HostID when missing
- tests: Minor NVMe HostNQN fixes

Vojtech Trefny (4):

- tests: Replace deprecated unittest assert calls
- fs: Fix leaking directories with temporary mounts
- fs: Fix memory leak
- crypto: Correctly convert passphrases from Python to C

Libblockdev 3.0.2
------------------

New bugfix release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Alexis Murzeau (1):

- Use ntfsinfo instead of ntfscluster for faster bd_fs_ntfs_get_info

Marek Szuba (1):

- docs: Fix test quotation

Michael Biebl (1):

- Restrict list of exported symbols via -export-symbols-regex

Tomas Bzatek (2):

- lib: Silence the missing DEFAULT_CONF_DIR_PATH
- loop: Report BD_LOOP_ERROR_DEVICE on empty loop devices

Vojtech Trefny (5):

- Fix formatting in NEWS.rst
- fs: Fix unused error in extract_e2fsck_progress
- fs: Use read-only mount where possible for generic FS functions
- fs: Document that generic functions can mount filesystems
- fs: Avoid excess logging in extract_e2fsck_progress

Libblockdev 3.0.1
------------------

New bugfix release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Giulio Benetti (1):

- loop: define LOOP_SET_BLOCK_SIZE is not defined

Tomas Bzatek (6):

- nvme: Mark private symbols as hidden
- build: Exit before AC_OUTPUT on error
- loop: Remove unused variable
- crypto: Remove stray struct redefinition
- boilerplate_generator: Annotate stub func args as G_GNUC_UNUSED
- fs: Simplify struct BDFSInfo

Vojtech Trefny (11):

- vdo_stats: Remove unused libparted include
- lvm: Make _vglock_start_stop static
- lvm: Fix declaration for bd_lvm_vdolvpoolname
- loop: Remove bd_loop_get_autoclear definition
- lvm: Add bd_lvm_segdata_copy/free to the header file
- fs: Add missing copy and free functions to the header file
- misc: Update steps and Dockerfile for Python documentation
- dist: Sync spec with downstream
- spec: Add dependency on libblockdev-utils to the s390 plugin
- configure: Fix MAJOR_VER macro
- Make the conf.d directory versioned

Libblockdev 3.0
---------------

New major release of the libblockdev library. This release contains a large
API overhaul, please check the documentation for full list of API changes.

**Notable changes**

- VDO a KBD plugins were removed.
- New NVMe plugin was added.
- Runtime dependencies are no longer checked during plugin initialization.
- Part plugin was rewritten to use libfdisk instead of libparted.
- Crypto plugin API went through an extensive rewrite.
- Support for new technologies was added to the crypto plugin: FileVault2 encryption,
  DM Integrity, LUKS2 tokens.
- Filesystem plugin adds support for btrfs, F2FS, NILFS2, exFAT and UDF.
- Support for new filesystem operations was added to the plugin: setting label and UUID,
  generic mkfs function and API for getting feature support for filesystems.
- dmraid support was removed from the DM plugin.
- Python 2 support was dropped.

Libblockdev 2.28
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Michael Biebl (1):

- Fix typos

Vojtech Trefny (17):

- lvm: Fix bd_lvm_get_supported_pe_sizes in Python on 32bit
- tests: Create bigger devices for XFS tests
- tests: Use ext instead of xfs in MountTestCase.test_mount_ro_device
- mpath: Memory leak fix
- spec: Require the same version utils from plugins
- mdraid: Try harder to get container UUID in bd_md_detail
- Add a test case for DDF arrays/containers
- mdraid: Do not ignore errors from bd_md_canonicalize_uuid in bd_md_examine
- mdraid: Try harder to get container UUID in bd_md_examine
- mdraid: Fix copy-paste error when checking return value
- tests: Wait for raid and mirrored LVs to be synced before removing
- tests: Make smaller images for test_lvcreate_type
- dm: Fix comparing DM RAID member devices UUID
- mdraid: Fix use after free
- ci: Add .lgtm.yaml config for LGTM
- ci: Add GitHub actions for running rpmbuilds and csmock
- mpath: Fix potential NULL pointer dereference

zhanghongtao (1):

- Fix mismatched functions return value type


Libblockdev 2.27
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Tomas Bzatek (1):

- fs: Return BD_FS_ERROR_UNKNOWN_FS on mounting unknown filesystem

Vojtech Trefny (21):

- overrides: Fix translating exceptions in ErrorProxy
- tests: Do not check that swap flag is not supported on DOS table
- tests: Lower expected free space on newly created Ext filesystems
- tests: Remove test for NTFS read-only mounting
- vdo_stats: Default to 100 % savings for invalid savings values
- lvm: Fix reading statistics for VDO pools with VDO 8
- tests: Fix creating loop device for CryptoTestLuksSectorSize
- tests: Use losetup to create 4k sector size loop device for testing
- s390: Remove double fclose in bd_s390_dasd_online (#2045784)
- lvm-dbus: Add support for changing compression and deduplication
- tests: Skip test_lvcreate_type on CentOS/RHEL 9
- tests: Fix expected extended partition flags with new parted
- lvm: Do not set global config to and empty string
- lvm: Do not include duplicate entries in bd_lvm_lvs output
- lvm: Use correct integer type in for comparison
- crypto: Remove useless comparison in progress report in luks_format
- boilerplate_generator: Remove unused variable assignment
- kbd: Add missing progress reporting to bd_kbd_bcache_create
- kbd: Fix leaking error in bd_kbd_bcache_detach
- kbd: Fix potential NULL pointer dereference in bd_kbd_bcache_create
- crypto: Remove unused and leaking error in write_escrow_data_file

Libblockdev 2.26
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Manuel Wassermann (1):

- exec: Fix deprecated glib function call Glib will rename
  "g_spawn_check_exit_status()" to "g_spawn_check_wait_status()" in version
  2.69.

Tomasz Paweł Gajc (1):

- remove unused variable and fix build with LLVM/clang

Vojtech Trefny (22):

- NEWS.rts: Fix markup
- crypto: Fix default key size for non XTS ciphers
- vdo: Do not use g_memdup in bd_vdo_stats_copy
- fs: Allow using empty label for vfat with newest dosfstools
- tests: Call fs_vfat_mkfs with "--mbr=n" extra option in tests
- kbd: Fix memory leak
- crypto: Fix memory leak
- dm: Fix memory leak in the DM plugin and DM logging redirect function
- fs: Fix memory leak
- kbd: Fix memory leak
- lvm-dbus: Fix memory leak
- mdraid: Fix memory leak
- swap: Fix memory leak
- tests: Make sure the test temp mount is always unmounted
- tests: Do not check that XFS shrink fails with xfsprogs >= 5.12
- tests: Temporarily skip test_snapshotcreate_lvorigin_snapshotmerge
- Fix skipping tests on Debian testing
- crypto: Let cryptsetup autodect encryption sector size when not specified
- tests: Do not try to remove VG before removing the VDO pool
- tests: Force remove LVM VG /dev/ entry not removed by vgremove
- tests: Tag LvmPVVGLVcachePoolCreateRemoveTestCase as unstable
- Add missing plugins to the default config


Libblockdev 2.25
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Full list of changes**

Tomas Bzatek (6):

- exec: Fix polling for stdout and stderr
- exec: Use non-blocking read and process the buffer manually
- exec: Clarify the BDUtilsProgExtract callback documentation
- tests: Add bufferbloat exec tests
- tests: Add null-byte exec tests
- lvm: Fix bd_lvm_vdopooldata_* symbols

Vojtech Trefny (10):

- exec: Fix setting locale for util calls
- fs: Do not report error when errors were fixed by e2fsck
- README: Use CI status image for 2.x-branch on 2.x
- fs: Fix compile error in ext_repair caused by cherry pick from master
- Mark all GIR file constants as guint64
- lvm: Set thin metadata limits to match limits LVM uses in lvcreate
- lvm: Do not use thin_metadata_size to recommend thin metadata size
- lvm: Use the UNUSED macro instead of __attribute__((unused))
- Fix max size limit for LVM thinpool metadata
- loop: Retry LOOP_SET_STATUS64 on EAGAIN


Libblockdev 2.24
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- vdo

  - VDO plugin has been deprecated in this release (functionality replaced by LVM VDO)

- lvm

  - support for creating and managing LVM VDO volumes added

- crypto

  - support for unlocking of BitLocker-compatible format BITLK added (requires cryptsetup 2.3.0)

**Full list of changes**

Lars Wendler (1):

- configure.ac: Avoid bashisms

Matt Thompson (1):

- Fixed a number of memory leaks in lvm-dbus plugin

Matt Whitlock (1):

- configure.ac: Avoid more bashisms

Tomas Bzatek (4):

- utils: Add functions to get and check current linux kernel version
- vdo: Fix a memleak
- exec: Fix a memleak
- mount: Fix a memleak

Vojtech Trefny (47):

- Sync spec with downstream
- Use 'explicit_bzero' to erase passphrases from key files
- Add new function 'bd_fs_wipe_force' to control force wipe
- Fix linking against utils on Debian
- exec.c: Fix reading outputs with null bytes
- fs: Fix checking for UID/GID == 0
- Fix expected cache pool name with newest LVM
- Fix memory leak in LVM DBus plugin
- Manually remove symlinks not removed by udev in tests
- Add a helper function for closing an active crypto device
- Add support for BitLocker encrypted devices using cryptsetup
- ext: Return empty string instead of "<none>" for empty UUID
- Fix typo in (un)mount error messages
- vdo: Run "vdo create" with "--force"
- lvm-dbus: Do not activate LVs during pvscan --cache
- lvm-dbus: Fix memory leak in bd_lvm_thlvpoolname
- tests: Specify loader for yaml.load in VDO tests
- Add a function to check if a tool supports given feature
- Do not hardcode pylint executable name in Makefile
- Fix LVM plugin so names in tests
- Add support for creating and managing VDO LVs with LVM
- Add some helper functions to get LVM VDO mode and state strings
- Fix converting to VDO pool without name for the VDO LV
- Add write policy and index size to LVM VDO data
- Fix getting string representation of unknown VDO state index
- Fix getting VDO data in the LVM DBus plugin
- Allow calling LVM functions without locking global_config_lock
- Add extra parameters for creating LVM VDO volumes
- Add function to get LVM VDO write policy from a string
- exec: Disable encoding when reading data from stdout/stderr
- Fix copy-paste bug in lvm.api
- Move VDO statistics code to a separate file
- Add functions to get VDO stats for LVM VDO volumes
- lvm-dbus: Get data LV name for LVM VDO pools too
- lvm: Add a function to get VDO pool name for a VDO LV
- lvm-dbus: Add LVM VDO pools to bd_lvm_lvs
- tests: Skip LVM VDO tests if kvdo module cannot be loaded
- Do not skip LVM VDO tests when the kvdo module is already loaded
- lvm: Fix getting cache stats for cache thinpools
- Create a common function to get label and uuid of a filesystem
- Do not open devices as read-write for read-only fs operations
- Use libblkid to get label and UUID for XFS filesystems
- Do not check VDO saving percent value in LVM DBus tests
- utils: Remove deadcode in exec.c
- fs: Fix potential NULL pointer dereference in mount.c
- Fix multiple uninitialized values discovered by coverity
- Mark VDO plugin as deprecated since 2.24

Libblockdev 2.23
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- fs

  - new functions for (un)freezing filesystems added

- tests

  - test suite can now be run against installed version of libblockdev


**Full list of changes**

Vojtech Trefny (28):

- Skip bcache tests on all Debian versions
- Add a function to check whether a path is a mounpoint or not
- Add function for (un)freezing filesystems
- Add a decorator for "tagging" tests
- Use test tags for skipping tests
- Use the new test tags in tests
- Remove duplicate test case
- Allow running tests against installed libblockdev
- Add a special test tag for library tests that recompile plugins
- Force LVM cli plugin in lvm_test
- Mark 'test_set_bitmap_location' as unstable
- Add ability to read tests to skip from a config file
- Skip bcache tests if make-bcache is not installed
- Use the new config file for skipping tests
- Ignore coverity deadcode warnings in the generated code
- Ignore coverity deadcode warning in 'bd_fs_is_tech_avail'
- Mark 'private' plugin management functions as static
- Remove unused 'get_PLUGIN_num_functions' and 'get_PLUGIN_functions' functions
- Mark LVM global config locks as static
- Hide filesystem-specific is_tech_available functions
- Use 'kmod_module_probe_insert_module' function for loading modules
- Fix parsing distro version from CPE name
- Move the NTFS read-only device test to a separate test case
- Print skipped test "results" to stderr instead of stdout
- Fix LVM_MAX_LV_SIZE in the GIR file
- Fix skipping NTFS read-only test case on systems without NTFS
- Skip tests for old-style LVM snapshots on recent Fedora
- Fix how we get process exit code from g_spawn_sync

Libblockdev 2.22
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- nvdimm

  - new function for getting list of supported sector sizes for namespaces

- fixes

  - multiple memory leaks fixed


**Full list of changes**

Adam Williamson (1):

- Sync spec file with python2 obsoletion added downstream

Tomas Bzatek (17):

- bd_fs_xfs_get_info: Allow passing error == NULL
- lvm: Fix some obvious memory leaks
- lvm: Use g_ptr_array_free() for creating lists
- lvm: Fix leaking BDLVMPVdata.vg_uuid
- exec: Fix some memory leaks
- mdraid: Fix g_strsplit() leaks
- s390: Fix g_strsplit() leaks
- ext: Fix g_strsplit() leaks
- ext: Fix g_match_info_fetch() leaks
- kbd: Fix g_match_info_fetch() leaks
- part: Fix leaking objects
- ext: Fix leaking string
- part: Fix leaking string in args
- mdraid: Fix leaking error
- mdraid: Fix leaking BDMDExamineData.metadata
- btrfs: Fix number of memory leaks
- module: Fix libkmod related leak

Vojtech Trefny (7):

- Sync spec with downstream
- Allow skiping tests only based on architecture
- New function to get supported sector sizes for NVDIMM namespaces
- Use existing cryptsetup API for changing keyslot passphrase
- tests: Fix removing targetcli lun
- Remove device-mapper-multipath dependency from fs and part plugins
- tests: Fix Debian testing "version" for skipping


Libblockdev 2.21
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- crypto

  - default key size for LUKS was changed to 512bit

- tools

  - new simple cli tools that use libblockdev
  - first tool is ``lvm-cache-stats`` for displaying stats for LVM cache devices
  - use configure option ``--without-tools`` to disable building these


**Full list of changes**

Vojtech Trefny (19):

- Use libblkid to check swap status before swapon
- Add error codes and Python exceptions for swapon fails
- Add libblkid-devel as a build dependency for the swap plugin
- Skip VDO grow physical test
- crypto_test.py: Use blkid instead of lsblk to check luks label
- Use major/minor macros from sys/sysmacros.h instead of linux/kdev_t.h
- Add custom error message for wrong passphrase for open
- Skip LUKS2+integrity test on systems without dm-integrity module
- Use cryptsetup to check LUKS2 label
- Fix LUKS2 resize password test
- crypto: Do not try to use keyring on systems without keyring support
- lvm-dbus: Do not pass extra arguments enclosed in a tuple
- Enable cryptsetup debug messages when compiled using --enable-debug
- vagrant: install 'autoconf-archive' on Ubuntu
- vagrant: remove F27 and add F29
- Add 'autoconf-archive' to build requires
- tests: Remove some old/irrelevant skips
- tests: Stop skipping some tests on Debian testing
- Fix checking swap status on lvm/md

Vratislav Podzimek (6):

- Discard messages from libdevmapper in the LVM plugins
- Add a tool for getting cached LVM statistics
- Make building tools optional
- Document what the 'tools' directory contains
- Add a new subpackage with the tool(s)
- Use 512bit keys in LUKS by default

Libblockdev 2.20
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- fixes

  - Fix parsing extra arguments for LVM methods calls in the LVM DBus plugin.
  - Multiple fixes for running tests on Debian testing.

- development

  - Vagrantfile template was added for easy development machine setup.

**Full list of changes**

Dennis Schridde (1):

- Fix build of plugins by changing linking order

Vojtech Trefny (17):

- Fix spacing in NEWS.rst
- Fix licence header in dbus.c
- Do not require 'dmraid' package if built without dmraid support
- Always build the VDO plugin
- kbd: Check for zram module availability in 'bd_kbd_is_tech_avail'
- Fix skipping zram tests on Fedora 27
- Build the dm plugin without dmraid support on newer RHEL
- tests: Try harder to get distribution version
- Skip bcache tests on Debian testing
- Skip NTFS mount test on Debian testing
- Skip MDTestAddRemove on Debian
- lvm-dbus: Fix parsing extra arguments for LVM methods calls
- Fix how we check zram stats from /sys/block/zram0/stat
- Add some missing test dependencies to the vagrant template
- Add Ubuntu 18.04 VM configuration to the vagrant template
- Skip nvdimm tests on systems without ndctl
- Require newer version of cryptsetup for LUKS2 tests

Vratislav Podzimek (6):

- Mark the function stubs as static
- Fix the error message when deleting partition fails
- Add a Vagrantfile template
- Document what the 'misc' directory contains
- Fix how/where the bcache tests are skipped
- Use unsafe caching for storage for devel/testing VMs


Libblockdev 2.19
----------------

New minor release of the libblockdev library with multiple fixes. See below
for details.

**Notable changes**

- features

  - vdo: new functions to get statistical data for existing VDO volumes (`bd_vdo_get_stats`)
  - crypto: support for passing extra arguments for key derivation function when creating LUKS2 format

**Full list of changes**

Max Kellermann (8):

- fix -Wstrict-prototypes
- exec: make `msg` parameters const
- plugins/check_deps: make all strings and `UtilDep` instances `const`
- plugins/crypto: work around -Wdiscarded-qualifiers
- plugins/dm: add explicit cast to work around -Wdiscarded-qualifiers
- plugins/lvm{,-dbus}: get_lv_type_from_flags() returns const string
- plugins/kbd: make wait_for_file() static
- pkg-config: add -L${libdir} and -I${includedir}

Tom Briden (1):

- Re-order libbd_crypto_la_LIBADD to fix libtool issue

Tomas Bzatek (2):

- vdo: Properly destroy the yaml parser
- fs: Properly close both ends of the pipe

Vojtech Trefny (33):

- Sync spec with downstream
- Do not build VDO plugin on non-x86_64 architectures
- Show simple summary after configure
- Add Python override for bd_crypto_tc_open_full
- Add a simple test case for bd_crypto_tc_open
- Use libblkid in bd_crypto_is_luks
- Make sure all our free and copy functions work with NULL
- Fix few wrong names in doc strings
- Use versioned command for Python 2
- Reintroduce python2 support for Fedora 29
- Allow specifying extra options for PBKDF when creating LUKS2
- configure.ac: Fix missing parenthesis in blkid version check
- acinclude.m4: Use AS_EXIT to fail in LIBBLOCKDEV_FAILURES
- Skip 'test_cache_pool_create_remove' on CentOS 7
- BlockDev.py Convert dictionary keys to set before using them
- Make sure library tests properly clean after themselves
- Make sure library_test works after fixing -Wstrict-prototypes
- Do not build btrfs plugin on newer RHEL
- Do not build KBD plugin with bcache support on RHEL
- Skip btrfs tests if btrfs module is not available
- Add version to tests that should be skipped on CentOS/RHEL 7
- Skip VDO tests also when the 'kvdo' module is not available
- Fix how we check zram stats from /sys/block/zram0/mm_stat
- Fix calling BlockDev.reinit in swap tests
- Fix vdo configuration options definition in spec file
- Fix running pylint in tests
- Ignore "bad-super-call" pylint warning in BlockDev.py
- Fix three memory leaks in lvm-dbus.c
- Fix licence headers in sources
- lvm.c: Check for 'lvm' dependency in 'bd_lvm_is_tech_avail'
- lvm-dbus.c: Check for 'lvmdbus' dependency in 'bd_lvm_is_tech_avail'
- Add test for is_tech_available with multiple dependencies
- Use python interpreter explicitly when running boilerplate_generator.py

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
