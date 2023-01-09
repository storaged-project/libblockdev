import unittest
import os
import re
import glob
import time
import shutil
import overrides_hack

from utils import run, create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, TestTags, tag_test, write_file, run_command
from gi.repository import BlockDev, GLib


class SMARTTest(unittest.TestCase):
    requested_plugins = BlockDev.plugin_specs_from_names(("smart", "loop"))

    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.SMART), "libbd_smart.so.3")

    @classmethod
    def setUpClass(cls):
        if not shutil.which("smartctl"):
            raise unittest.SkipTest("smartctl executable not found in $PATH, skipping.")

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)


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


    @tag_test(TestTags.CORE)
    def test_ata_info(self):
        """Test SMART ATA info on LIO, loop and scsi_debug devices"""

        # non-existing device
        msg = r".*Error getting ATA SMART info: Smartctl open device: /dev/.* failed: No such device"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info("/dev/nonexistent", False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info("/dev/nonexistent", True)

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        msg = r"Error getting ATA SMART info: Device open failed or device did not return an IDENTIFY DEVICE structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.lio_dev, False)
        msg = r"Device is in a low-power mode"     # FIXME
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.lio_dev, True)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        # Sadly there's no specific message reported for loop devices (not being an ATA device)
        msg = r"Error getting ATA SMART info: Device open failed or device did not return an IDENTIFY DEVICE structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.loop_dev, False)
        msg = r"Device is in a low-power mode"     # FIXME
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.loop_dev, True)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        msg = r"Error getting ATA SMART info: Device open failed or device did not return an IDENTIFY DEVICE structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.scsi_debug_dev, False)
        msg = r"Device is in a low-power mode"     # FIXME
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.loop_dev, True)

