import unittest
import os
import re
import glob
import time
import shutil
import overrides_hack

from utils import run, create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, TestTags, tag_test, write_file, run_command

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import BlockDev, GLib


class SMARTTest(unittest.TestCase):

    # dumps from real drives, both HDD and SSD
    SKDUMPS = ["TOSHIBA_THNSNH128GBST", "Hitachi_HDS721010CLA632", "WDC_WD20EARS-00MVWB0",
               "SAMSUNG_HS122JC", "SAMSUNG_MMCRE28G5MXP-0VBH1", "IBM_IC25N020ATCS04-0",
               "Maxtor_6Y120P0", "SiliconPower_SSD_SBFM61.3", "Patriot_Burst_240GB",
               "KINGSTON_SA400S37480G_SBFKQ13", "GIGABYTE_GP-GSTFS31100TNTD",
               "Biwintech_SSD_SX500"]

    @classmethod
    def setUpClass(cls):
        cls.ps = BlockDev.PluginSpec(name=BlockDev.Plugin.SMART, so_name="libbd_smart.so.3")
        cls.ps2 = BlockDev.PluginSpec(name=BlockDev.Plugin.LOOP)
        if not BlockDev.is_initialized():
            BlockDev.init([cls.ps, cls.ps2], None)
        else:
            BlockDev.reinit([cls.ps, cls.ps2], True, None)

    def _setup_lio(self):
        self.lio_dev_file = create_sparse_tempfile("smart_test", 1024**3)
        try:
            self.lio_dev = create_lio_device(self.lio_dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup LIO device for testing: %s" % e)

    def _clean_lio(self):
        try:
            delete_lio_device(self.lio_dev)
        except:
            pass
        os.unlink(self.lio_dev_file)

    def _setup_loop(self):
        self.loop_dev_file = create_sparse_tempfile("smart_test", 1024**3)
        succ, self.loop_dev = BlockDev.loop_setup(self.loop_dev_file)
        self.assertTrue(succ)
        self.assertTrue(self.loop_dev)
        self.loop_dev = '/dev/' + self.loop_dev

    def _clean_loop(self):
        try:
            BlockDev.loop_teardown(self.loop_dev)
        except:
            pass
        os.unlink(self.loop_dev_file)

    def _setup_scsi_debug(self):
        res, _out, _err = run_command('modprobe scsi_debug')
        self.assertEqual(res, 0)
        dirs = []
        while len(dirs) < 1:
            dirs = glob.glob('/sys/bus/pseudo/drivers/scsi_debug/adapter*/host*/target*/*:*/block')
            time.sleep(0.1)
        self.scsi_debug_dev = os.listdir(dirs[0])
        self.assertEqual(len(self.scsi_debug_dev), 1)
        self.scsi_debug_dev = '/dev/' + self.scsi_debug_dev[0]
        self.assertTrue(os.path.exists(self.scsi_debug_dev))

    def _clean_scsi_debug(self):
        try:
            device = self.scsi_debug_dev.split('/')[-1]
            if os.path.exists('/sys/block/' + device):
                self.write_file('/sys/block/%s/device/delete' % device, '1')
            while os.path.exists(device):
                time.sleep(0.1)
            self.run_command('modprobe -r scsi_debug')
        except:
            pass

    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.SMART), "libbd_smart.so.3")

    @tag_test(TestTags.CORE)
    def test_ata_info(self):
        """Test SMART ATA info on LIO, loop and scsi_debug devices"""

        # non-existing device
        msg = r"Error opening device /dev/.*: No such file or directory"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info("/dev/nonexistent")

        msg = r"Error reading SMART data from device: Operation not supported"

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.lio_dev)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.loop_dev)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.scsi_debug_dev)

    @tag_test(TestTags.CORE)
    def test_smart_enable_disable(self):
        """Test turning SMART functionality on/off over LIO, loop and scsi_debug devices"""

        msg = r"Enabling/disabling ATA SMART functionality is unavailable with libatasmart"

        # non-existing device
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled("/dev/nonexistent", False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled("/dev/nonexistent", True)

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.lio_dev, False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.lio_dev, True)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.loop_dev, False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.loop_dev, True)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.scsi_debug_dev, False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.scsi_debug_dev, True)

    @tag_test(TestTags.CORE)
    def test_smart_selftest(self):
        """Test SMART self-test functionality over LIO, loop and scsi_debug devices"""

        tests = [BlockDev.SmartSelfTestOp.OFFLINE, BlockDev.SmartSelfTestOp.SHORT,
                 BlockDev.SmartSelfTestOp.LONG, BlockDev.SmartSelfTestOp.CONVEYANCE,
                 BlockDev.SmartSelfTestOp.ABORT]

        # non-existing device
        msg = r"Error opening device /dev/.*: No such file or directory"
        for t in tests:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test("/dev/nonexistent", t)

        msg = r"Error triggering device self-test: Operation not supported"

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        for t in tests:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test(self.lio_dev, t)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        for t in tests:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test(self.loop_dev, t)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        for t in tests:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test(self.scsi_debug_dev, t)

    @tag_test(TestTags.CORE)
    def test_scsi_info(self):
        """Test SMART SCSI info on LIO, loop and scsi_debug devices"""

        # non-existing device
        msg = r"SCSI SMART is unavailable with libatasmart"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info("/dev/nonexistent")

        # LIO device (SCSI)
        self._setup_lio()
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info(self.lio_dev)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info(self.loop_dev)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info(self.scsi_debug_dev)

    @tag_test(TestTags.CORE)
    def test_ata_real_dumps_by_blob(self):
        """Test SMART ATA info on supplied skdump blobs (from real devices)"""

        # feed it with garbage
        for d in ["/dev/zero", "/dev/random"]:
            with open(d, "rb") as f:
                content = f.read(1024)
                msg = r"Error parsing blob data: (Unknown error -61|Invalid argument)"
                with self.assertRaisesRegex(GLib.GError, msg):
                    BlockDev.smart_ata_get_info_from_data(content)

        # feed it with proper skdumps
        for d in self.SKDUMPS:
            with open(os.path.join("tests", "smart_dumps", "%s.bin" % d), "rb") as f:
                content = f.read()
                data = BlockDev.smart_ata_get_info_from_data(content)
                self.assertIsNotNone(data)
                self.assertGreater(data.power_cycle_count, 0)
                self.assertGreater(data.power_on_time, 0)
                self.assertEqual(data.smart_capabilities, 0)
                self.assertTrue(data.smart_enabled)
                self.assertTrue(data.smart_supported)
                self.assertLess(data.temperature, 320)
                self.assertGreater(len(data.attributes), 0)
                for attr in data.attributes:
                    self.assertGreater(attr.id, 0)
                    self.assertGreater(len(attr.name), 0)
                    self.assertGreater(len(attr.pretty_value_string), 0)
