Rationale and specifications of the libblockdev library
========================================================

:Authors:
   Vratislav Podzimek <vpodzime@redhat.com>

Scope
------

libblockdev library is a library for doing operations with block devices
replacing and extending the code from the blivet.devicelibs (python-blivet)
modules as well as udisks2. It supports the following storage technologies:

* partitions
* filesystem operations
* LVM
* BTRFS
* SWAP
* cryptsetup/LUKS
* DM (device mapper)
* loop devices
* MD RAID
* multipath
* DASD
* NVDIMM namespaces
* VDO volumes

Of course some additional technologies may be supported in the future.


Architecture
-------------

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

Although the architecture described in the previous paragraph may look overly
complicated, it brings quite nice advantages:

1. a different implementation of the operations for technologies can be easily
   used/tested (e.g. LVM plugin using binaries vs. LVM plugin using liblvm), as
   part of the library initialization, the application may explicitly state it
   wants to use a particular plugin

2. if some functionality is missing in a plugin, no crash will appear

3. plugins may be used per se without the library (if some application needs to
   do e.g. LVM-only operations)

4. plugins may be implemented in various languages as long as they provide
   functions with the right names in a way that those functions can be
   dynamically loaded


Technologies behind the library
--------------------------------

The library is written in C using the GLib library. The GLib library provides a
lot of handy utilities that may be used by the library (no need for a new
implementation of hash tables, lists, etc.) and moreover, it should be really
easy to create bindings for the library via GObject introspection that works
even with "not OOP" code. However, instead of returning links to structs
(e.g. as a return value of the ``bd_lvm_vginfo`` function) it will return
references to GObjects with attributes/properties and access methods. The reason
is again an easy way to create bindings which we get for free. The
implementation gathers knowledge from the blivet, system-storage-manager (SSM),
udisks2 projects.


Purpose of the library
-----------------------

The library replaces the blivet.devicelibs code as well as udisks2's low-level
code and is available for other new projects.
