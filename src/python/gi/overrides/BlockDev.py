"""
This code wraps the bindings automatically created by gobject-introspection.
They allow for creating more pythonic bindings where necessary.  For instance
this code allows many functions with default-value arguments to be called
without specifying values for such arguments.


A bit more special is the :class:`ErrorProxy` class defined in the second half
of this file. It enhances work with libblockdev in the area of error reporting
and exception handling. While native libblockdev functions only raise
GLib.GError instances via the GObject introspection it is desired to have the
exceptions more granular -- e.g. raise SwapError instances from swap-related
functions or even raise SwapErrorNoDev instances from swap-related functions if
the particular device passed as an argument doesn't exist etc. Also, it is
desired to be able to distinguish libblockdev errors/exceptions from other
GLib.GError errors/exceptions by having all libblockdev exception instances
inherited from a single class -- BlockDevError. That's what the
:class:`ErrorProxy` class and its instances (one for each libblockdev plugin)
implement. If for example ``BlockDev.swap.swapon("/dev/sda2")`` is used instead
of ``BlockDev.swap_swapon("/dev/sda2")`` (note the ``.`` instead of ``_``), then
if the function raises an error/exception, the exception is transformed into a
SwapError instance and thus can be caught by ``except BlockDev.SwapError`` or
even ``BlockDev.BlockDevError``. The ``BlockDev.swap`` object is an instance of
the :class:`ErrorProxy` class and makes sure the exception transformation
happens. It of course calls the ``swap_swapon`` function internally so there's
no code duplication and it propagates non-callable objects directly.

"""

import re
import os
from collections import namedtuple

from gi.importer import modules
from gi.overrides import override
from gi.repository import GLib

BlockDev = modules['BlockDev']._introspection_module
__all__ = []

bd_plugins = { "lvm": BlockDev.Plugin.LVM,
               "btrfs": BlockDev.Plugin.BTRFS,
               "crypto": BlockDev.Plugin.CRYPTO,
               "dm": BlockDev.Plugin.DM,
               "loop": BlockDev.Plugin.LOOP,
               "swap": BlockDev.Plugin.SWAP,
               "mdraid": BlockDev.Plugin.MDRAID,
               "mpath": BlockDev.Plugin.MPATH,
               "kbd": BlockDev.Plugin.KBD,
               "part": BlockDev.Plugin.PART,
               "fs": BlockDev.Plugin.FS,
               "s390": BlockDev.Plugin.S390,
               "nvdimm": BlockDev.Plugin.NVDIMM,
               "vdo": BlockDev.Plugin.VDO,
}


class ExtraArg(BlockDev.ExtraArg):
    def __new__(cls, opt, val=""):
        ret = BlockDev.ExtraArg.new(opt, val)
        ret.__class__ = cls
        return ret
ExtraArg = override(ExtraArg)
__all__.append("ExtraArg")

def _get_extra(extra, kwargs, cmd_extra=True):
    # pylint: disable=no-member
    # pylint doesn't really get how ExtraArg with overrides work
    if extra:
        if isinstance(extra, dict):
            ea = [ExtraArg.new(key, val) for key, val in extra.items()]
        elif isinstance(extra, list) and all(isinstance(arg, BlockDev.ExtraArg) for arg in extra):
            ea = extra
        else:
            msg = "extra arguments can only be given as a list of ExtraArg items or a as a dict"
            raise ValueError(msg)
    else:
        ea = []
    if cmd_extra:
        ea += [ExtraArg.new("--" + key, val) for key, val in kwargs.items()]
    else:
        ea += [ExtraArg.new(key, val) for key, val in kwargs.items()]
    if len(ea) == 0:
        return None
    return ea


_init = BlockDev.init
@override(BlockDev.init)
def init(require_plugins=None, log_func=None):
    return _init(require_plugins, log_func)
__all__.append("init")

_reinit = BlockDev.reinit
@override(BlockDev.reinit)
def reinit(require_plugins=None, reload=False, log_func=None):
    return _reinit(require_plugins, reload, log_func)
__all__.append("reinit")

_ensure_init = BlockDev.ensure_init
@override(BlockDev.ensure_init)
def ensure_init(require_plugins=None, log_func=None):
    return _ensure_init(require_plugins, log_func)
__all__.append("ensure_init")

_try_init = BlockDev.try_init
@override(BlockDev.try_init)
def try_init(require_plugins=None, log_func=None):
    return _try_init(require_plugins, log_func)
__all__.append("try_init")

_try_reinit = BlockDev.try_reinit
@override(BlockDev.try_reinit)
def try_reinit(require_plugins=None, reload=False, log_func=None):
    return _try_reinit(require_plugins, reload, log_func)
__all__.append("try_reinit")

