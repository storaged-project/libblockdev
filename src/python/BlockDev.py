"""
This code wraps the bindings automatically created by gobject-introspection.
They allow for creating more pythonic bindings where necessary.  For instance
this code allows many functions with default-value arguments to be called
without specifying such arguments.
"""

from gi.importer import modules
from gi.overrides import override

BlockDev = modules['BlockDev']._introspection_module
__all__ = []

_init = BlockDev.init
@override(BlockDev.init)
def init(force_plugins=None, log_func=None):
    return _init(force_plugins, log_func)
__all__.append("init")

_reinit = BlockDev.reinit
@override(BlockDev.reinit)
def reinit(force_plugins=None, reload=True, log_func=None):
    return _reinit(force_plugins, reload, log_func)
__all__.append("reinit")


_btrfs_create_volume = BlockDev.btrfs_create_volume
@override(BlockDev.btrfs_create_volume)
def btrfs_create_volume(devices, label=None, data_level=None, md_level=None):
    return _btrfs_create_volume(devices, label, data_level, md_level)
__all__.append("btrfs_create_volume")

_btrfs_mkfs = BlockDev.btrfs_mkfs
@override(BlockDev.btrfs_mkfs)
def btrfs_mkfs(devices, label=None, data_level=None, md_level=None):
    return _btrfs_mkfs(devices, label, data_level, md_level)
__all__.append("btrfs_mkfs")


_crypto_luks_format = BlockDev.crypto_luks_format
@override(BlockDev.crypto_luks_format)
def crypto_luks_format(device, cipher=None, key_size=0, passphrase=None, key_file=None):
    return _crypto_luks_format(device, cipher, key_size, passphrase, key_file)
__all__.append("crypto_luks_format")

_crypto_luks_open = BlockDev.crypto_luks_open
@override(BlockDev.crypto_luks_open)
def crypto_luks_open(device, name, passphrase=None, key_file=None):
    return _crypto_luks_open(device, name, passphrase, key_file)
__all__.append("crypto_luks_open")

_crypto_luks_add_key = BlockDev.crypto_luks_add_key
@override(BlockDev.crypto_luks_add_key)
def crypto_luks_add_key(device, pass_=None, key_file=None, npass=None, nkey_file=None):
    return _crypto_luks_add_key(device, pass_, key_file, npass, nkey_file)
__all__.append("crypto_luks_add_key")

_crypto_luks_remove_key = BlockDev.crypto_luks_remove_key
@override(BlockDev.crypto_luks_remove_key)
def crypto_luks_remove_key(device, pass_=None, key_file=None):
    return _crypto_luks_remove_key(device, pass_, key_file)
__all__.append("crypto_luks_remove_key")

_crypto_escrow_device = BlockDev.crypto_escrow_device
@override(BlockDev.crypto_escrow_device)
def crypto_escrow_device(device, passphrase, cert_data, directory, backup_passphrase=None):
    return _crypto_escrow_device(device, passphrase, cert_data, directory, backup_passphrase)
__all__.append("crypto_escrow_device")


_dm_create_linear = BlockDev.dm_create_linear
@override(BlockDev.dm_create_linear)
def dm_create_linear(map_name, device, length, uuid=None):
    return _dm_create_linear(map_name, device, length, uuid)
__all__.append("dm_create_linear")

_dm_get_member_raid_sets = BlockDev.dm_get_member_raid_sets
@override(BlockDev.dm_get_member_raid_sets)
def dm_get_member_raid_sets(name=None, uuid=None, major=-1, minor=-1):
    return _dm_get_member_raid_sets(name, uuid, major, minor)
__all__.append("dm_get_member_raid_sets")


_lvm_round_size_to_pe = BlockDev.lvm_round_size_to_pe
@override(BlockDev.lvm_round_size_to_pe)
def lvm_round_size_to_pe(size, pe_size=0, roundup=True):
    return _lvm_round_size_to_pe(size, pe_size, roundup)
__all__.append("lvm_round_size_to_pe")

_lvm_get_thpool_padding = BlockDev.lvm_get_thpool_padding
@override(BlockDev.lvm_get_thpool_padding)
def lvm_get_thpool_padding(size, pe_size=0, included=False):
    return _lvm_get_thpool_padding(size, pe_size, included)
__all__.append("lvm_get_thpool_padding")

