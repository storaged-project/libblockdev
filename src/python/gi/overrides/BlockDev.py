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
               "s390": BlockDev.Plugin.S390,
}

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

_try_init = BlockDev.try_init
@override(BlockDev.try_init)
def try_init(require_plugins=None, log_func=None):
    return _try_init(require_plugins, log_func)
__all__.append("try_init")

_try_reinit = BlockDev.try_reinit
@override(BlockDev.try_reinit)
def try_reinit(require_plugins=None, reload=True, log_func=None):
    return _try_reinit(require_plugins, reload, log_func)
__all__.append("try_reinit")

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

_btrfs_list_subvolumes = BlockDev.btrfs_list_subvolumes
@override(BlockDev.btrfs_list_subvolumes)
def btrfs_list_subvolumes(mountpoint, snapshots_only=False):
    return _btrfs_list_subvolumes(mountpoint, snapshots_only)
__all__.append("btrfs_list_subvolumes")

_btrfs_create_snapshot = BlockDev.btrfs_create_snapshot
@override(BlockDev.btrfs_create_snapshot)
def btrfs_create_snapshot(source, dest, ro=False):
    return _btrfs_create_snapshot(source, dest, ro)
__all__.append("btrfs_create_snapshot")


_crypto_luks_format = BlockDev.crypto_luks_format
@override(BlockDev.crypto_luks_format)
def crypto_luks_format(device, cipher=None, key_size=0, passphrase=None, key_file=None, min_entropy=0):
    return _crypto_luks_format(device, cipher, key_size, passphrase, key_file, min_entropy)
__all__.append("crypto_luks_format")

_crypto_luks_open = BlockDev.crypto_luks_open
@override(BlockDev.crypto_luks_open)
def crypto_luks_open(device, name, passphrase=None, key_file=None):
    return _crypto_luks_open(device, name, passphrase, key_file)
__all__.append("crypto_luks_open")

_crypto_luks_resize = BlockDev.crypto_luks_resize
@override(BlockDev.crypto_luks_resize)
def crypto_luks_resize(luks_device, size=0):
    return _crypto_luks_resize(luks_device, size)
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
def lvm_pvcreate(device, data_alignment=0, metadata_size=0):
    return _lvm_pvcreate(device, data_alignment, metadata_size)
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
def lvm_lvcreate(vg_name, lv_name, size, type=None, pv_list=None):
    return _lvm_lvcreate(vg_name, lv_name, size, type, pv_list)
__all__.append("lvm_lvcreate")

_lvm_lvremove = BlockDev.lvm_lvremove
@override(BlockDev.lvm_lvremove)
def lvm_lvremove(vg_name, lv_name, force=False):
    return _lvm_lvremove(vg_name, lv_name, force)
__all__.append("lvm_lvremove")

_lvm_lvactivate = BlockDev.lvm_lvactivate
@override(BlockDev.lvm_lvactivate)
def lvm_lvactivate(vg_name, lv_name, ignore_skip=False):
    return _lvm_lvactivate(vg_name, lv_name, ignore_skip)
__all__.append("lvm_lvactivate")

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


_kbd_zram_create_devices = BlockDev.kbd_zram_create_devices
@override(BlockDev.kbd_zram_create_devices)
def kbd_zram_create_devices(num_devices, sizes, nstreams=None):
    return _kbd_zram_create_devices(num_devices, sizes, nstreams)
__all__.append("kbd_zram_create_devices")


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


XRule = namedtuple("XRule", ["orig_exc", "regexp", "new_exc"])
# XXX: how to document namedtuple fields?
"""
:field orig_exc: exception class to be transformed
:field regexp: regexp that needs to match exception msg for this rule to be
               applicable or None if no match is required
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
            items = set(dir(self._mod) + locals().keys())
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
                    if self._xrules[e_type].regexp and self._xrules[e_type].regexp.match(msg):
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
__all__.append("SwapError")

class KbdError(BlockDevError):
    pass
__all__.append("KbdError")

class S390Error(BlockDevError):
    pass
__all__.append("S390Error")

class UtilsError(BlockDevError):
    pass
__all__.append("UtilsError")

class BlockDevNotImplementedError(NotImplementedError, BlockDevError):
    pass
__all__.append("BlockDevNotImplementedError")

not_implemented_rule = XRule(GLib.Error, re.compile(r".*The function '.*' called, but not implemented!"), BlockDevNotImplementedError)

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

swap = ErrorProxy("swap", BlockDev, [(GLib.Error, SwapError)], [not_implemented_rule])
__all__.append("swap")

kbd = ErrorProxy("kbd", BlockDev, [(GLib.Error, KbdError)], [not_implemented_rule])
__all__.append("kbd")

s390 = ErrorProxy("s390", BlockDev, [(GLib.Error, S390Error)], [not_implemented_rule])
__all__.append("s390")

utils = ErrorProxy("utils", BlockDev, [(GLib.Error, UtilsError)])
__all__.append("utils")
