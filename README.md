### CI status

<img alt="CI status" src="https://fedorapeople.org/groups/storage_apis/statuses/libblockdev-master.svg" width="100%" height="350ex" />

### Introduction

libblockdev is a C library supporting GObject introspection for manipulation of
block devices. It has a plugin-based architecture where each technology (like
LVM, Btrfs, MD RAID, Swap,...) is implemented in a separate plugin, possibly
with multiple implementations (e.g. using LVM CLI or the new LVM DBus API).

#### Features

Following storage technologies are supported by libblockdev

 - partitions
   - MSDOS, GPT
 - filesystem operations
   - ext2, ext3, ext4, xfs, vfat, ntfs, exfat, btrfs, f2fs, nilfs2, udf
   - mounting
 - LVM
   - thin provisioning, LVM RAID, cache, LVM VDO
 - BTRFS
   - multi-device volumes, subvolumes, snapshots
 - swap
 - encryption
   - LUKS, TrueCrypt/VeraCrypt, BitLocker, FileVault2
   - integrity
 - DM (device mapper)
 - loop devices
 - MD RAID
 - multipath
 - s390
   - DASD, zFCP
 - NVDIMM namespaces
 - NVMe

#### Architecture

The library itself is only a thin wrapper around a set of plugins, each for a
particular technology listed above. The library provides an API consisting of
sets of functions provided by its plugins. For example, there is a
symbol/function called ``bd_lvm_lvcreate`` provided by the library which is
dynamically loaded from the LVM plugin when the library's ``bd_init`` function
will be called. Initially all those functions are no-ops just printing a warning
on stderr and doing nothing. This way applications using the library won't
crash, the operations just won't be run. Of course, the library
has ``bd_is_plugin_available``, which allows applications to check if something
is provided/implemented or not.

#### Technologies behind the library

The library is written in C using the GLib library. The GLib library provides a
lot of handy utilities that may be used by the library (no need for a new
implementation of hash tables, lists, etc.) and moreover, it should be really
easy to create bindings for the library via GObject introspection that works
even with "not OOP" code. However, instead of returning links to structs
(e.g. as a return value of the ``bd_lvm_vginfo`` function) it will return
references to GObjects with attributes/properties and access methods. The reason
is again an easy way to create bindings which we get for free.

### License

The libblockdev code is licensed under LGPL 2.1 or later, see [LICENSE](LICENSE)
for full text of the license.

### Development

For developer documentation see [README.DEVEL.md](README.DEVEL.md).

API documentation is available at [https://storaged.org/libblockdev](https://storaged.org/libblockdev).

#### Branches and supported versions

The currently actively developed and supported version is 3.x on the *main* branch.
New features should target this release.

The older 2.x version available on the *2.x-branch* is still supported but new features
are not planned for this release.
