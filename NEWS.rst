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