_lvm_pvcreate = BlockDev.lvm_pvcreate
@override(BlockDev.lvm_pvcreate)
def lvm_pvcreate(device, dataalignment=0, metadatasize=0):
    return _lvm_pvcreate(device, dataalignment, metadatasize)
__all__.append("lvm_pvcreate")

_lvm_pvmove = BlockDev.lvm_pvmove
@override(BlockDev.lvm_pvmove)
def lvm_pvmove(src, dest=None):
    return _lvm_pvmove(src, dest)
__all__.append("lvm_pvmove")

_lvm_pvscan = BlockDev.lvm_pvscan
@override(BlockDev.lvm_pvscan)
def lvm_pvscan(device=None, update_cache=True):
    return _lvm_pvscan(device, update_cache)
__all__.append("lvm_pvscan")

_lvm_vgcreate = BlockDev.lvm_vgcreate
@override(BlockDev.lvm_vgcreate)
def lvm_vgcreate(name, pv_list, pe_size=0):
    return _lvm_vgcreate(name, pv_list, pe_size)
__all__.append("lvm_vgcreate")

_lvm_vgreduce = BlockDev.lvm_vgreduce
@override(BlockDev.lvm_vgreduce)
def lvm_vgreduce(vg_name, device=None):
    return _lvm_vgreduce(vg_name, device)
__all__.append("lvm_vgreduce")

_lvm_lvcreate = BlockDev.lvm_lvcreate
@override(BlockDev.lvm_lvcreate)
def lvm_lvcreate(vg_name, lv_name, size, pv_list=None):
    return _lvm_lvcreate(vg_name, lv_name, size, pv_list)
__all__.append("lvm_lvcreate")

_lvm_lvs = BlockDev.lvm_lvs
@override(BlockDev.lvm_lvs)
def lvm_lvs(vg_name=None):
    return _lvm_lvs(vg_name)
__all__.append("lvm_lvs")

_lvm_thpoolcreate = BlockDev.lvm_thpoolcreate
@override(BlockDev.lvm_thpoolcreate)
def lvm_thpoolcreate(vg_name, lv_name, size, md_size=0, chunk_size=0, profile=None):
    return _lvm_thpoolcreate(vg_name, lv_name, size, md_size, chunk_size, profile)
__all__.append("lvm_thpoolcreate")

_lvm_thsnapshotcreate = BlockDev.lvm_thsnapshotcreate
@override(BlockDev.lvm_thsnapshotcreate)
def lvm_thsnapshotcreate(vg_name, origin_name, snapshot_name, pool_name=None):
    return _lvm_thsnapshotcreate(vg_name, origin_name, snapshot_name, pool_name)
__all__.append("lvm_thsnapshotcreate")

_lvm_set_global_config = BlockDev.lvm_set_global_config
@override(BlockDev.lvm_set_global_config)
def lvm_set_global_config(new_config=None):
    return _lvm_set_global_config(new_config)
__all__.append("lvm_set_global_config")


_md_get_superblock_size = BlockDev.md_get_superblock_size
@override(BlockDev.md_get_superblock_size)
def md_get_superblock_size(size, version=None):
    return _md_get_superblock_size(size, version)
__all__.append("md_get_superblock_size")

_md_create = BlockDev.md_create
@override(BlockDev.md_create)
def md_create(device_name, level, disks, spares=0, version=None, bitmap=False):
    return _md_create(device_name, level, disks, spares, version, bitmap)
__all__.append("md_create")

_md_add = BlockDev.md_add
@override(BlockDev.md_add)
def md_add(raid_name, device, raid_devs=0):
    return _md_add(raid_name, device, raid_devs)
__all__.append("md_add")

_md_activate = BlockDev.md_activate
@override(BlockDev.md_activate)
def md_activate(device_name, members=None, uuid=None):
    return _md_activate(device_name, members, uuid)
__all__.append("md_activate")


_swap_mkswap = BlockDev.swap_mkswap
@override(BlockDev.swap_mkswap)
def swap_mkswap(device, label=None):
    return _swap_mkswap(device, label)
__all__.append("swap_mkswap")

_swap_swapon = BlockDev.swap_swapon
@override(BlockDev.swap_swapon)
def swap_swapon(device, priority=-1):
    return _swap_swapon(device, priority)
__all__.append("swap_swapon")
