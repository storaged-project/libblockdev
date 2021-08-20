import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, create_nvmet_device, delete_nvmet_device, fake_utils, fake_path, run_command, run, TestTags, tag_test
from gi.repository import BlockDev, GLib
from distutils.spawn import find_executable


class NVMeTest(unittest.TestCase):
    requested_plugins = BlockDev.plugin_specs_from_names(("nvme",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)


class NVMeTestCase(NVMeTest):
    def setUp(self):
        if not find_executable("nvme"):
            raise unittest.SkipTest("nvme executable (nvme-cli package) not found in $PATH, skipping.")
        if not find_executable("nvmetcli"):
            raise unittest.SkipTest("nvmetcli executable not found in $PATH, skipping.")

        self.dev_file = None
        self.loop_dev = None
        self.nvme_dev = None
        self.nvme_ns_dev = None

        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("nvme_test", 1024**3)

        ret, self.loop_dev, err = run_command("losetup --find --show %s" % self.dev_file)
        if ret != 0:
            raise RuntimeError("Cannot attach loop device: '%s %s'" % (self.dev_file, err))

        self.nvme_dev = create_nvmet_device(self.loop_dev)
        self.nvme_ns_dev = self.nvme_dev + "n1"

    def _clean_up(self):
        if self.nvme_dev:
            try:
                delete_nvmet_device(self.nvme_dev)
            except RuntimeError:
                # just move on, we can do no better here
                pass

        # detach the loop device
        run_command("losetup --detach %s" % self.loop_dev)
        if self.dev_file:
            os.unlink(self.dev_file)

    @tag_test(TestTags.CORE)
    def test_ns_info(self):
        """Test namespace info"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_namespace_info("/dev/nonexistent")

        with self.assertRaisesRegexp(GLib.GError, r"Error getting Namespace Identifier \(NSID\): Inappropriate ioctl for device"):
            BlockDev.nvme_get_namespace_info(self.nvme_dev)

        # not much information can be gathered from loop-based NVMe target devices...
        info = BlockDev.nvme_get_namespace_info(self.nvme_ns_dev)
        self.assertFalse(info.features & BlockDev.NVMENamespaceFeature.THIN)
        self.assertTrue (info.features & BlockDev.NVMENamespaceFeature.MULTIPATH_SHARED)
        self.assertFalse(info.features & BlockDev.NVMENamespaceFeature.FORMAT_PROGRESS)
        self.assertEqual(info.eui64, "0000000000000000")
        self.assertEqual(info.format_progress_remaining, 0)
        self.assertEqual(len(info.lba_formats), 0)
        self.assertGreater(len(info.nguid), 0)
        self.assertEqual(info.nsid, 0)
        self.assertEqual(info.ncap, 2097152)
        self.assertEqual(info.nsize, 2097152)
        self.assertEqual(info.nuse, 2097152)
        self.assertGreater(len(info.uuid), 0)
        self.assertFalse(info.write_protected)
        self.assertEqual(info.current_lba_format.data_size, 0)
        self.assertEqual(info.current_lba_format.relative_performance, 0)

    @tag_test(TestTags.CORE)
    def test_ctrl_info(self):
        """Test controller info"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_controller_info("/dev/nonexistent")

        info = BlockDev.nvme_get_controller_info(self.nvme_dev)
        self.assertEqual(info.ctrl_id, 1)

        self.assertTrue (info.features & BlockDev.NVMEControllerFeature.MULTIPORT)
        self.assertTrue (info.features & BlockDev.NVMEControllerFeature.MULTICTRL)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SRIOV)
        self.assertTrue (info.features & BlockDev.NVMEControllerFeature.ANA_REPORTING)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.FORMAT)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.FORMAT_ALL_NS)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.NS_MGMT)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SELFTEST)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SELFTEST_SINGLE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SANITIZE_CRYPTO)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SANITIZE_BLOCK)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SANITIZE_OVERWRITE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SECURE_ERASE_ALL_NS)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SECURE_ERASE_CRYPTO)
        self.assertEqual(info.fguid, "")
        self.assertEqual(info.hmb_min_size, 0)
        self.assertEqual(info.hmb_pref_size, 0)
        self.assertEqual(info.num_namespaces, 1024)
        self.assertEqual(info.selftest_ext_time, 0)
        self.assertEqual(info.size_total, 0)
        self.assertEqual(info.size_unalloc, 0)
        self.assertEqual(info.subsysnqn, "libblockdev_subnqn")

    @tag_test(TestTags.CORE)
    def test_smart_log(self):
        """Test SMART health log"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_smart_log("/dev/nonexistent")

        log = BlockDev.nvme_get_smart_log(self.nvme_dev)

        self.assertEqual(log.avail_spare, 0)
        self.assertEqual(log.cctemp, 0)
        self.assertEqual(log.critical_temp_time, 0)
        self.assertEqual(log.ctrl_busy_time, 0)
        self.assertEqual(log.media_errors, 0)
        # self.assertEqual(log.num_err_log_entries, 0)
        self.assertEqual(log.percent_used, 0)
        self.assertEqual(log.power_cycles, 0)
        self.assertEqual(log.power_on_hours, 0)
        self.assertEqual(log.spare_thresh, 0)
        self.assertEqual(log.temp_sensors, [-273, -273, -273, -273, -273, -273, -273, -273])
        self.assertEqual(log.temperature, -273)
        self.assertGreater(log.total_data_read, 1)
        self.assertEqual(log.unsafe_shutdowns, 0)
        self.assertEqual(log.warning_temp_time, 0)
        self.assertEqual(log.wctemp, 0)
        self.assertFalse(log.critical_warning & BlockDev.NVMESmartCriticalWarning.SPARE)
        self.assertFalse(log.critical_warning & BlockDev.NVMESmartCriticalWarning.TEMPERATURE)
        self.assertFalse(log.critical_warning & BlockDev.NVMESmartCriticalWarning.DEGRADED)
        self.assertFalse(log.critical_warning & BlockDev.NVMESmartCriticalWarning.READONLY)
        self.assertFalse(log.critical_warning & BlockDev.NVMESmartCriticalWarning.VOLATILE_MEM)
        self.assertFalse(log.critical_warning & BlockDev.NVMESmartCriticalWarning.PMR_READONLY)


    @tag_test(TestTags.CORE)
    def test_error_log(self):
        """Test error log retrieval"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_error_log_entries("/dev/nonexistent")

        log = BlockDev.nvme_get_error_log_entries(self.nvme_dev)
        # expect an empty log...
        self.assertIsNotNone(log)
        self.assertEqual(len(log), 1)
        # TODO: find a way to spoof drive errors


    @tag_test(TestTags.CORE)
    def test_self_test_log(self):
        """Test self-test log retrieval"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_self_test_log("/dev/nonexistent")

        message = r"NVMe Get Log Page - Device Self-test Log command error: Invalid Field in Command: A reserved coded value or an unsupported value in a defined field"
        with self.assertRaisesRegexp(GLib.GError, message):
            # Cannot retrieve self-test log on a nvme target loop devices
            BlockDev.nvme_get_self_test_log(self.nvme_dev)


    @tag_test(TestTags.CORE)
    def test_self_test(self):
        """Test issuing the self-test command"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_device_self_test("/dev/nonexistent", BlockDev.NVMESelfTestAction.SHORT)

        message = r"Invalid value specified for the self-test action"
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.NOT_RUNNING)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.NOT_RUNNING)

        message = r"NVMe Device Self-test command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field"
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.SHORT)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.SHORT)

        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.EXTENDED)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.EXTENDED)

        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.VENDOR_SPECIFIC)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.VENDOR_SPECIFIC)

        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.ABORT)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.ABORT)


    @tag_test(TestTags.CORE)
    def test_format(self):
        """Test issuing the format command"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_format("/dev/nonexistent", 0, BlockDev.NVMEFormatSecureErase.NONE)

        message = r"Couldn't match desired LBA data block size in a device supported LBA format data sizes"
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_ns_dev, 123, BlockDev.NVMEFormatSecureErase.NONE)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_dev, 123, BlockDev.NVMEFormatSecureErase.NONE)

        # format doesn't really work on the kernel loop target
        message = r"Format NVM command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field"
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_ns_dev, 0, BlockDev.NVMEFormatSecureErase.NONE)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_dev, 0, BlockDev.NVMEFormatSecureErase.NONE)
