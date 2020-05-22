Features
=========

*libblockdev* is a C library supporting GObject introspection for manipulation
of block devices. It has a plugin-based architecture where each technology (like
LVM, Btrfs, MD RAID, Swap,...) is implemented in a separate plugin, possibly
with multiple implementations (e.g. using LVM CLI or the new LVM DBus
API). Every plugin is also usable as a standalone shared library.


partitions
-----------

:supported technologies:
   MBR, GPT, partition manipulation on partitionable devices

:functions:
   * mklabel
   * create_part
   * resize_part
   * remove_part


filesystems
------------

:supported technologies:
   * DONE: ext2, ext3, ext4, xfs, vfat, ntfs

:functions:
   * make_FSTYPE
   * resize_FSTYPE
   * check_FSTYPE
   * repair_FSTYPE
   * change_label_FSTYPE

   * wipefs
   * mount
   * unmount
   * generic_resize
   * freeze
   * unfreeze


LVM
----

:supported technologies:
   "plain LVM", LVM Thin Provisioning

:TODO:
   * read-only locking and default config

:functions:
   * get_global_config
   * set_global_config
   * get_possible_pe_sizes
   * is_supported_pe_size
   * get_max_lv_size
   * round_size_to_pe
   * get_lv_physical_size
   * get_thpool_padding
   * is_valid_thpool_metadata_size
   * is_valid_thpool_chunk_size

   * pvcreate
   * pvresize
   * pvremove
   * pvmove
   * pvscan
   * pvinfo
   * pvs

   * vgcreate
   * vgextend
   * vgremove
   * vgactivate
   * vgdeactivate
   * vgreduce
   * vginfo
   * vgs

   * lvorigin
   * lvcreate
   * lvremove
   * lvresize
   * lvactivate
   * lvdeactivate
   * lvsnapshotcreate
   * lvsnapshotmerge
   * lvinfo
   * lvs

   * thpoolcreate
   * thlvcreate
   * thlvpoolname
   * thsnapshotcreate

   * cache_get_default_md_size
   * cache_get_mode_str
   * cache_get_mode_from_str
   * cache_create_pool
   * cache_attach
   * cache_detach
   * cache_create_cached_lv
   * cache_pool_name
   * cache_stats

   * data_lv_name
   * metadata_lv_name

   * thpool_convert
   * cache_pool_convert

BTRFS
------

:supported technologies:
   btrfs as both filesystem and multi-device volume, subvolumes, snapshots

:functions:
   * create_volume
   * add_device
   * remove_device
   * list_devices
   * create_subvolume
   * delete_subvolume
   * list_subvolumes
   * get_default_subvolume
   * set_default_subvolume
   * create_snapshot
   * filesystem_info
   * mkfs
   * resize
   * check
   * repair
   * change_label


SWAP
-----

:supported technologies:
   swap partitions/LVs, swap files

:functions:
   * mkswap
   * swapon
   * swapoff
   * swapstatus


MDRAID
-------

:supported technologies:
   all RAID levels supported by the MD RAID

:functions:
   * get_superblock_size
   * create
   * destroy
   * activate
   * deactivate
   * run
   * nominate
   * denominate
   * add
   * remove
   * examine
   * canonicalize_uuid
   * get_md_uuid
   * detail
   * node_from_name
   * name_from_node


CRYPTO/LUKS
------------

:supported technologies:
   LUKS1 and LUKS2 encrypted devices, TrueCrypt/VeraCrypt devices (open/close only)

:functions:
   * generate_backup_passphrase
   * device_is_luks
   * luks_uuid
   * luks_status
   * luks_format
   * luks_open
   * luks_close
   * luks_add_key
   * luks_remove_key
   * luks_change_key
   * luks_resize
   * luks_suspend
   * luks_resume
   * luks_header_backup
   * luks_header_restore
   * luks_kill_slot
   * luks_info
   * integrity_info
   * escrow_device
   * tc_open
   * tc_close


MULTIPATH
----------

:supported technologies:
   just very basic functionality

:functions:
   * flush_mpaths
   * device_is_mpath_member
   * get_mpath_members
   * set_friendly_names


LOOP
-----

:supported technologies:
   basic operations with loop devices

:functions:
   * get_backing_file
   * get_loop_name
   * loop_setup
   * loop_teardown


DEVICE MAPPER
--------------

:supported technologies:
   basic operations with raw device mapper and DM RAID sets

:functions:
   * create_linear
   * remove
   * node_from_name
   * name_from_node
   * map_exists
   * get_member_raid_sets
   * activate_raid_set
   * deactivate_raid_set
   * get_raid_set_type


s390
-----

:supported technologies:
   DASD, zFCP

:functions:
   * s390_dasd_format
   * s390_dasd_needs_format
   * s390_dasd_online
   * s390_dasd_is_ldl
   * s390_dasd_is_fba
   * s390_sanitize_dev_input
   * s390_zfcp_sanitize_wwpn_input
   * s390_zfcp_sanitize_lun_input
   * s390_zfcp_online
   * s390_zfcp_scsi_offline
   * s390_zfcp_offline


KBD (Kernel Block Devices)
---------------------------

:supported technologies:
   bcache, zram

:functions:
   * bcache_create
   * bcache_destroy
   * bcache_attach
   * bcache_detach
   * bcache_status
   * bcache_set_mode
   * bcache_get_mode
   * bcache_get_backing_device
   * bcache_get_cache_device

   * zram_create_devices
   * zram_destroy_devices
   * zram_get_stats

NVDIMM
-------

:supported technologies:
   namespaces

:functions:
   * namespace_enable
   * namespace_disable
   * namespace_info
   * namespace_reconfigure
   * list_namespaces

VDO
---

Standalone VDO plugin is deprecated since 2.24 and will be removed in upcoming 3.0.
Deduplication and compression support is now provided by LVM VDO volumes and pools with
support for these added in 2.23.

:functions:
   * info
   * create
   * remove
   * change_write_policy
   * enable_compression
   * disable_compression
   * enable_deduplication
   * disable_deduplication
   * activate
   * deactivate
   * start
   * stop
   * grow_logical
   * grow_physical
   * get_statistics

utils
------

Library (not a plugin) providing utility functions usable for multiple plugins
and any third-party code.

:functions:
   * exec_and_report_error
   * exec_and_capture_output
   * size_human_readable
   * size_from_spec
   * init_logging
   * version_cmp
   * check_util_version
