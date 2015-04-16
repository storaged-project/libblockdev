libblockdev's roadmap
======================

:authors:
   Vratislav Podzimek <vpodzime@redhat.com>


Releases
---------

+-----------+-------------+---------------------------------------------------------------------------------+
| **Date**  | **Version** | **Expected Features**                                                           |
+===========+=============+=================================================================================+
| May  1    |     1.0     | autotools, *kbd* and *s390* plugins, LVM cache support, first stable API        |
+-----------+-------------+---------------------------------------------------------------------------------+
| Aug  1    |     2.0     | *part* and *fs* plugins, progress reporting, new API (so-name bump)             |
+-----------+-------------+---------------------------------------------------------------------------------+


Tasks
------

* migrate from scons to autotools (vpodzime)
* develop the *kbd* (*Kernel Block Devices*) plugin (vpodzime)
* develop the *s390* plugin (sbueno)
* implement the LVM cache support (vpodzime)
* develop the *part* plugin (vpodzime)
* develop the *fs* plugin (vpodzime)
* design and implement progress reporting (vpodzime)
