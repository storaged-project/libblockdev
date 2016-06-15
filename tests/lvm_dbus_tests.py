from __future__ import division
import unittest
import os
import math
import overrides_hack
import six
import re

from utils import create_sparse_tempfile
from gi.repository import BlockDev, GLib

import dbus
sb = dbus.SystemBus()
lvm_dbus_running = any("lvmdbus" in name for name in sb.list_names())

if lvm_dbus_running:
    # force the new plugin to be used
    ps = BlockDev.PluginSpec()
    ps.name = BlockDev.Plugin.LVM
    ps.so_name = "libbd_lvm-dbus.so"
    ps2 = BlockDev.PluginSpec()
    ps2.name = BlockDev.Plugin.LOOP
    if not BlockDev.is_initialized():
        BlockDev.init([ps, ps2], None)
    else:
        BlockDev.reinit([ps, ps2], True, None)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmNoDevTestCase(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(LvmNoDevTestCase, self).__init__(*args, **kwargs)
        self._log = ""

    def test_is_supported_pe_size(self):
        """Verify that lvm_is_supported_pe_size works as expected"""

        self.assertTrue(BlockDev.lvm_is_supported_pe_size(4 * 1024))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(4 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(6 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(12 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(15 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(4 * 1024**3))

        self.assertFalse(BlockDev.lvm_is_supported_pe_size(512))
        self.assertFalse(BlockDev.lvm_is_supported_pe_size(4097))
        self.assertFalse(BlockDev.lvm_is_supported_pe_size(65535))
        self.assertFalse(BlockDev.lvm_is_supported_pe_size(32 * 1024**3))

    def test_get_supported_pe_sizes(self):
        """Verify that supported PE sizes are really supported"""

        for size in BlockDev.lvm_get_supported_pe_sizes():
            self.assertTrue(BlockDev.lvm_is_supported_pe_size(size))

    def test_get_max_lv_size(self):
        """Verify that max LV size is correctly determined"""

        if os.uname()[-1] == "i686":
            # 32-bit arch
            expected = 16 * 1024**4
        else:
            # 64-bit arch
            expected = 8 * 1024**6

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), expected)

    def test_round_size_to_pe(self):
        """Verify that round_size_to_pe works as expected"""

        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 4 * 1024**2, True), 12 * 1024**2)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 4 * 1024**2, False), 8 * 1024**2)

        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 6 * 1024**2, True), 12 * 1024**2)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 6 * 1024**2, False), 6 * 1024**2)

        # default PE size is 4 MiB
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 0, True), 12 * 1024**2)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 0, False), 8 * 1024**2)

        # cannot round up to GLib.MAXUINT64, but can round up over GLib.MAXUINT64 (should round down in that case)
        biggest_multiple = (GLib.MAXUINT64 // (4 * 1024**2)) * (4 * 1024**2)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(biggest_multiple + (2 * 1024**2), 4 * 1024**2, True),
                         biggest_multiple)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(biggest_multiple + (2 * 1024**2), 4 * 1024**2, False),
                         biggest_multiple)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(biggest_multiple - (2 * 4 * 1024**2) + 1, 4 * 1024**2, True),
                         biggest_multiple - (4 * 1024**2))
        self.assertEqual(BlockDev.lvm_round_size_to_pe(biggest_multiple - (2 * 4 * 1024**2) + 1, 4 * 1024**2, False),
                         biggest_multiple - (2 * 4 * 1024**2))

    def test_get_lv_physical_size(self):
        """Verify that get_lv_physical_size works as expected"""

        self.assertEqual(BlockDev.lvm_get_lv_physical_size(25 * 1024**3, 4 * 1024**2),
                         25 * 1024**3)

        # default PE size is 4 MiB
        self.assertEqual(BlockDev.lvm_get_lv_physical_size(25 * 1024**3, 0),
                         25 * 1024**3)

        self.assertEqual(BlockDev.lvm_get_lv_physical_size(11 * 1024**2, 4 * 1024**2),
                         12 * 1024**2)

    def test_get_thpool_padding(self):
        """Verify that get_thpool_padding works as expected"""

        expected_padding = BlockDev.lvm_round_size_to_pe(int(math.ceil(11 * 1024**2 * 0.2)),
                                                         4 * 1024**2, True)
        self.assertEqual(BlockDev.lvm_get_thpool_padding(11 * 1024**2, 4 * 1024**2, False),
                         expected_padding)

        expected_padding = BlockDev.lvm_round_size_to_pe(int(math.ceil(11 * 1024**2 * (1.0/6.0))),
                                                         4 * 1024**2, True)
        self.assertEqual(BlockDev.lvm_get_thpool_padding(11 * 1024**2, 4 * 1024**2, True),
                         expected_padding)

    def test_is_valid_thpool_md_size(self):
        """Verify that is_valid_thpool_md_size works as expected"""

        self.assertTrue(BlockDev.lvm_is_valid_thpool_md_size(2 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_valid_thpool_md_size(3 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_valid_thpool_md_size(16 * 1024**3))

        self.assertFalse(BlockDev.lvm_is_valid_thpool_md_size(1 * 1024**2))
        self.assertFalse(BlockDev.lvm_is_valid_thpool_md_size(17 * 1024**3))

    def test_is_valid_thpool_chunk_size(self):
        """Verify that is_valid_thpool_chunk_size works as expected"""

        # 64 KiB is OK with or without discard
        self.assertTrue(BlockDev.lvm_is_valid_thpool_chunk_size(64 * 1024, True))
        self.assertTrue(BlockDev.lvm_is_valid_thpool_chunk_size(64 * 1024, False))

        # 192 KiB is OK without discard, but NOK with discard
        self.assertTrue(BlockDev.lvm_is_valid_thpool_chunk_size(192 * 1024, False))
        self.assertFalse(BlockDev.lvm_is_valid_thpool_chunk_size(192 * 1024, True))

        # 191 KiB is NOK in both cases
        self.assertFalse(BlockDev.lvm_is_valid_thpool_chunk_size(191 * 1024, False))
        self.assertFalse(BlockDev.lvm_is_valid_thpool_chunk_size(191 * 1024, True))

    def _store_log(self, lvl, msg):
        self._log += str((lvl, msg))

    def test_get_set_global_config(self):
        """Verify that getting and setting global config works as expected"""

        # setup logging
        self.assertTrue(BlockDev.reinit([ps], False, self._store_log))

        # no global config set initially
        self.assertEqual(BlockDev.lvm_get_global_config(), "")

        # set and try to get back
        succ = BlockDev.lvm_set_global_config("bla")
        self.assertTrue(succ)
        self.assertEqual(BlockDev.lvm_get_global_config(), "bla")

        # reset and try to get back
        succ = BlockDev.lvm_set_global_config(None)
        self.assertTrue(succ)
        self.assertEqual(BlockDev.lvm_get_global_config(), "")

        # set twice and try to get back twice
        succ = BlockDev.lvm_set_global_config("foo")
        self.assertTrue(succ)
        succ = BlockDev.lvm_set_global_config("bla")
        self.assertTrue(succ)
        self.assertEqual(BlockDev.lvm_get_global_config(), "bla")
        self.assertEqual(BlockDev.lvm_get_global_config(), "bla")

        # set something sane and check it's really used
        succ = BlockDev.lvm_set_global_config("backup {backup=0 archive=0}")
        BlockDev.lvm_pvscan(None, False, None)
        self.assertIn("'--config': <'backup {backup=0 archive=0}'>", self._log)

        # reset back to default
        succ = BlockDev.lvm_set_global_config(None)
        self.assertTrue(succ)

    def test_cache_get_default_md_size(self):
        """Verify that default cache metadata size is calculated properly"""

        # 1000x smaller than the data LV size, but at least 8 MiB
        self.assertEqual(BlockDev.lvm_cache_get_default_md_size(100 * 1024**3), (100 * 1024**3) // 1000)
        self.assertEqual(BlockDev.lvm_cache_get_default_md_size(80 * 1024**3), (80 * 1024**3) // 1000)
        self.assertEqual(BlockDev.lvm_cache_get_default_md_size(6 * 1024**3), 8 * 1024**2)

    def test_cache_mode_bijection(self):
        """Verify that cache modes and their string representations map to each other"""

        mode_strs = {BlockDev.LVMCacheMode.WRITETHROUGH: "writethrough",
                     BlockDev.LVMCacheMode.WRITEBACK: "writeback",
                     BlockDev.LVMCacheMode.UNKNOWN: "unknown",
        }
        for mode in mode_strs.keys():
            self.assertEqual(BlockDev.lvm_cache_get_mode_str(mode), mode_strs[mode])
            self.assertEqual(BlockDev.lvm_cache_get_mode_from_str(mode_strs[mode]), mode)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_cache_get_mode_from_str("bla")

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVonlyTestCase(unittest.TestCase):
    # :TODO:
    #     * test pvmove (must create two PVs, a VG, a VG and some data in it
    #       first)
    #     * some complex test for pvs, vgs, lvs, pvinfo, vginfo and lvinfo
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("lvm_test", 1024**3)
        self.dev_file2 = create_sparse_tempfile("lvm_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

    def _clean_up(self):
        try:
            BlockDev.lvm_pvremove(self.loop_dev, None)
        except:
            pass

        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        try:
            BlockDev.lvm_pvremove(self.loop_dev2, None)
        except:
            pass
        os.unlink(self.dev_file)
        succ = BlockDev.loop_teardown(self.loop_dev2)
        if  not succ:
            os.unlink(self.dev_file2)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file2)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestPVcreateRemove(LvmPVonlyTestCase):
    def test_pvcreate_and_pvremove(self):
        """Verify that it's possible to create and destroy a PV"""

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_pvcreate("/non/existing/device", 0, 0, None)

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvremove(self.loop_dev, None)
        self.assertTrue(succ)

        # this time try to specify data_alignment and metadata_size
        succ = BlockDev.lvm_pvcreate(self.loop_dev, 2*1024**2, 4*1024**2, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_pvremove("/non/existing/device", None)

        succ = BlockDev.lvm_pvremove(self.loop_dev, None)
        self.assertTrue(succ)

        # already removed -- not an issue
        succ = BlockDev.lvm_pvremove(self.loop_dev, None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestPVresize(LvmPVonlyTestCase):
    def test_pvresize(self):
        """Verify that it's possible to resize a PV"""

        with self.assertRaises(GLib.GError):
            succ = BlockDev.lvm_pvresize(self.loop_dev, 200 * 1024**2, None)

        with self.assertRaises(GLib.GError):
            succ = BlockDev.lvm_pvresize("/non/existing/device", 200 * 1024**2, None)

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvresize(self.loop_dev, 200 * 1024**2, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvresize(self.loop_dev, 200 * 1024**3, None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestPVscan(LvmPVonlyTestCase):
    def test_pvscan(self):
        """Verify that pvscan runs without issues with cache or without"""

        succ = BlockDev.lvm_pvscan(None, False, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvscan(self.loop_dev, True, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvscan(None, True, None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestPVinfo(LvmPVonlyTestCase):
    def test_pvinfo(self):
        """Verify that it's possible to gather info about a PV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        info = BlockDev.lvm_pvinfo(self.loop_dev)
        self.assertTrue(info)
        self.assertEqual(info.pv_name, self.loop_dev)
        self.assertTrue(info.pv_uuid)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestPVs(LvmPVonlyTestCase):
    def test_pvs(self):
        """Verify that it's possible to gather info about PVs"""

        pvs = BlockDev.lvm_pvs()
        orig_len = len(pvs)

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        pvs = BlockDev.lvm_pvs()
        self.assertTrue(len(pvs) > orig_len)
        self.assertTrue(any(info.pv_name == self.loop_dev for info in pvs))

        info = BlockDev.lvm_pvinfo(self.loop_dev)
        self.assertTrue(info)

        self.assertTrue(any(info.pv_uuid == all_info.pv_uuid for all_info in pvs))

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGTestCase(LvmPVonlyTestCase):
    def _clean_up(self):
        try:
            BlockDev.lvm_vgremove("testVG", None)
        except:
            pass

        LvmPVonlyTestCase._clean_up(self)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestVGcreateRemove(LvmPVVGTestCase):
    def test_vgcreate_vgremove(self):
        """Verify that it is possible to create and destroy a VG"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgcreate("testVG", ["/non/existing/device"], 0, None)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        # VG already exists
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)

        succ = BlockDev.lvm_vgremove("testVG", None)
        self.assertTrue(succ)

        # no longer exists
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgremove("testVG", None)

class LvmTestVGrename(LvmPVVGTestCase):
    def test_vgrename(self):
        """Verify that it is possible to rename a VG"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        # try rename
        succ = BlockDev.lvm_vgrename("testVG", "testVG_new", None)
        self.assertTrue(succ)

        # rename back
        succ = BlockDev.lvm_vgrename("testVG_new", "testVG", None)
        self.assertTrue(succ)

        # (hopefully) non-existing VG
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgrename("testVG_new", "testVG", None)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestVGactivateDeactivate(LvmPVVGTestCase):
    def test_vgactivate_vgdeactivate(self):
        """Verify that it is possible to (de)activate a VG"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgactivate("nonexistingVG", None)

        succ = BlockDev.lvm_vgactivate("testVG", None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgdeactivate("nonexistingVG", None)

        succ = BlockDev.lvm_vgdeactivate("testVG", None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgactivate("testVG", None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgdeactivate("testVG", None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestVGextendReduce(LvmPVVGTestCase):
    def test_vgextend_vgreduce(self):
        """Verify that it is possible to extend/reduce a VG"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev], 0, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgextend("nonexistingVG", self.loop_dev2, None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgextend("testVG", "/non/existing/device", None)

        succ = BlockDev.lvm_vgextend("testVG", self.loop_dev2, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgreduce("testVG", self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgextend("testVG", self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgreduce("testVG", self.loop_dev2, None)
        self.assertTrue(succ)

        # try to remove missing PVs (there are none)
        succ = BlockDev.lvm_vgreduce("testVG", None, None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestVGinfo(LvmPVVGTestCase):
    def test_vginfo(self):
        """Verify that it is possible to gather info about a VG"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        info = BlockDev.lvm_vginfo("testVG")
        self.assertTrue(info)
        self.assertEqual(info.name, "testVG")
        self.assertTrue(info.uuid)
        self.assertEqual(info.pv_count, 2)
        self.assertTrue(info.size < 2 * 1024**3)
        self.assertEqual(info.free, info.size)
        self.assertEqual(info.extent_size, 4 * 1024**2)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestVGs(LvmPVVGTestCase):
    def test_vgs(self):
        """Verify that it's possible to gather info about VGs"""

        vgs = BlockDev.lvm_vgs()
        orig_len = len(vgs)

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev], 0, None)
        self.assertTrue(succ)

        vgs = BlockDev.lvm_vgs()
        self.assertTrue(len(vgs) > orig_len)
        self.assertTrue(any(info.name == "testVG" for info in vgs))

        info = BlockDev.lvm_vginfo("testVG")
        self.assertTrue(info)

        self.assertTrue(any(info.uuid == all_info.uuid for all_info in vgs))

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgremove("nonexistingVG", None)

        succ = BlockDev.lvm_vgremove("testVG", None)
        self.assertTrue(succ)

        # already removed
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_vgremove("testVG", None)

        succ = BlockDev.lvm_pvremove(self.loop_dev, None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGLVTestCase(LvmPVVGTestCase):
    def _clean_up(self):
        try:
            BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        except:
            pass

        LvmPVVGTestCase._clean_up(self)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVcreateRemove(LvmPVVGLVTestCase):
    def test_lvcreate_lvremove(self):
        """Verify that it's possible to create/destroy an LV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvcreate("nonexistingVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, ["/non/existing/device"], None)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        self.assertTrue(succ)

        # not enough space (only one PV)
        with six.assertRaisesRegex(self, GLib.GError, "Insufficient free space"):
            succ = BlockDev.lvm_lvcreate("testVG", "testLV", 1048 * 1024**2, None, [self.loop_dev], None)

        # enough space (two PVs)
        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 1048 * 1024**2, None, [self.loop_dev, self.loop_dev2], None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvremove("nonexistingVG", "testLV", True, None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvremove("testVG", "nonexistingLV", True, None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvremove("nonexistingVG", "nonexistingLV", True, None)

        succ = BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        self.assertTrue(succ)

        # already removed
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvremove("testVG", "testLV", True, None)

class LvmTestLVcreateWithExtra(LvmPVVGLVTestCase):
    def __init__(self, *args, **kwargs):
        LvmPVVGLVTestCase.__init__(self, *args, **kwargs)
        self.log = ""
        self.ignore_log = True

    def my_log_func(self, level, msg):
        if self.ignore_log:
            return
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    def test_lvcreate_with_extra(self):
        """Verify that it's possible to create an LV with extra arguments"""

        self.ignore_log = True
        self.assertTrue(BlockDev.reinit(None, False, self.my_log_func))

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvcreate("nonexistingVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, ["/non/existing/device"], None)

        self.ignore_log = False
        ea = BlockDev.ExtraArg.new("-Z", "y")
        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], [ea])
        self.assertTrue(succ)
        match = re.search(r"'-Z': <'y'>", self.log)
        self.assertIsNot(match, None)

        self.assertTrue(BlockDev.reinit(None, False, None))

        succ = BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVcreateType(LvmPVVGLVTestCase):
    def test_lvcreate_type(self):
        """Verify it's possible to create LVs with various types"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        # try to create a striped LV
        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, "striped", [self.loop_dev, self.loop_dev2], None)
        self.assertTrue(succ)

        # verify that the LV has the requested segtype
        info = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertEqual(info.segtype, "striped")

        succ = BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        self.assertTrue(succ)

        # try to create a mirrored LV
        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, "mirror", [self.loop_dev, self.loop_dev2], None)
        self.assertTrue(succ)

        # verify that the LV has the requested segtype
        info = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertEqual(info.segtype, "mirror")

        succ = BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        self.assertTrue(succ)

        # try to create a raid1 LV
        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, "raid1", [self.loop_dev, self.loop_dev2], None)
        self.assertTrue(succ)

        # verify that the LV has the requested segtype
        info = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertEqual(info.segtype, "raid1")

        succ = BlockDev.lvm_lvremove("testVG", "testLV", True, None)
        self.assertTrue(succ)


@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVactivateDeactivate(LvmPVVGLVTestCase):
    def test_lvactivate_lvdeactivate(self):
        """Verify it's possible to (de)actiavate an LV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvactivate("nonexistingVG", "testLV", True, None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvactivate("testVG", "nonexistingLV", True, None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvactivate("nonexistingVG", "nonexistingLV", True, None)

        succ = BlockDev.lvm_lvactivate("testVG", "testLV", True, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvdeactivate("nonexistingVG", "testLV", None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvdeactivate("testVG", "nonexistingLV", None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvdeactivate("nonexistingVG", "nonexistingLV", None)

        succ = BlockDev.lvm_lvdeactivate("testVG", "testLV", None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvactivate("testVG", "testLV", True, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvdeactivate("testVG", "testLV", None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVresize(LvmPVVGLVTestCase):
    def test_lvresize(self):
        """Verify that it's possible to resize an LV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvresize("nonexistingVG", "testLV", 768 * 1024**2, None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvresize("testVG", "nonexistingLV", 768 * 1024**2, None)

        # grow
        succ = BlockDev.lvm_lvresize("testVG", "testLV", 768 * 1024**2, None)
        self.assertTrue(succ)

        # same size
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvresize("testVG", "testLV", 768 * 1024**2, None)

        # shrink
        succ = BlockDev.lvm_lvresize("testVG", "testLV", 512 * 1024**2, None)
        self.assertTrue(succ)

        # shrink, not a multiple of 512
        succ = BlockDev.lvm_lvresize("testVG", "testLV", 500 * 1024**2, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvdeactivate("testVG", "testLV", None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVrename(LvmPVVGLVTestCase):
    def test_lvrename(self):
        """Verify that it's possible to rename an LV"""

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvrename("nonexistingVG", "testLV", "newTestLV", None)

        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvrename("testVG", "nonexistingLV", "newTestLV", None)

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        # rename
        succ = BlockDev.lvm_lvrename("testVG", "testLV", "newTestLV", None)
        self.assertTrue(succ)

        # and back
        succ = BlockDev.lvm_lvrename("testVG", "newTestLV", "testLV", None)
        self.assertTrue(succ)

        # needs a change
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_lvrename("testVG", "testLV", "testLV", None)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVsnapshots(LvmPVVGLVTestCase):
    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_snapshotcreate_lvorigin_snapshotmerge(self):
        """Verify that LV snapshot support works"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvsnapshotcreate("testVG", "testLV", "testLV_bak", 256 * 1024**2, None)
        self.assertTrue(succ)

        origin_name = BlockDev.lvm_lvorigin("testVG", "testLV_bak")
        lvi = BlockDev.lvm_lvinfo("testVG", "testLV_bak")
        self.assertEqual(origin_name, "testLV")
        self.assertEqual(lvi.origin, "testLV")
        self.assertIn("snapshot", lvi.roles.split(","))

        succ = BlockDev.lvm_lvsnapshotmerge("testVG", "testLV_bak", None)
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVinfo(LvmPVVGLVTestCase):
    def test_lvinfo(self):
        """Verify that it is possible to gather info about an LV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        info = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertTrue(info)
        self.assertEqual(info.lv_name, "testLV")
        self.assertEqual(info.vg_name, "testVG")
        self.assertTrue(info.uuid)
        self.assertEqual(info.size, 512 * 1024**2)
        self.assertIn("public", info.roles.split(","))

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVs(LvmPVVGLVTestCase):
    def test_lvs(self):
        """Verify that it's possible to gather info about LVs"""

        lvs = BlockDev.lvm_lvs(None)
        orig_len = len(lvs)

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        lvs = BlockDev.lvm_lvs(None)
        self.assertTrue(len(lvs) > orig_len)
        self.assertTrue(any(info.lv_name == "testLV" and info.vg_name == "testVG" for info in lvs))

        info = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertTrue(info)

        self.assertTrue(any(info.uuid == all_info.uuid for all_info in lvs))

        lvs = BlockDev.lvm_lvs("testVG")
        self.assertEqual(len(lvs), 1)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGthpoolTestCase(LvmPVVGTestCase):
    def _clean_up(self):
        try:
            BlockDev.lvm_lvremove("testVG", "testPool", True, None)
        except:
            pass

        LvmPVVGTestCase._clean_up(self)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestLVsAll(LvmPVVGthpoolTestCase):
    def test_lvs_all(self):
        """Verify that info is gathered for all LVs"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024, "thin-performance", None)
        self.assertTrue(succ)

        # there should be at least 3 LVs -- testPool, [testPool_tdata], [testPool_tmeta] (plus probably some spare LVs)
        lvs = BlockDev.lvm_lvs("testVG")
        self.assertGreater(len(lvs), 3)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestThpoolCreate(LvmPVVGthpoolTestCase):
    def test_thpoolcreate(self):
        """Verify that it is possible to create a thin pool"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024, "thin-performance", None)
        self.assertTrue(succ)

        info = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertIn("t", info.attr)
        self.assertIn("private", info.roles.split(","))

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestDataMetadataLV(LvmPVVGthpoolTestCase):
    def test_data_metadata_lv_name(self):
        """Verify that it is possible to get name of the data/metadata LV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024, "thin-performance", None)
        self.assertTrue(succ)

        name = BlockDev.lvm_data_lv_name("testVG", "testPool")
        lvi = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertTrue(name)
        self.assertTrue(name.startswith("testPool"))
        self.assertIn("_tdata", name)
        self.assertEqual(name, lvi.data_lv)

        info = BlockDev.lvm_lvinfo("testVG", name)
        self.assertTrue(info.attr.startswith("T"))
        self.assertIn("private", info.roles.split(","))
        self.assertIn("data", info.roles.split(","))

        name = BlockDev.lvm_metadata_lv_name("testVG", "testPool")
        lvi = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertTrue(name)
        self.assertTrue(name.startswith("testPool"))
        self.assertIn("_tmeta", name)
        self.assertEqual(name, lvi.metadata_lv)

        info = BlockDev.lvm_lvinfo("testVG", name)
        self.assertTrue(info.attr.startswith("e"))
        self.assertIn("private", info.roles.split(","))
        self.assertIn("metadata", info.roles.split(","))

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGLVthLVTestCase(LvmPVVGthpoolTestCase):
    def _clean_up(self):
        try:
            BlockDev.lvm_lvremove("testVG", "testThLV", True, None)
        except:
            pass

        LvmPVVGthpoolTestCase._clean_up(self)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestThLVcreate(LvmPVVGLVthLVTestCase):
    def test_thlvcreate_thpoolname(self):
        """Verify that it is possible to create a thin LV and get its pool name"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024, None, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thlvcreate("testVG", "testPool", "testThLV", 1024**3, None)
        self.assertTrue(succ)

        info = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertIn("t", info.attr)

        info = BlockDev.lvm_lvinfo("testVG", "testThLV")
        self.assertIn("V", info.attr)

        pool = BlockDev.lvm_thlvpoolname("testVG", "testThLV")
        lvi = BlockDev.lvm_lvinfo("testVG", "testThLV")
        self.assertEqual(pool, "testPool")
        self.assertEqual(lvi.pool_lv, "testPool")

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGLVthLVsnapshotTestCase(LvmPVVGLVthLVTestCase):
    def _clean_up(self):
        try:
            BlockDev.lvm_lvremove("testVG", "testThLV_bak", True, None)
        except:
            pass

        LvmPVVGLVthLVTestCase._clean_up(self)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmTestThSnapshotCreate(LvmPVVGLVthLVsnapshotTestCase):
    def test_thsnapshotcreate(self):
        """Verify that it is possible to create a thin LV snapshot"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)

        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024, None, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_thlvcreate("testVG", "testPool", "testThLV", 1024**3, None)
        self.assertTrue(succ)

        info = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertIn("t", info.attr)

        info = BlockDev.lvm_lvinfo("testVG", "testThLV")
        self.assertIn("V", info.attr)

        succ = BlockDev.lvm_thsnapshotcreate("testVG", "testThLV", "testThLV_bak", "testPool", None)
        self.assertTrue(succ)

        info = BlockDev.lvm_lvinfo("testVG", "testThLV_bak")
        self.assertIn("V", info.attr)
        self.assertIn("snapshot", info.roles.split(","))
        self.assertIn("thinsnapshot", info.roles.split(","))

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGLVcachePoolTestCase(LvmPVVGLVTestCase):
    def _clean_up(self):
        try:
            BlockDev.lvm_lvremove("testVG", "testCache", True, None)
        except:
            pass

        LvmPVVGLVTestCase._clean_up(self)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGLVcachePoolCreateRemoveTestCase(LvmPVVGLVcachePoolTestCase):
    def test_cache_pool_create_remove(self):
        """Verify that is it possible to create and remove a cache pool"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_create_pool("testVG", "testCache", 512 * 1024**2, 0, BlockDev.LVMCacheMode.WRITETHROUGH, 0, [self.loop_dev])
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvremove("testVG", "testCache", True, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_create_pool("testVG", "testCache", 512 * 1024**2, 0, BlockDev.LVMCacheMode.WRITEBACK,
                                              BlockDev.LVMCachePoolFlags.STRIPED|BlockDev.LVMCachePoolFlags.META_RAID1,
                                              [self.loop_dev, self.loop_dev2])
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGLVcachePoolAttachDetachTestCase(LvmPVVGLVcachePoolTestCase):
    def test_cache_pool_attach_detach(self):
        """Verify that is it possible to attach and detach a cache pool"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_create_pool("testVG", "testCache", 512 * 1024**2, 0, BlockDev.LVMCacheMode.WRITETHROUGH, 0, [self.loop_dev2])
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_attach("testVG", "testLV", "testCache", None)
        self.assertTrue(succ)

        # detach and destroy (the last arg)
        succ = BlockDev.lvm_cache_detach("testVG", "testLV", True, None)
        self.assertTrue(succ)

        # once more and do not destroy this time
        succ = BlockDev.lvm_cache_create_pool("testVG", "testCache", 512 * 1024**2, 0, BlockDev.LVMCacheMode.WRITETHROUGH, 0, [self.loop_dev2])
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_attach("testVG", "testLV", "testCache", None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_detach("testVG", "testLV", False, None)
        self.assertTrue(succ)

        lvs = BlockDev.lvm_lvs("testVG")
        self.assertTrue(any(info.lv_name == "testCache" for info in lvs))

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGcachedLVTestCase(LvmPVVGLVTestCase):
    def test_create_cached_lv(self):
        """Verify that it is possible to create a cached LV in a single step"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_create_cached_lv("testVG", "testLV", 512 * 1024**2, 256 * 1024**2, 10 * 1024**2,
                                                   BlockDev.LVMCacheMode.WRITEBACK, 0,
                                                   [self.loop_dev], [self.loop_dev2])
        self.assertTrue(succ)

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGcachedLVpoolTestCase(LvmPVVGLVTestCase):
    def test_cache_get_pool_name(self):
        """Verify that it is possible to get the name of the cache pool"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_create_pool("testVG", "testCache", 512 * 1024**2, 0, BlockDev.LVMCacheMode.WRITETHROUGH, 0, [self.loop_dev2])
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_attach("testVG", "testLV", "testCache", None)
        self.assertTrue(succ)

        self.assertEqual(BlockDev.lvm_cache_pool_name("testVG", "testLV"), "testCache")

@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LvmPVVGcachedLVstatsTestCase(LvmPVVGLVTestCase):
    def test_cache_get_stats(self):
        """Verify that it is possible to get stats for a cached LV"""

        succ = BlockDev.lvm_pvcreate(self.loop_dev, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_pvcreate(self.loop_dev2, 0, 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0, None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_create_pool("testVG", "testCache", 512 * 1024**2, 0, BlockDev.LVMCacheMode.WRITETHROUGH, 0, [self.loop_dev2])
        self.assertTrue(succ)

        succ = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, None, [self.loop_dev], None)
        self.assertTrue(succ)

        succ = BlockDev.lvm_cache_attach("testVG", "testLV", "testCache", None)
        self.assertTrue(succ)

        stats = BlockDev.lvm_cache_stats("testVG", "testLV")
        self.assertTrue(stats)
        self.assertEqual(stats.cache_size, 512 * 1024**2)
        self.assertEqual(stats.md_size, 8 * 1024**2)
        self.assertEqual(stats.mode, BlockDev.LVMCacheMode.WRITETHROUGH)