_btrfs_create_volume = BlockDev.btrfs_create_volume
@override(BlockDev.btrfs_create_volume)
def btrfs_create_volume(devices, label=None, data_level=None, md_level=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_create_volume(devices, label, data_level, md_level, extra)
__all__.append("btrfs_create_volume")

_btrfs_add_device = BlockDev.btrfs_add_device
@override(BlockDev.btrfs_add_device)
def btrfs_add_device(mountpoint, device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_add_device(mountpoint, device, extra)
__all__.append("btrfs_add_device")

_btrfs_remove_device = BlockDev.btrfs_remove_device
@override(BlockDev.btrfs_remove_device)
def btrfs_remove_device(mountpoint, device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_remove_device(mountpoint, device, extra)
__all__.append("btrfs_remove_device")

_btrfs_create_subvolume = BlockDev.btrfs_create_subvolume
@override(BlockDev.btrfs_create_subvolume)
def btrfs_create_subvolume(mountpoint, name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_create_subvolume(mountpoint, name, extra)
__all__.append("btrfs_create_subvolume")

_btrfs_delete_subvolume = BlockDev.btrfs_delete_subvolume
@override(BlockDev.btrfs_delete_subvolume)
def btrfs_delete_subvolume(mountpoint, name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_delete_subvolume(mountpoint, name, extra)
__all__.append("btrfs_delete_subvolume")

_btrfs_set_default_subvolume = BlockDev.btrfs_set_default_subvolume
@override(BlockDev.btrfs_set_default_subvolume)
def btrfs_set_default_subvolume(mountpoint, subvol_id, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_set_default_subvolume(mountpoint, subvol_id, extra)
__all__.append("btrfs_set_default_subvolume")

_btrfs_list_subvolumes = BlockDev.btrfs_list_subvolumes
@override(BlockDev.btrfs_list_subvolumes)
def btrfs_list_subvolumes(mountpoint, snapshots_only=False):
    return _btrfs_list_subvolumes(mountpoint, snapshots_only)
__all__.append("btrfs_list_subvolumes")

_btrfs_create_snapshot = BlockDev.btrfs_create_snapshot
@override(BlockDev.btrfs_create_snapshot)
def btrfs_create_snapshot(source, dest, ro=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_create_snapshot(source, dest, ro, extra)
__all__.append("btrfs_create_snapshot")

_btrfs_mkfs = BlockDev.btrfs_mkfs
@override(BlockDev.btrfs_mkfs)
def btrfs_mkfs(devices, label=None, data_level=None, md_level=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_mkfs(devices, label, data_level, md_level, extra)
__all__.append("btrfs_mkfs")

_btrfs_resize = BlockDev.btrfs_resize
@override(BlockDev.btrfs_resize)
def btrfs_resize(mountpoint, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_resize(mountpoint, size, extra)
__all__.append("btrfs_resize")

_btrfs_check = BlockDev.btrfs_check
@override(BlockDev.btrfs_check)
def btrfs_check(mountpoint, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _btrfs_check(mountpoint, extra)
__all__.append("btrfs_check")


class CryptoLUKSPBKDF(BlockDev.CryptoLUKSPBKDF):
    def __new__(cls, type=None, hash=None, max_memory_kb=0, iterations=0, time_ms=0, parallel_threads=0):  # pylint: disable=redefined-builtin
        ret = BlockDev.CryptoLUKSPBKDF.new(type, hash, max_memory_kb, iterations, time_ms, parallel_threads)
        ret.__class__ = cls
        return ret
    def __init__(self, *args, **kwargs):  # pylint: disable=unused-argument
        super(CryptoLUKSPBKDF, self).__init__()  #pylint: disable=bad-super-call
CryptoLUKSPBKDF = override(CryptoLUKSPBKDF)
__all__.append("CryptoLUKSPBKDF")

class CryptoLUKSExtra(BlockDev.CryptoLUKSExtra):
    def __new__(cls, data_alignment=0, data_device=None, integrity=None, sector_size=0, label=None, subsystem=None, pbkdf=None):
        if pbkdf is None:
            pbkdf = CryptoLUKSPBKDF()
        ret = BlockDev.CryptoLUKSExtra.new(data_alignment, data_device, integrity, sector_size, label, subsystem, pbkdf)
        ret.__class__ = cls
        return ret
    def __init__(self, *args, **kwargs):   # pylint: disable=unused-argument
        super(CryptoLUKSExtra, self).__init__()  #pylint: disable=bad-super-call
CryptoLUKSExtra = override(CryptoLUKSExtra)
__all__.append("CryptoLUKSExtra")

# calling `crypto_luks_format_luks2` with `luks_version` set to
# `BlockDev.CryptoLUKSVersion.LUKS1` and `extra` to `None` is the same
# as using the "original" function `crypto_luks_format`
_crypto_luks_format = BlockDev.crypto_luks_format_luks2
@override(BlockDev.crypto_luks_format)
def crypto_luks_format(device, cipher=None, key_size=0, passphrase=None, key_file=None, min_entropy=0, luks_version=BlockDev.CryptoLUKSVersion.LUKS1, extra=None):
    return _crypto_luks_format(device, cipher, key_size, passphrase, key_file, min_entropy, luks_version, extra)
__all__.append("crypto_luks_format")

_crypto_luks_open = BlockDev.crypto_luks_open
@override(BlockDev.crypto_luks_open)
def crypto_luks_open(device, name, passphrase=None, key_file=None, read_only=False):
    return _crypto_luks_open(device, name, passphrase, key_file, read_only)
__all__.append("crypto_luks_open")

_crypto_luks_resize = BlockDev.crypto_luks_resize_luks2
@override(BlockDev.crypto_luks_resize)
def crypto_luks_resize(luks_device, size=0, passphrase=None, key_file=None):
    return _crypto_luks_resize(luks_device, size, passphrase, key_file)
__all__.append("crypto_luks_resize")

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

_crypto_luks_resume = BlockDev.crypto_luks_resume
@override(BlockDev.crypto_luks_resume)
def crypto_luks_resume(device, passphrase=None, key_file=None):
    return _crypto_luks_resume(device, passphrase, key_file)
__all__.append("crypto_luks_resume")

_crypto_tc_open = BlockDev.crypto_tc_open_full
@override(BlockDev.crypto_tc_open)
def crypto_tc_open(device, name, passphrase, read_only=False, keyfiles=None, hidden=False, system=False, veracrypt=False, veracrypt_pim=0):
    if isinstance(passphrase, str):
        passphrase = [ord(c) for c in passphrase]
    return _crypto_tc_open(device, name, passphrase, keyfiles, hidden, system, veracrypt, veracrypt_pim, read_only)
__all__.append("crypto_tc_open")

_crypto_bitlk_open = BlockDev.crypto_bitlk_open
@override(BlockDev.crypto_bitlk_open)
def crypto_bitlk_open(device, name, passphrase, read_only=False):
    if isinstance(passphrase, str):
        passphrase = [ord(c) for c in passphrase]
    return _crypto_bitlk_open(device, name, passphrase, read_only)
__all__.append("crypto_bitlk_open")


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


_loop_setup = BlockDev.loop_setup
@override(BlockDev.loop_setup)
def loop_setup(file, offset=0, size=0, read_only=False, part_scan=True):
    return _loop_setup(file, offset, size, read_only, part_scan)
__all__.append("loop_setup")


_fs_unmount = BlockDev.fs_unmount
@override(BlockDev.fs_unmount)
def fs_unmount(spec, lazy=False, force=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs, False)
    return _fs_unmount(spec, lazy, force, extra)
__all__.append("fs_unmount")

_fs_mount = BlockDev.fs_mount
@override(BlockDev.fs_mount)
def fs_mount(device=None, mountpoint=None, fstype=None, options=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs, False)
    return _fs_mount(device, mountpoint, fstype, options, extra)
__all__.append("fs_mount")

_fs_ext2_mkfs = BlockDev.fs_ext2_mkfs
@override(BlockDev.fs_ext2_mkfs)
def fs_ext2_mkfs(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext2_mkfs(device, extra)
__all__.append("fs_ext2_mkfs")

_fs_ext3_mkfs = BlockDev.fs_ext3_mkfs
@override(BlockDev.fs_ext3_mkfs)
def fs_ext3_mkfs(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext3_mkfs(device, extra)
__all__.append("fs_ext3_mkfs")

_fs_ext4_mkfs = BlockDev.fs_ext4_mkfs
@override(BlockDev.fs_ext4_mkfs)
def fs_ext4_mkfs(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext4_mkfs(device, extra)
__all__.append("fs_ext4_mkfs")

_fs_ext2_check = BlockDev.fs_ext2_check
@override(BlockDev.fs_ext2_check)
def fs_ext2_check(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext2_check(device, extra)
__all__.append("fs_ext2_check")

_fs_ext3_check = BlockDev.fs_ext3_check
@override(BlockDev.fs_ext3_check)
def fs_ext3_check(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext3_check(device, extra)
__all__.append("fs_ext3_check")

_fs_ext4_check = BlockDev.fs_ext4_check
@override(BlockDev.fs_ext4_check)
def fs_ext4_check(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext4_check(device, extra)
__all__.append("fs_ext4_check")

_fs_ext2_repair = BlockDev.fs_ext2_repair
@override(BlockDev.fs_ext2_repair)
def fs_ext2_repair(device, unsafe=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext2_repair(device, unsafe, extra)
__all__.append("fs_ext2_repair")

_fs_ext3_repair = BlockDev.fs_ext3_repair
@override(BlockDev.fs_ext3_repair)
def fs_ext3_repair(device, unsafe=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext3_repair(device, unsafe, extra)
__all__.append("fs_ext3_repair")

_fs_ext4_repair = BlockDev.fs_ext4_repair
@override(BlockDev.fs_ext4_repair)
def fs_ext4_repair(device, unsafe=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext4_repair(device, unsafe, extra)
__all__.append("fs_ext4_repair")

_fs_ext2_resize = BlockDev.fs_ext2_resize
@override(BlockDev.fs_ext2_resize)
def fs_ext2_resize(device, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext2_resize(device, size, extra)
__all__.append("fs_ext2_resize")

_fs_ext3_resize = BlockDev.fs_ext3_resize
@override(BlockDev.fs_ext3_resize)
def fs_ext3_resize(device, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext3_resize(device, size, extra)
__all__.append("fs_ext3_resize")

_fs_ext4_resize = BlockDev.fs_ext4_resize
@override(BlockDev.fs_ext4_resize)
def fs_ext4_resize(device, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_ext4_resize(device, size, extra)
__all__.append("fs_ext4_resize")

_fs_xfs_mkfs = BlockDev.fs_xfs_mkfs
@override(BlockDev.fs_xfs_mkfs)
def fs_xfs_mkfs(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_xfs_mkfs(device, extra)
__all__.append("fs_xfs_mkfs")

_fs_xfs_repair = BlockDev.fs_xfs_repair
@override(BlockDev.fs_xfs_repair)
def fs_xfs_repair(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_xfs_repair(device, extra)
__all__.append("fs_xfs_repair")

_fs_xfs_resize = BlockDev.fs_xfs_resize
@override(BlockDev.fs_xfs_resize)
def fs_xfs_resize(device, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_xfs_resize(device, size, extra)
__all__.append("fs_xfs_resize")

_fs_vfat_mkfs = BlockDev.fs_vfat_mkfs
@override(BlockDev.fs_vfat_mkfs)
def fs_vfat_mkfs(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_vfat_mkfs(device, extra)
__all__.append("fs_vfat_mkfs")

_fs_vfat_check = BlockDev.fs_vfat_check
@override(BlockDev.fs_vfat_check)
def fs_vfat_check(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_vfat_check(device, extra)
__all__.append("fs_vfat_check")

_fs_vfat_repair = BlockDev.fs_vfat_repair
@override(BlockDev.fs_vfat_repair)
def fs_vfat_repair(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _fs_vfat_repair(device, extra)
__all__.append("fs_vfat_repair")


try:
    _kbd_bcache_create = BlockDev.kbd_bcache_create
    @override(BlockDev.kbd_bcache_create)
    def kbd_bcache_create(backing_device, cache_device, extra=None, **kwargs):
        extra = _get_extra(extra, kwargs)
        return _kbd_bcache_create(backing_device, cache_device, extra)
    __all__.append("kbd_bcache_create")
except AttributeError:
    # the bcache support may not be available
    # TODO: do this more generically
    pass


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

_lvm_get_thpool_meta_size = BlockDev.lvm_get_thpool_meta_size
@override(BlockDev.lvm_get_thpool_meta_size)
def lvm_get_thpool_meta_size(size, chunk_size=0, n_snapshots=0):
    return _lvm_get_thpool_meta_size(size, chunk_size, n_snapshots)
__all__.append("lvm_get_thpool_meta_size")

_lvm_pvcreate = BlockDev.lvm_pvcreate
@override(BlockDev.lvm_pvcreate)
def lvm_pvcreate(device, data_alignment=0, metadata_size=0, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_pvcreate(device, data_alignment, metadata_size, extra)
__all__.append("lvm_pvcreate")

_lvm_pvresize = BlockDev.lvm_pvresize
@override(BlockDev.lvm_pvresize)
def lvm_pvresize(device, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_pvresize(device, size, extra)
__all__.append("lvm_pvresize")

_lvm_pvremove = BlockDev.lvm_pvremove
@override(BlockDev.lvm_pvremove)
def lvm_pvremove(device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_pvremove(device, extra)
__all__.append("lvm_pvremove")

_lvm_pvmove = BlockDev.lvm_pvmove
@override(BlockDev.lvm_pvmove)
def lvm_pvmove(src, dest=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_pvmove(src, dest, extra)
__all__.append("lvm_pvmove")

_lvm_pvscan = BlockDev.lvm_pvscan
@override(BlockDev.lvm_pvscan)
def lvm_pvscan(device=None, update_cache=True, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_pvscan(device, update_cache, extra)
__all__.append("lvm_pvscan")

_lvm_vgcreate = BlockDev.lvm_vgcreate
@override(BlockDev.lvm_vgcreate)
def lvm_vgcreate(name, pv_list, pe_size=0, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgcreate(name, pv_list, pe_size, extra)
__all__.append("lvm_vgcreate")

_lvm_vgremove = BlockDev.lvm_vgremove
@override(BlockDev.lvm_vgremove)
def lvm_vgremove(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgremove(name, extra)
__all__.append("lvm_vgremove")

_lvm_vgrename = BlockDev.lvm_vgrename
@override(BlockDev.lvm_vgrename)
def lvm_vgrename(old_name, new_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgrename(old_name, new_name, extra)
__all__.append("lvm_vgrename")

_lvm_vgactivate = BlockDev.lvm_vgactivate
@override(BlockDev.lvm_vgactivate)
def lvm_vgactivate(vg_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgactivate(vg_name, extra)
__all__.append("lvm_vgactivate")

_lvm_vgdeactivate = BlockDev.lvm_vgdeactivate
@override(BlockDev.lvm_vgdeactivate)
def lvm_vgdeactivate(vg_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgdeactivate(vg_name, extra)
__all__.append("lvm_vgdeactivate")

_lvm_vgreduce = BlockDev.lvm_vgreduce
@override(BlockDev.lvm_vgreduce)
def lvm_vgreduce(vg_name, device=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgreduce(vg_name, device, extra)
__all__.append("lvm_vgreduce")

_lvm_vgextend = BlockDev.lvm_vgextend
@override(BlockDev.lvm_vgextend)
def lvm_vgextend(vg_name, device, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vgextend(vg_name, device, extra)
__all__.append("lvm_vgextend")

_lvm_lvcreate = BlockDev.lvm_lvcreate
@override(BlockDev.lvm_lvcreate)
def lvm_lvcreate(vg_name, lv_name, size, type=None, pv_list=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvcreate(vg_name, lv_name, size, type, pv_list, extra)
__all__.append("lvm_lvcreate")

_lvm_lvremove = BlockDev.lvm_lvremove
@override(BlockDev.lvm_lvremove)
def lvm_lvremove(vg_name, lv_name, force=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvremove(vg_name, lv_name, force, extra)
__all__.append("lvm_lvremove")

_lvm_lvrename = BlockDev.lvm_lvrename
@override(BlockDev.lvm_lvrename)
def lvm_lvrename(vg_name, lv_name, new_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvrename(vg_name, lv_name, new_name, extra)
__all__.append("lvm_lvrename")

_lvm_lvresize = BlockDev.lvm_lvresize
@override(BlockDev.lvm_lvresize)
def lvm_lvresize(vg_name, lv_name, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvresize(vg_name, lv_name, size, extra)
__all__.append("lvm_lvresize")

_lvm_lvactivate = BlockDev.lvm_lvactivate
@override(BlockDev.lvm_lvactivate)
def lvm_lvactivate(vg_name, lv_name, ignore_skip=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvactivate(vg_name, lv_name, ignore_skip, extra)
__all__.append("lvm_lvactivate")

_lvm_lvdeactivate = BlockDev.lvm_lvdeactivate
@override(BlockDev.lvm_lvdeactivate)
def lvm_lvdeactivate(vg_name, lv_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvdeactivate(vg_name, lv_name, extra)
__all__.append("lvm_lvdeactivate")

_lvm_lvsnapshotcreate = BlockDev.lvm_lvsnapshotcreate
@override(BlockDev.lvm_lvsnapshotcreate)
def lvm_lvsnapshotcreate(vg_name, origin_name, snapshot_name, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvsnapshotcreate(vg_name, origin_name, snapshot_name, size, extra)
__all__.append("lvm_lvsnapshotcreate")

_lvm_lvsnapshotmerge = BlockDev.lvm_lvsnapshotmerge
@override(BlockDev.lvm_lvsnapshotmerge)
def lvm_lvsnapshotmerge(vg_name, snapshot_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_lvsnapshotmerge(vg_name, snapshot_name, extra)
__all__.append("lvm_lvsnapshotmerge")

_lvm_lvs = BlockDev.lvm_lvs
@override(BlockDev.lvm_lvs)
def lvm_lvs(vg_name=None):
    return _lvm_lvs(vg_name)
__all__.append("lvm_lvs")

_lvm_thpoolcreate = BlockDev.lvm_thpoolcreate
@override(BlockDev.lvm_thpoolcreate)
def lvm_thpoolcreate(vg_name, lv_name, size, md_size=0, chunk_size=0, profile=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_thpoolcreate(vg_name, lv_name, size, md_size, chunk_size, profile, extra)
__all__.append("lvm_thpoolcreate")

_lvm_thsnapshotcreate = BlockDev.lvm_thsnapshotcreate
@override(BlockDev.lvm_thsnapshotcreate)
def lvm_thsnapshotcreate(vg_name, origin_name, snapshot_name, pool_name=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_thsnapshotcreate(vg_name, origin_name, snapshot_name, pool_name, extra)
__all__.append("lvm_thsnapshotcreate")

_lvm_cache_attach = BlockDev.lvm_cache_attach
@override(BlockDev.lvm_cache_attach)
def lvm_cache_attach(vg_name, data_lv, cache_pool_lv, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_cache_attach(vg_name, data_lv, cache_pool_lv, extra)
__all__.append("lvm_cache_attach")

_lvm_cache_detach = BlockDev.lvm_cache_detach
@override(BlockDev.lvm_cache_detach)
def lvm_cache_detach(vg_name, cached_lv, destroy=True, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_cache_detach(vg_name, cached_lv, destroy, extra)
__all__.append("lvm_cache_detach")

_lvm_is_valid_thpool_chunk_size = BlockDev.lvm_is_valid_thpool_chunk_size
@override(BlockDev.lvm_is_valid_thpool_chunk_size)
def lvm_is_valid_thpool_chunk_size(size, discard=False):
    return _lvm_is_valid_thpool_chunk_size(size, discard)
__all__.append("lvm_is_valid_thpool_chunk_size")

_lvm_set_global_config = BlockDev.lvm_set_global_config
@override(BlockDev.lvm_set_global_config)
def lvm_set_global_config(new_config=None):
    return _lvm_set_global_config(new_config)
__all__.append("lvm_set_global_config")

_lvm_thpool_convert = BlockDev.lvm_thpool_convert
@override(BlockDev.lvm_thpool_convert)
def lvm_thpool_convert(vg_name, data_lv, metadata_lv, name=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_thpool_convert(vg_name, data_lv, metadata_lv, name, extra)
__all__.append("lvm_thpool_convert")

_lvm_cache_pool_convert = BlockDev.lvm_cache_pool_convert
@override(BlockDev.lvm_cache_pool_convert)
def lvm_cache_pool_convert(vg_name, data_lv, metadata_lv, name=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_cache_pool_convert(vg_name, data_lv, metadata_lv, name, extra)
__all__.append("lvm_cache_pool_convert")

_lvm_vdo_pool_create = BlockDev.lvm_vdo_pool_create
@override(BlockDev.lvm_vdo_pool_create)
def lvm_vdo_pool_create(vg_name, lv_name, pool_name, data_size, virtual_size, index_memory=0, compression=True, deduplication=True, write_policy=BlockDev.LVMVDOWritePolicy.AUTO, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_pool_create(vg_name, lv_name, pool_name, data_size,virtual_size, index_memory, compression, deduplication, write_policy, extra)
__all__.append("lvm_vdo_pool_create")

_lvm_vdo_resize = BlockDev.lvm_vdo_resize
@override(BlockDev.lvm_vdo_resize)
def lvm_vdo_resize(vg_name, lv_name, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_resize(vg_name, lv_name, size, extra)
__all__.append("lvm_vdo_resize")

_lvm_vdo_pool_resize = BlockDev.lvm_vdo_pool_resize
@override(BlockDev.lvm_vdo_pool_resize)
def lvm_vdo_pool_resize(vg_name, lv_name, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_pool_resize(vg_name, lv_name, size, extra)
__all__.append("lvm_vdo_pool_resize")

_lvm_vdo_enable_compression = BlockDev.lvm_vdo_enable_compression
@override(BlockDev.lvm_vdo_enable_compression)
def lvm_vdo_enable_compression(vg_name, pool_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_enable_compression(vg_name, pool_name, extra)
__all__.append("lvm_vdo_enable_compression")

_lvm_vdo_disable_compression = BlockDev.lvm_vdo_disable_compression
@override(BlockDev.lvm_vdo_disable_compression)
def lvm_vdo_disable_compression(vg_name, pool_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_disable_compression(vg_name, pool_name, extra)
__all__.append("lvm_vdo_disable_compression")

_lvm_vdo_enable_deduplication = BlockDev.lvm_vdo_enable_deduplication
@override(BlockDev.lvm_vdo_enable_deduplication)
def lvm_vdo_enable_deduplication(vg_name, pool_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_enable_deduplication(vg_name, pool_name, extra)
__all__.append("lvm_vdo_enable_deduplication")

_lvm_vdo_disable_deduplication = BlockDev.lvm_vdo_disable_deduplication
@override(BlockDev.lvm_vdo_disable_deduplication)
def lvm_vdo_disable_deduplication(vg_name, pool_name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_disable_deduplication(vg_name, pool_name, extra)
__all__.append("lvm_vdo_disable_deduplication")

_lvm_vdo_pool_convert = BlockDev.lvm_vdo_pool_convert
@override(BlockDev.lvm_vdo_pool_convert)
def lvm_vdo_pool_convert(vg_name, lv_name, pool_name, virtual_size, index_memory=0, compression=True, deduplication=True, write_policy=BlockDev.LVMVDOWritePolicy.AUTO, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _lvm_vdo_pool_convert(vg_name, lv_name, pool_name, virtual_size, index_memory, compression, deduplication, write_policy, extra)
__all__.append("lvm_vdo_pool_convert")

_md_get_superblock_size = BlockDev.md_get_superblock_size
@override(BlockDev.md_get_superblock_size)
def md_get_superblock_size(size, version=None):
    return _md_get_superblock_size(size, version)
__all__.append("md_get_superblock_size")

_md_create = BlockDev.md_create
@override(BlockDev.md_create)
def md_create(device_name, level, disks, spares=0, version=None, bitmap=False, chunk_size=0, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _md_create(device_name, level, disks, spares, version, bitmap, chunk_size, extra)
__all__.append("md_create")

_md_add = BlockDev.md_add
@override(BlockDev.md_add)
def md_add(raid_spec, device, raid_devs=0, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _md_add(raid_spec, device, raid_devs, extra)
__all__.append("md_add")

_md_remove = BlockDev.md_remove
@override(BlockDev.md_remove)
def md_remove(raid_spec, device, fail, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _md_remove(raid_spec, device, fail, extra)
__all__.append("md_remove")

_md_activate = BlockDev.md_activate
@override(BlockDev.md_activate)
def md_activate(raid_spec=None, members=None, uuid=None, start_degraded=True, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _md_activate(raid_spec, members, uuid, start_degraded, extra)
__all__.append("md_activate")


if os.uname()[4].startswith('s390'):
    _s390_dasd_format = BlockDev.s390_dasd_format
    @override(BlockDev.s390_dasd_format)
    def s390_dasd_format(dasd, extra=None, **kwargs):
        extra = _get_extra(extra, kwargs)
        return _s390_dasd_format(dasd, extra)
    __all__.append("s390_dasd_format")


_swap_mkswap = BlockDev.swap_mkswap
@override(BlockDev.swap_mkswap)
def swap_mkswap(device, label=None, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _swap_mkswap(device, label, extra)
__all__.append("swap_mkswap")

_swap_swapon = BlockDev.swap_swapon
@override(BlockDev.swap_swapon)
def swap_swapon(device, priority=-1):
    return _swap_swapon(device, priority)
__all__.append("swap_swapon")


_kbd_zram_create_devices = BlockDev.kbd_zram_create_devices
@override(BlockDev.kbd_zram_create_devices)
def kbd_zram_create_devices(num_devices, sizes, nstreams=None):
    return _kbd_zram_create_devices(num_devices, sizes, nstreams)
__all__.append("kbd_zram_create_devices")

_kbd_zram_add_device = BlockDev.kbd_zram_add_device
@override(BlockDev.kbd_zram_add_device)
def kbd_zram_add_device(size, nstreams=0):
    return _kbd_zram_add_device(size, nstreams)
__all__.append("kbd_zram_add_device")


_part_create_table = BlockDev.part_create_table
@override(BlockDev.part_create_table)
def part_create_table(disk, type, ignore_existing=True):
    return _part_create_table(disk, type, ignore_existing)
__all__.append("part_create_table")


_nvdimm_namespace_reconfigure = BlockDev.nvdimm_namespace_reconfigure
@override(BlockDev.nvdimm_namespace_reconfigure)
def nvdimm_namespace_reconfigure(namespace, mode, force=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _nvdimm_namespace_reconfigure(namespace, mode, force, extra)
__all__.append("nvdimm_namespace_reconfigure")

_nvdimm_namespace_info = BlockDev.nvdimm_namespace_info
@override(BlockDev.nvdimm_namespace_info)
def nvdimm_namespace_info(namespace, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _nvdimm_namespace_info(namespace, extra)
__all__.append("nvdimm_namespace_info")

_nvdimm_list_namespaces = BlockDev.nvdimm_list_namespaces
@override(BlockDev.nvdimm_list_namespaces)
def nvdimm_list_namespaces(bus=None, region=None, idle=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _nvdimm_list_namespaces(bus, region, idle, extra)
__all__.append("nvdimm_list_namespaces")

_nvdimm_namespace_enable = BlockDev.nvdimm_namespace_enable
@override(BlockDev.nvdimm_namespace_enable)
def nvdimm_namespace_enable(namespace, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _nvdimm_namespace_enable(namespace, extra)
__all__.append("nvdimm_namespace_enable")

_nvdimm_namespace_disable = BlockDev.nvdimm_namespace_disable
@override(BlockDev.nvdimm_namespace_disable)
def nvdimm_namespace_disable(namespace, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _nvdimm_namespace_disable(namespace, extra)
__all__.append("nvdimm_namespace_disable")


_vdo_create = BlockDev.vdo_create
@override(BlockDev.vdo_create)
def vdo_create(name, backing_device, logical_size=0, index_memory=0, compression=True, deduplication=True, write_policy=BlockDev.VDOWritePolicy.AUTO, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_create(name, backing_device, logical_size, index_memory, compression, deduplication, write_policy, extra)
__all__.append("vdo_create")

_vdo_remove = BlockDev.vdo_remove
@override(BlockDev.vdo_remove)
def vdo_remove(name, force=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_remove(name, force, extra)
__all__.append("vdo_remove")

_vdo_change_write_policy = BlockDev.vdo_change_write_policy
@override(BlockDev.vdo_change_write_policy)
def vdo_change_write_policy(name, write_policy, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_change_write_policy(name, write_policy, extra)
__all__.append("vdo_change_write_policy")

_vdo_enable_compression = BlockDev.vdo_enable_compression
@override(BlockDev.vdo_enable_compression)
def vdo_enable_compression(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_enable_compression(name, extra)
__all__.append("vdo_enable_compression")

_vdo_disable_compression = BlockDev.vdo_disable_compression
@override(BlockDev.vdo_disable_compression)
def vdo_disable_compression(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_disable_compression(name, extra)
__all__.append("vdo_disable_compression")

_vdo_enable_deduplication = BlockDev.vdo_enable_deduplication
@override(BlockDev.vdo_enable_deduplication)
def vdo_enable_deduplication(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_enable_deduplication(name, extra)
__all__.append("vdo_enable_deduplication")

_vdo_disable_deduplication = BlockDev.vdo_disable_deduplication
@override(BlockDev.vdo_disable_deduplication)
def vdo_disable_deduplication(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_disable_deduplication(name, extra)
__all__.append("vdo_disable_deduplication")

_vdo_activate = BlockDev.vdo_activate
@override(BlockDev.vdo_activate)
def vdo_activate(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_activate(name, extra)
__all__.append("vdo_activate")

_vdo_deactivate = BlockDev.vdo_deactivate
@override(BlockDev.vdo_deactivate)
def vdo_deactivate(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_deactivate(name, extra)
__all__.append("vdo_deactivate")

_vdo_start = BlockDev.vdo_start
@override(BlockDev.vdo_start)
def vdo_start(name, rebuild=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_start(name, rebuild, extra)
__all__.append("vdo_start")

_vdo_stop = BlockDev.vdo_stop
@override(BlockDev.vdo_stop)
def vdo_stop(name, force=False, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_stop(name, force, extra)
__all__.append("vdo_stop")

_vdo_grow_logical = BlockDev.vdo_grow_logical
@override(BlockDev.vdo_grow_logical)
def vdo_grow_logical(name, size, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_grow_logical(name, size, extra)
__all__.append("vdo_grow_logical")

_vdo_grow_physical = BlockDev.vdo_grow_physical
@override(BlockDev.vdo_grow_physical)
def vdo_grow_physical(name, extra=None, **kwargs):
    extra = _get_extra(extra, kwargs)
    return _vdo_grow_physical(name, extra)
__all__.append("vdo_grow_physical")


## defined in this overrides only!
def plugin_specs_from_names(plugin_names):
    ret = []
    for name in plugin_names:
        plugin = BlockDev.PluginSpec()
        plugin.name = bd_plugins[name.lower()]
        plugin.so_name = None
        ret.append(plugin)

    return ret
__all__.append("plugin_specs_from_names")


XRule = namedtuple("XRule", ["orig_exc", "regexp", "code", "new_exc"])
# XXX: how to document namedtuple fields?
"""
:field orig_exc: exception class to be transformed
:field regexp: regexp that needs to match exception msg for this rule to be
               applicable or None if no match is required
:field code: code of the exception, if defined (e.g. for GLib.Error)
:field new_exc: exception class to transform the :field:`orig_exc` into

"""

class ErrorProxy(object):
    """
    Class that defines a proxy that can be used to transform errors/exceptions
    reported/raised by functions into different exception instances of given
    class(es).

    """

    def __init__(self, prefix, mod, tr_excs, xrules=None, use_local=True):
        """Constructor for the :class:`ErrorProxy` class.

        :param str prefix: prefix of the proxied set of functions
        :param mod: module that provides the original functions
        :type mod: module
        :param tr_excs: list of pairs of exception classes that should be transformed (the first item into the second one)
        :type tr_excs: list of 2-tuples of exception classes
        :param xrules: transformation rules for exception transformations which
                       take precedence over the :param:`orig_excs` ->
                       :param:`new_excs` mapping (see below for details)
        :type xrules: set of XRule instances
        :param bool use_local: if original functions should be first searched in
                               the local scope (the current module) or not

        The :param:`tr_excs` parameter specifies the basic transformations. If
        an instance of an error/exception contained in the list as first item of
        some tuple is raised, it is transformed in an instance of the second
        item of the same tuple. For example::

          tr_excs = [(GLib.Error, SwapError), (OverflowError, ValueError)]

        would result in every GLib.GError instance raised from any of the
        functions being transformed into an instance of a SwapError class and
        similarly for the (OverflowError, ValueError) pair.

        If the :param:`xrules` parameter is specified, it takes precedence over
        the :param:`tr_excs` list above in the following way -- if raised
        exception/error is of type equal to :field:`orig_exc` field of any of
        the :param:`xrules` items then the transformation rule defined by the
        mapped XRule instance is used unless the :field:`regexp` is not None and
        doesn't match exception's/error's :attribute:`msg` attribute.

        """

        self._prefix = prefix
        self._mod = mod
        self._tr_excs = tr_excs
        if xrules:
            self._xrules = {xrule.orig_exc: xrule for xrule in xrules}
        else:
            self._xrules = dict()
        self._use_local = use_local
        self._wrapped_cache = dict()

    def __dir__(self):
        """Let's make TAB-TAB in ipython work!"""

        if self._use_local:
            items = set(dir(self._mod)) | set(locals().keys())
        else:
            items = set(dir(self._mod))

        prefix_len = len(self._prefix) + 1 # prefix + "_"
        return [item[prefix_len:] for item in items if item.startswith(self._prefix)]

    def __getattr__(self, attr):
        if self._use_local and (self._prefix + "_" + attr) in globals():
            orig_obj = globals()[self._prefix + "_" + attr]
        else:
            orig_obj = getattr(self._mod, self._prefix + "_" + attr)

        if not callable(orig_obj):
            # not a callable, just return the original object
            return orig_obj

        if attr in self._wrapped_cache:
            return self._wrapped_cache[attr]

        def wrapped(*args, **kwargs):
            try:
                ret = orig_obj(*args, **kwargs)
            # pylint: disable=catching-non-exception
            except tuple(tr_t[0] for tr_t in self._tr_excs) as e:
                if hasattr(e, "msg"):
                    msg = e.msg # pylint: disable=no-member
                elif hasattr(e, "message"):
                    msg = e.message # pylint: disable=no-member
                else:
                    msg = str(e)

                e_type = type(e)
                if e_type in self._xrules:
                    matches = True
                    if self._xrules[e_type].code and self._xrules[e_type].code != getattr(e, "code"):
                        matches = False
                    if matches and self._xrules[e_type].regexp and not self._xrules[e_type].regexp.match(msg):
                        matches = False
                    if matches:
                        raise self._xrules[e_type].new_exc(msg)

                # try to find exact type match
                transform = next((tr_t for tr_t in self._tr_excs if self._tr_excs == e_type), None)
                if not transform:
                    # no exact match, but we still caught the exception -> must be some child class
                    transform = next(tr_t for tr_t in self._tr_excs if isinstance(e, tr_t[0]))

                raise transform[1](msg)

            return ret

        self._wrapped_cache[attr] = wrapped
        return wrapped

class BlockDevError(Exception):
    pass
__all__.append("BlockDevError")

class BtrfsError(BlockDevError):
    pass
__all__.append("BtrfsError")

class CryptoError(BlockDevError):
    pass
__all__.append("CryptoError")

class DMError(BlockDevError):
    pass
__all__.append("DMError")

class LoopError(BlockDevError):
    pass
__all__.append("LoopError")

class LVMError(BlockDevError):
    pass
__all__.append("LVMError")

class MDRaidError(BlockDevError):
    pass
__all__.append("MDRaidError")

class MpathError(BlockDevError):
    pass
__all__.append("MpathError")

class SwapError(BlockDevError):
    pass
class SwapActivateError(SwapError):
    pass
class SwapOldError(SwapActivateError):
    pass
class SwapSuspendError(SwapActivateError):
    pass
class SwapUnknownError(SwapActivateError):
    pass
class SwapPagesizeError(SwapActivateError):
    pass
__all__.extend(("SwapError", "SwapActivateError", "SwapOldError", "SwapSuspendError", "SwapUnknownError", "SwapPagesizeError"))

class KbdError(BlockDevError):
    pass
__all__.append("KbdError")

class PartError(BlockDevError):
    pass
__all__.append("PartError")

class FSError(BlockDevError):
    pass
class FSNoFSError(FSError):
    pass
__all__.extend(("FSError", "FSNoFSError"))

class S390Error(BlockDevError):
    pass
__all__.append("S390Error")

class UtilsError(BlockDevError):
    pass
__all__.append("UtilsError")

class NVDIMMError(BlockDevError):
    pass
__all__.append("NVDIMMError")

class VDOError(BlockDevError):
    pass
__all__.append("VDOError")

class BlockDevNotImplementedError(NotImplementedError, BlockDevError):
    pass
__all__.append("BlockDevNotImplementedError")

not_implemented_rule = XRule(GLib.Error, re.compile(r".*The function '.*' called, but not implemented!"), None, BlockDevNotImplementedError)

fs_nofs_rule = XRule(GLib.Error, None, 3, FSNoFSError)
swap_activate_rule = XRule(GLib.Error, None, 1, SwapActivateError)
swap_old_rule = XRule(GLib.Error, None, 3, SwapOldError)
swap_suspend_rule = XRule(GLib.Error, None, 4, SwapSuspendError)
swap_unknown_rule = XRule(GLib.Error, None, 5, SwapUnknownError)
swap_pagesize_rule = XRule(GLib.Error, None, 6, SwapPagesizeError)

btrfs = ErrorProxy("btrfs", BlockDev, [(GLib.Error, BtrfsError)], [not_implemented_rule])
__all__.append("btrfs")

crypto = ErrorProxy("crypto", BlockDev, [(GLib.Error, CryptoError)], [not_implemented_rule])
__all__.append("crypto")

dm = ErrorProxy("dm", BlockDev, [(GLib.Error, DMError)], [not_implemented_rule])
__all__.append("dm")

loop = ErrorProxy("loop", BlockDev, [(GLib.Error, LoopError)], [not_implemented_rule])
__all__.append("loop")

lvm = ErrorProxy("lvm", BlockDev, [(GLib.Error, LVMError)], [not_implemented_rule])
__all__.append("lvm")

md = ErrorProxy("md", BlockDev, [(GLib.Error, MDRaidError)], [not_implemented_rule])
__all__.append("md")

mpath = ErrorProxy("mpath", BlockDev, [(GLib.Error, MpathError)], [not_implemented_rule])
__all__.append("mpath")

swap = ErrorProxy("swap", BlockDev, [(GLib.Error, SwapError)], [not_implemented_rule, swap_activate_rule, swap_old_rule, swap_suspend_rule, swap_unknown_rule, swap_pagesize_rule])
__all__.append("swap")

kbd = ErrorProxy("kbd", BlockDev, [(GLib.Error, KbdError)], [not_implemented_rule])
__all__.append("kbd")

part = ErrorProxy("part", BlockDev, [(GLib.Error, PartError)], [not_implemented_rule])
__all__.append("part")

fs = ErrorProxy("fs", BlockDev, [(GLib.Error, FSError)], [not_implemented_rule, fs_nofs_rule])
__all__.append("fs")

nvdimm = ErrorProxy("nvdimm", BlockDev, [(GLib.Error, NVDIMMError)], [not_implemented_rule])
__all__.append("nvdimm")

s390 = ErrorProxy("s390", BlockDev, [(GLib.Error, S390Error)], [not_implemented_rule])
__all__.append("s390")

utils = ErrorProxy("utils", BlockDev, [(GLib.Error, UtilsError)])
__all__.append("utils")

vdo = ErrorProxy("vdo", BlockDev, [(GLib.Error, VDOError)])
__all__.append("vdo")
