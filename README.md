### CI status

<img alt="CI status" src="https://fedorapeople.org/groups/storage_apis/statuses/libblockdev-master.svg" width="100%" height="350ex" />


### Introduction

libblockdev is a C library supporting GObject introspection for manipulation of
block devices. It has a plugin-based architecture where each technology (like
LVM, Btrfs, MD RAID, Swap,...) is implemented in a separate plugin, possibly
with multiple implementations (e.g. using LVM CLI or the new LVM DBus API).

For more information about the library's architecture see the specs.rst
file. For more information about the expected functionality and features see the
features.rst file.

For information about development and contributing guidelines see the
README.DEVEL.rst file.

For more information about the API see the generated documentation at
http://storaged.org/libblockdev/.


### Branches and API stability

We are currently working on a new major release 3.0 which will also include major API
overhaul, some backward-incompatible changes are already present on the *master* branch
and we do not recommend using it right now if you are not interested in libblockdev
development. Stable *2.x-branch* is still supported and we will continue to backport
bugfixes and selected new features to it from *master*.
