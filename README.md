### CI status

<img alt="CI status" src="https://fedorapeople.org/groups/storage_apis/statuses/libblockdev-2.x.svg" width="100%" height="300ex" />


### Introduction

libblockdev is a C library supporting GObject introspection for manipulation of
block devices. It has a plugin-based architecture where each technology (like
LVM, Btrfs, MD RAID, Swap,...) is implemented in a separate plugin, possibly
with multiple implementations (e.g. using LVM CLI or the new LVM DBus API).

For more information about the library's architecture see the specs.rst
file. For more infomation about the expected functionality and features see the
features.rst file.

For information about development and contributing guidelines see the
README.DEVEL.rst file.

For more information about the API see the generated documentation at
http://storaged.org/libblockdev/.
