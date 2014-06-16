Features
=========

The libblockdev library will be state-less from the device perspective. There
may (and probably will) be some library-wide as well as technology-wide
configuration (e.g. default configuration for the LVM functions), but the
library won't keep state of the devices. So if for example the lvcreate()
function is called with a non-existing or full VG as a parameter, the function
will fail when trying to create the LV not in any pre-processing check.


partitions
-----------

:supported technologies:
   MBR, GPT, partition manipulation on partitionable devices

:functions:
   mklabel
   create_part
   resize_part
   remove_part


filesystems
------------

:supported technologies:
   ext2, ext3, ext4, xfs, vfat

:functions:
   make_FSTYPE
   resize_FSTYPE
   check_FSTYPE
   repair_FSTYPE
   change_label_FSTYPE
   snapshot_FSTYPE

   wipefs


LVM
----

:supported technologies:
   "plain LVM", LVM Thin Provisioning

:functions:
   * get_global_config
   * set_global_config
   * get_possible_pe_sizes [DONE]
   * is_supported_pe_size [DONE]
   * get_max_lv_size [DONE]
   * round_size_to_pe [DONE]
   * get_lv_physical_size [DONE]
   * get_thpool_padding [DONE]
   * is_valid_thpool_metadata_size [DONE]
   * is_valid_thpool_chunk_size [DONE]

   * pvcreate [DONE]
   * pvresize [DONE]
   * pvremove [DONE]
   * pvmove [DONE]
   * pvscan [DONE]
   * pvinfo [DONE]
   * pvs [DONE]

   * vgcreate [DONE]
   * vgextend [DONE]
   * vgremove [DONE]
   * vgactivate [DONE]
   * vgdeactivate [DONE]
   * vgreduce [DONE]
   * vginfo [DONE]
   * vgs [DONE]

   * lvorigin
   * lvcreate
   * lvremove
   * lvresize
   * lvactivate
   * lvdeactivate
   * lvinfo
   * lvsnapshotcreate
   * lvsnapshotmerge
   * lvs

   * thpoolcreate
   * thlvcreate
   * thlvpoolname
   * thinsnapshotcreate


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
   * create_snapshot
   * mkfs_btrfs
   * resize_btrfs
   * check_btrfs
   * repair_btrfs
   * change_label_btrfs
   * summarize_filesystem


SWAP
-----

:supported technologies:
   swap partitions/LVs, swap files

:functions:
   * mkswap [DONE]
   * swapon [DONE]
   * swapoff [DONE]
   * swapstatus [DONE]


MDRAID
-------

:supported technologies:
   all RAID levels supported by the MD RAID

:functions:
   * get_raid_superblock_size
   * mdcreate
   * mddestroy
   * mdadd
   * mdactivate
   * mdremove
   * mddeactivate
   * mdresize
   * mdexamine
   * mddetail
   * mdmemberstatus
   * md_node_from_name
   * name_from_md_node


CRYPTO/LUKS
------------

:supported technologies:
   only LUKS encrypted devices

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
   * luks_resize


MULTIPATH
----------

:supported technologies:
   just very basic functionality

:functions:
   * flush_mpaths
   * device_is_mpath_member
   * set_friendly_names


LOOP
-----

:supported technologies:
   basic operations with loop devices

:functions:
   * get_backing_file [DONE]
   * get_loop_name
   * loop_setup
   * loop_teardown


DEVICE MAPPER
--------------

:supported technologies:
   basic operations with raw device mapper

:functions:
   * dm_create_linear
   * dm_remove
   * dm_node_from_name
   * name_from_dm_node
