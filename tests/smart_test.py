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
    ATA_JSON_DUMPS = ["TOSHIBA_THNSNH128GBST", "Hitachi_HDS5C3020ALA632", "HGST_HMS5C4040BLE640",
                      "HGST_HUS726060ALA640", "INTEL_SSDSC2BB120G4L", "WDC_WD10EFRX-68PJCN0"]
    SCSI_JSON_DUMPS = ["WD4001FYYG-01SL3", "HGST_HUSMR3280ASS200", "SEAGATE_ST600MP0036",
                      "TOSHIBA_AL15SEB120NY", "TOSHIBA_AL15SEB18EQY", "TOSHIBA_KPM5XMUG400G"]
    SKDUMPS = ["TOSHIBA_THNSNH128GBST", "Hitachi_HDS721010CLA632", "WDC_WD20EARS-00MVWB0",
               "SAMSUNG_HS122JC", "SAMSUNG_MMCRE28G5MXP-0VBH1", "IBM_IC25N020ATCS04-0",
               "Maxtor_6Y120P0"]

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


class SmartmontoolsTest(SMARTTest):

    @classmethod
    def setUpClass(cls):
        cls.ps = BlockDev.PluginSpec(name=BlockDev.Plugin.SMART, so_name="libbd_smartmontools.so.3")
        cls.ps2 = BlockDev.PluginSpec(name=BlockDev.Plugin.LOOP)
        if not BlockDev.is_initialized():
            BlockDev.init([cls.ps, cls.ps2], None)
        else:
            BlockDev.reinit([cls.ps, cls.ps2], True, None)


    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.SMART), "libbd_smartmontools.so.3")


    @tag_test(TestTags.CORE)
    def test_ata_info(self):
        """Test SMART ATA info on LIO, loop and scsi_debug devices"""

        if not shutil.which("smartctl"):
            raise unittest.SkipTest("smartctl executable not found in $PATH, skipping.")

        # non-existing device
        msg = r".*Error getting ATA SMART info: /dev/.*: Unable to detect device type"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info("/dev/nonexistent")

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        msg = r"Missing 'ata_smart_data' section: The member .ata_smart_data. is not defined in the object at the current position."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.lio_dev)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        msg = r".*Error getting ATA SMART info: /dev/.*: Unable to detect device type"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.loop_dev)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        msg = r"Missing 'ata_smart_data' section: The member .ata_smart_data. is not defined in the object at the current position."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_ata_get_info(self.scsi_debug_dev)


    @tag_test(TestTags.CORE)
    def test_ata_real_dumps_fake_tool(self):
        """Test SMART ATA info on supplied JSON dumps (from real devices) via fake smartctl"""

        with fake_utils("tests/fake_utils/smartctl"):
            for d in self.ATA_JSON_DUMPS:
                data = BlockDev.smart_ata_get_info(d)
                self.assertIsNotNone(data)
                self.assertTrue(data.overall_status_passed)
                self.assertGreater(data.power_cycle_count, 0)
                self.assertGreater(data.power_on_time, 0)
                self.assertEqual(data.self_test_percent_remaining, 0)
                self.assertGreater(data.smart_capabilities, 0)
                self.assertTrue(data.smart_enabled)
                self.assertTrue(data.smart_supported)
                self.assertGreater(data.temperature, 0)
                self.assertGreater(len(data.attributes), 0)
                for attr in data.attributes:
                    self.assertGreater(attr.id, 0)
                    self.assertGreater(len(attr.name), 0)
                    self.assertGreater(len(attr.pretty_value_string), 0)

    @tag_test(TestTags.CORE)
    def test_ata_real_dumps_by_blob(self):
        """Test SMART ATA info on supplied skdump blobs (from real devices)"""

        # feed it with garbage
        for d in ["/dev/zero", "/dev/random"]:
            with open(d, "rb") as f:
                content = f.read(1024)
                msg = r"Error getting ATA SMART info: (Empty response|JSON data must be UTF-8 encoded)"
                with self.assertRaisesRegex(GLib.GError, msg):
                    BlockDev.smart_ata_get_info_from_data(content)

        # feed it with skdumps
        for d in self.SKDUMPS:
            with open(os.path.join("tests", "smart_dumps", "%s.bin" % d), "rb") as f:
                content = f.read()
                msg = r"Error getting ATA SMART info: .* Parse error: .*"
                with self.assertRaisesRegex(GLib.GError, msg):
                    BlockDev.smart_ata_get_info_from_data(content)

        # feed it with proper JSON
        for d in self.ATA_JSON_DUMPS:
            with open(os.path.join("tests", "smart_dumps", "%s.json" % d), "rb") as f:
                content = f.read()
                data = BlockDev.smart_ata_get_info_from_data(content)
                self.assertIsNotNone(data)
                self.assertTrue(data.overall_status_passed)
                self.assertGreater(data.power_cycle_count, 0)
                self.assertGreater(data.power_on_time, 0)
                self.assertEqual(data.self_test_percent_remaining, 0)
                self.assertGreater(data.smart_capabilities, 0)
                self.assertTrue(data.smart_enabled)
                self.assertTrue(data.smart_supported)
                self.assertGreater(data.temperature, 0)
                self.assertGreater(len(data.attributes), 0)
                for attr in data.attributes:
                    self.assertGreater(attr.id, 0)
                    self.assertGreater(len(attr.name), 0)
                    self.assertGreater(len(attr.pretty_value_string), 0)


    @tag_test(TestTags.CORE)
    def test_ata_error_dumps(self):
        """Test SMART ATA info on supplied JSON dumps (error cases)"""

        with fake_utils("tests/fake_utils/smartctl"):
            msg = r"Reported smartctl JSON format version too low: 0"
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_ata_get_info("01_old_ver")

            msg = r"Error getting ATA SMART info: Command line did not parse."
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_ata_get_info("02_exit_err")

            data = BlockDev.smart_ata_get_info("03_exit_err_32")
            self.assertIsNotNone(data)

            msg = r"Error getting ATA SMART info: .* Parse error: unexpected character"
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_ata_get_info("04_malformed")

            msg = r"Empty response"
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_ata_get_info("05_empty")


    @tag_test(TestTags.CORE)
    def test_smart_enable_disable(self):
        """Test turning SMART functionality on/off over LIO, loop and scsi_debug devices"""
        # FYI: take this test with a grain of salt - smartctl behaves unpredictably on unsupported
        #      devices, often not reporting any error at all despite the device clearly not supporting
        #      the SMART functionality

        if not shutil.which("smartctl"):
            raise unittest.SkipTest("smartctl executable not found in $PATH, skipping.")

        # non-existing device
        msg = r"Error setting SMART functionality: /dev/.*: Unable to detect device type"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled("/dev/nonexistent", False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled("/dev/nonexistent", True)

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        msg = r"Error setting SMART functionality: Some SMART or other ATA command to the disk failed, or there was a checksum error in a SMART data structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.lio_dev, False)
        # smartctl doesn't report failure when turning SMART on
        BlockDev.smart_set_enabled(self.lio_dev, True)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        # Sadly there's no specific message reported for loop devices (not being an ATA device)
        msg = r"Error setting SMART functionality: /dev/.*: Unable to detect device type"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.loop_dev, False)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_set_enabled(self.loop_dev, True)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        BlockDev.smart_set_enabled(self.scsi_debug_dev, False)
        BlockDev.smart_set_enabled(self.scsi_debug_dev, True)


    @tag_test(TestTags.CORE)
    def test_smart_selftest(self):
        """Test SMART self-test functionality over LIO, loop and scsi_debug devices"""

        if not shutil.which("smartctl"):
            raise unittest.SkipTest("smartctl executable not found in $PATH, skipping.")

        # non-existing device
        msg = r"Error executing SMART self-test: /dev/.*: Unable to detect device type"
        for t in [BlockDev.SmartSelfTestOp.OFFLINE, BlockDev.SmartSelfTestOp.SHORT,
                  BlockDev.SmartSelfTestOp.LONG, BlockDev.SmartSelfTestOp.CONVEYANCE,
                  BlockDev.SmartSelfTestOp.ABORT]:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test("/dev/nonexistent", t)

        # LIO device (SCSI)
        self._setup_lio()
        self.addCleanup(self._clean_lio)
        msg = r"Error executing SMART self-test: Some SMART or other ATA command to the disk failed, or there was a checksum error in a SMART data structure."
        for t in [BlockDev.SmartSelfTestOp.OFFLINE, BlockDev.SmartSelfTestOp.SHORT,
                  BlockDev.SmartSelfTestOp.LONG, BlockDev.SmartSelfTestOp.ABORT]:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test(self.lio_dev, t)
        BlockDev.smart_device_self_test(self.lio_dev, BlockDev.SmartSelfTestOp.CONVEYANCE)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        msg = r"Error executing SMART self-test: /dev/.*: Unable to detect device type"
        for t in [BlockDev.SmartSelfTestOp.OFFLINE, BlockDev.SmartSelfTestOp.SHORT,
                  BlockDev.SmartSelfTestOp.LONG, BlockDev.SmartSelfTestOp.CONVEYANCE,
                  BlockDev.SmartSelfTestOp.ABORT]:
            with self.assertRaisesRegex(GLib.GError, msg):
                BlockDev.smart_device_self_test(self.loop_dev, t)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        for t in [BlockDev.SmartSelfTestOp.OFFLINE, BlockDev.SmartSelfTestOp.SHORT,
                  BlockDev.SmartSelfTestOp.LONG, BlockDev.SmartSelfTestOp.CONVEYANCE,
                  BlockDev.SmartSelfTestOp.ABORT]:
            BlockDev.smart_device_self_test(self.scsi_debug_dev, t)


    @tag_test(TestTags.CORE)
    def test_scsi_info(self):
        """Test SMART SCSI info on LIO, loop and scsi_debug devices"""

        if not shutil.which("smartctl"):
            raise unittest.SkipTest("smartctl executable not found in $PATH, skipping.")

        # non-existing device
        msg = r".*Error getting SCSI SMART info: Smartctl open device: /dev/.* failed: No such device"
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info("/dev/nonexistent")

        # LIO device (SCSI)
        self._setup_lio()
        msg = r".*Error getting SCSI SMART info: Some SMART or other ATA command to the disk failed, or there was a checksum error in a SMART data structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info(self.lio_dev)

        # loop device
        self._setup_loop()
        self.addCleanup(self._clean_loop)
        msg = r"Error getting SCSI SMART info: Device open failed or device did not return an IDENTIFY DEVICE structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info(self.loop_dev)

        # scsi_debug
        self._setup_scsi_debug()
        self.addCleanup(self._clean_scsi_debug)
        msg = r".*Error getting SCSI SMART info: Some SMART or other ATA command to the disk failed, or there was a checksum error in a SMART data structure."
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.smart_scsi_get_info(self.scsi_debug_dev)


    @tag_test(TestTags.CORE)
    def test_scsi_real_dumps(self):
        """Test SMART SCSI info on supplied JSON dumps (from real devices)"""

        with fake_utils("tests/fake_utils/smartctl"):
            for d in self.SCSI_JSON_DUMPS:
                data = BlockDev.smart_scsi_get_info(d)
                self.assertIsNotNone(data)
                self.assertTrue(data.overall_status_passed)
                self.assertTrue(data.smart_supported)
                self.assertGreater(data.power_on_time, 0)
                self.assertGreater(data.temperature, 0)
                self.assertGreater(data.temperature_drive_trip, 0)
                self.assertGreater(data.read_processed_bytes, 1000000)
                self.assertGreater(data.write_processed_bytes, 1000000)


class LibatasmartTest(SMARTTest):

    @classmethod
    def setUpClass(cls):
        cls.ps = BlockDev.PluginSpec(name=BlockDev.Plugin.SMART, so_name="libbd_smart.so.3")
        cls.ps2 = BlockDev.PluginSpec(name=BlockDev.Plugin.LOOP)
        if not BlockDev.is_initialized():
            BlockDev.init([cls.ps, cls.ps2], None)
        else:
            BlockDev.reinit([cls.ps, cls.ps2], True, None)

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

        # feed it with JSON
        for d in self.ATA_JSON_DUMPS:
            with open(os.path.join("tests", "smart_dumps", "%s.json" % d), "rb") as f:
                content = f.read()
                msg = r"Error parsing blob data: Invalid argument"
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
