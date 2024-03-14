import unittest
import os
import re
import shutil
import overrides_hack

from utils import create_sparse_tempfile, create_nvmet_device, delete_nvmet_device, setup_nvme_target, teardown_nvme_target, find_nvme_ctrl_devs_for_subnqn, find_nvme_ns_devs_for_subnqn, get_nvme_hostnqn, run_command, TestTags, tag_test, read_file, write_file

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev

class NVMeTest(unittest.TestCase):
    requested_plugins = BlockDev.plugin_specs_from_names(("nvme", "loop"))

    # kernel nvme target needs disk images on a real filesystem (not tmpfs)
    TMPDIR = "/var/tmp"

    @classmethod
    def setUpClass(cls):
        if not shutil.which("nvme"):
            raise unittest.SkipTest("nvme executable (nvme-cli package) not found in $PATH, skipping.")
        if not shutil.which("nvmetcli"):
            raise unittest.SkipTest("nvmetcli executable not found in $PATH, skipping.")
        ret, out, err = run_command("modprobe nvme-fabrics")
        if ret != 0:
            raise unittest.SkipTest("nvme-fabrics kernel module unavailable, skipping.")

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def _safe_unlink(self, f):
        try:
            os.unlink(f)
        except FileNotFoundError:
            pass


class NVMePluginVersionTestCase(NVMeTest):
    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.NVME), "libbd_nvme.so.3")

    @tag_test(TestTags.NOSTORAGE)
    def test_availability(self):
        avail = BlockDev.nvme_is_tech_avail(BlockDev.NVMETech.NVME, 0)
        self.assertTrue(avail)

        avail = BlockDev.nvme_is_tech_avail(BlockDev.NVMETech.FABRICS, 0)
        self.assertTrue(avail)


class NVMeTestCase(NVMeTest):
    def setUp(self):
        self.dev_file = None
        self.nvme_dev = None
        self.nvme_ns_dev = None

        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("nvme_test", 1024**3, dir=self.TMPDIR)

        self.nvme_dev = create_nvmet_device(self.dev_file)
        self.nvme_ns_dev = self.nvme_dev + "n1"

        os.unlink(self.dev_file)

    def _clean_up(self):
        if self.nvme_dev:
            try:
                delete_nvmet_device(self.nvme_dev)
            except RuntimeError:
                # just move on, we can do no better here
                pass

        if self.dev_file:
            self._safe_unlink(self.dev_file)

    @tag_test(TestTags.CORE)
    def test_ns_info(self):
        """Test namespace info"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_namespace_info("/dev/nonexistent")

        with self.assertRaisesRegex(GLib.GError, r"Error getting Namespace Identifier \(NSID\): Inappropriate ioctl for device"):
            BlockDev.nvme_get_namespace_info(self.nvme_dev)

        # not much information can be gathered from loop-based NVMe target devices...
        info = BlockDev.nvme_get_namespace_info(self.nvme_ns_dev)
        self.assertFalse(info.features & BlockDev.NVMENamespaceFeature.THIN)
        self.assertTrue (info.features & BlockDev.NVMENamespaceFeature.MULTIPATH_SHARED)
        self.assertFalse(info.features & BlockDev.NVMENamespaceFeature.FORMAT_PROGRESS)
        self.assertFalse(info.features & BlockDev.NVMENamespaceFeature.ROTATIONAL)
        self.assertIsNone(info.eui64)
        self.assertEqual(info.format_progress_remaining, 0)
        self.assertEqual(len(info.lba_formats), 1)
        self.assertGreater(len(info.nguid), 0)
        self.assertEqual(info.nsid, 1)
        self.assertEqual(info.ncap, 262144)
        self.assertEqual(info.nsize, 262144)
        self.assertEqual(info.nuse, 262144)
        self.assertGreater(len(info.uuid), 0)
        self.assertFalse(info.write_protected)
        self.assertEqual(info.current_lba_format.data_size, 4096)
        self.assertEqual(info.current_lba_format.metadata_size, 0)
        self.assertEqual(info.current_lba_format.relative_performance, BlockDev.NVMELBAFormatRelativePerformance.BEST)

    @tag_test(TestTags.CORE)
    def test_ctrl_info(self):
        """Test controller info"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_controller_info("/dev/nonexistent")

        info = BlockDev.nvme_get_controller_info(self.nvme_dev)
        self.assertEqual(info.ctrl_id, 1)
        self.assertEqual(info.pci_vendor_id, 0)
        self.assertEqual(info.pci_subsys_vendor_id, 0)
        self.assertIsNone(info.fguid)
        self.assertIn("Linux", info.model_number)
        self.assertGreater(len(info.serial_number), 0)
        self.assertGreater(len(info.firmware_ver), 0)
        self.assertGreater(len(info.nvme_ver), 0)
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
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.STORAGE_DEVICE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.ENCLOSURE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MGMT_PCIE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MGMT_SMBUS)
        self.assertEqual(info.controller_type, BlockDev.NVMEControllerType.IO)
        self.assertEqual(info.selftest_ext_time, 0)
        self.assertEqual(info.hmb_pref_size, 0)
        self.assertEqual(info.hmb_min_size, 0)
        self.assertEqual(info.size_total, 0)
        self.assertEqual(info.size_unalloc, 0)
        self.assertEqual(info.num_namespaces, 1024)
        self.assertEqual(info.subsysnqn, "libblockdev_subnqn")

    @tag_test(TestTags.CORE)
    def test_smart_log(self):
        """Test SMART health log"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_smart_log("/dev/nonexistent")

        log = BlockDev.nvme_get_smart_log(self.nvme_dev)

        self.assertEqual(log.avail_spare, 0)
        self.assertEqual(log.cctemp, 0)
        self.assertEqual(log.critical_temp_time, 0)
        self.assertEqual(log.ctrl_busy_time, 0)
        self.assertEqual(log.media_errors, 0)
        self.assertEqual(log.percent_used, 0)
        self.assertEqual(log.power_cycles, 0)
        self.assertEqual(log.power_on_hours, 0)
        self.assertEqual(log.spare_thresh, 0)
        self.assertEqual(log.temp_sensors, [0, 0, 0, 0, 0, 0, 0, 0])
        self.assertEqual(log.temperature, 0)
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

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_error_log_entries("/dev/nonexistent")

        log = BlockDev.nvme_get_error_log_entries(self.nvme_dev)
        self.assertIsNotNone(log)
        # TODO: find a way to spoof drive errors


    @tag_test(TestTags.CORE)
    def test_self_test_log(self):
        """Test self-test log retrieval"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_self_test_log("/dev/nonexistent")

        message = r"NVMe Get Log Page - Device Self-test Log command error: Invalid Field in Command: A reserved coded value or an unsupported value in a defined field|NVMe Get Log Page - Device Self-test Log command error: unrecognized"
        with self.assertRaisesRegex(GLib.GError, message):
            # Cannot retrieve self-test log on a nvme target loop devices
            BlockDev.nvme_get_self_test_log(self.nvme_dev)

        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.NO_ERROR), "success")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.ABORTED), "aborted")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.CTRL_RESET), "ctrl_reset")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.NS_REMOVED), "ns_removed")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.ABORTED_FORMAT), "aborted_format")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.FATAL_ERROR), "fatal_error")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.UNKNOWN_SEG_FAIL), "unknown_seg_fail")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.KNOWN_SEG_FAIL), "known_seg_fail")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.ABORTED_UNKNOWN), "aborted_unknown")
        self.assertEqual(BlockDev.nvme_self_test_result_to_string(BlockDev.NVMESelfTestResult.ABORTED_SANITIZE), "aborted_sanitize")


    @tag_test(TestTags.CORE)
    def test_self_test(self):
        """Test issuing the self-test command"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_device_self_test("/dev/nonexistent", BlockDev.NVMESelfTestAction.SHORT)

        message = r"Invalid value specified for the self-test action"
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.NOT_RUNNING)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.NOT_RUNNING)

        message = r"NVMe Device Self-test command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field|NVMe Device Self-test command error: Invalid Queue Identifier: The creation of the I/O Completion Queue failed due to an invalid queue identifier specified as part of the command"
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.SHORT)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.SHORT)

        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.EXTENDED)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.EXTENDED)

        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.VENDOR_SPECIFIC)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.VENDOR_SPECIFIC)

        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_dev, BlockDev.NVMESelfTestAction.ABORT)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_device_self_test(self.nvme_ns_dev, BlockDev.NVMESelfTestAction.ABORT)


    @tag_test(TestTags.CORE)
    def test_format(self):
        """Test issuing the format command"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_format("/dev/nonexistent", 0, 0, BlockDev.NVMEFormatSecureErase.NONE)

        message = r"Couldn't match desired LBA data block size in a device supported LBA format data sizes"
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_ns_dev, 123, 0, BlockDev.NVMEFormatSecureErase.NONE)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_dev, 123, 0, BlockDev.NVMEFormatSecureErase.NONE)

        # format doesn't really work on the kernel loop target
        message = r"Format NVM command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field|Format NVM command error: Invalid Queue Identifier: The creation of the I/O Completion Queue failed due to an invalid queue identifier specified as part of the command"
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_ns_dev, 0, 0, BlockDev.NVMEFormatSecureErase.NONE)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_dev, 0, 0, BlockDev.NVMEFormatSecureErase.NONE)


    @tag_test(TestTags.CORE)
    def test_sanitize_log(self):
        """Test sanitize log retrieval"""

        with self.assertRaisesRegex(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_sanitize_log("/dev/nonexistent")

        message = r"NVMe Get Log Page - Sanitize Status Log command error: Invalid Field in Command: A reserved coded value or an unsupported value in a defined field|NVMe Get Log Page - Sanitize Status Log command error: unrecognized"
        with self.assertRaisesRegex(GLib.GError, message):
            # Cannot retrieve sanitize log on a nvme target loop devices
            BlockDev.nvme_get_sanitize_log(self.nvme_dev)
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_get_sanitize_log(self.nvme_ns_dev)


    @tag_test(TestTags.CORE)
    def test_sanitize(self):
        """Test issuing the sanitize command"""

        message = r".*Failed to open device .*': No such file or directory"
        with self.assertRaisesRegex(GLib.GError, message):
            BlockDev.nvme_sanitize("/dev/nonexistent", BlockDev.NVMESanitizeAction.BLOCK_ERASE, False, 0, 0, False)

        message = r"Sanitize command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field|Sanitize command error: Invalid Queue Identifier: The creation of the I/O Completion Queue failed due to an invalid queue identifier specified as part of the command"
        for i in [BlockDev.NVMESanitizeAction.BLOCK_ERASE, BlockDev.NVMESanitizeAction.CRYPTO_ERASE, BlockDev.NVMESanitizeAction.OVERWRITE, BlockDev.NVMESanitizeAction.EXIT_FAILURE]:
            with self.assertRaisesRegex(GLib.GError, message):
                BlockDev.nvme_sanitize(self.nvme_dev, i, False, 0, 0, False)
            with self.assertRaisesRegex(GLib.GError, message):
                BlockDev.nvme_sanitize(self.nvme_ns_dev, i, False, 0, 0, False)


class NVMeFabricsTestCase(NVMeTest):
    SUBNQN = 'libblockdev_nvme'
    DISCOVERY_NQN = 'nqn.2014-08.org.nvmexpress.discovery'

    def setUp(self):
        self.dev_files = []
        self.hostnqn = get_nvme_hostnqn()
        self.have_stable_nqn = os.path.exists('/sys/class/dmi/id/product_uuid')

    def _setup_target(self, num_devices):
        self.addCleanup(self._clean_up)
        for i in range(num_devices):
            self.dev_files += [create_sparse_tempfile("nvmeof_test%d" % i, 1024**3, dir=self.TMPDIR)]
        setup_nvme_target(self.dev_files, self.SUBNQN)
        for d in self.dev_files:
            os.unlink(d)

    def _clean_up(self):
        teardown_nvme_target()
        for d in self.dev_files:
            self._safe_unlink(d)

    def _nvme_disconnect(self, subnqn, ignore_errors=False):
        ret, out, err = run_command("nvme disconnect --nqn=%s" % subnqn)
        if not ignore_errors and (ret != 0 or 'disconnected 0 ' in out):
            raise RuntimeError("Error disconnecting the '%s' subsystem NQN: '%s %s'" % (subnqn, out, err))

    def _get_sysconf_dir(self):
        try:
            makefile = read_file(os.path.join(os.environ['LIBBLOCKDEV_PROJ_DIR'], 'Makefile'))
            r = re.search(r'sysconfdir = (.*)', makefile)
            return r.group(1)
        except:
            return None

    @tag_test(TestTags.CORE)
    def test_connect_single_ns(self):
        """Test simple connect and disconnect"""

        # test that no device node exists for given subsystem nqn
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        # nothing to disconnect
        with self.assertRaisesRegex(GLib.GError, r"No subsystems matching '.*' NQN found."):
            BlockDev.nvme_disconnect(self.SUBNQN)

        # nothing to connect to
        msg = r'Error connecting the controller: '
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, self.hostnqn, None)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.nvme_connect(self.SUBNQN, 'loop', '127.0.0.1', None, None, None, self.hostnqn, None)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, None, None)

        self._setup_target(1)

        # make a connection
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, None, None)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        self.assertTrue(ret)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 1)
        for c in ctrls:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+', c))
            self.assertTrue(os.path.exists(c))
        namespaces = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(namespaces), 1)
        for ns in namespaces:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+n[0-9]+', ns))
            self.assertTrue(os.path.exists(ns))

        # make a duplicate connection
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, None, None)
        self.assertTrue(ret)

        # should see two controllers now
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 2)
        for c in ctrls:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+', c))
            self.assertTrue(os.path.exists(c))

        # disconnect
        with self.assertRaisesRegex(GLib.GError, r"No subsystems matching '.*' NQN found."):
            BlockDev.nvme_disconnect(self.SUBNQN + "xx")
        with self.assertRaisesRegex(GLib.GError, r"No controllers matching the /dev/nvme.*xx device name found."):
            BlockDev.nvme_disconnect_by_path(ctrls[0] + "xx")
        # should disconnect both connections as long the SUBNQN matches
        BlockDev.nvme_disconnect(self.SUBNQN)
        for c in ctrls:
            self.assertFalse(os.path.exists(c))
        for ns in namespaces:
            self.assertFalse(os.path.exists(ns))
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)
        namespaces = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(namespaces), 0)


    @tag_test(TestTags.CORE)
    def test_connect_multiple_ns(self):
        """Test connect and disconnect multiple namespaces"""

        NUM_NS = 3

        # test that no device node exists for given subsystem nqn
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        self._setup_target(NUM_NS)

        # make a connection
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, self.hostnqn, None)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        self.assertTrue(ret)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 1)
        for c in ctrls:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+', c))
            self.assertTrue(os.path.exists(c))
        namespaces = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(namespaces), NUM_NS)
        for ns in namespaces:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+n[0-9]+', ns))
            self.assertTrue(os.path.exists(ns))

            # verify the sysfs paths
            ret, ns_sysfs_path, err = run_command("udevadm info --query=path %s" % ns)
            if ret != 0:
                raise RuntimeError("Error getting udev info for %s: '%s'" % (ns,  err))
            self.assertIsNotNone(ns_sysfs_path)
            self.assertGreater(len(ns_sysfs_path), 0)
            ns_sysfs_path = "/sys" + ns_sysfs_path
            ret, ctrl_sysfs_path, err = run_command("udevadm info --query=path %s" % ctrls[0])
            if ret != 0:
                raise RuntimeError("Error getting udev info for %s: '%s'" % (ctrls[0],  err))
            self.assertIsNotNone(ctrl_sysfs_path)
            self.assertGreater(len(ctrl_sysfs_path), 0)
            ctrl_sysfs_path = "/sys" + ctrl_sysfs_path

            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, None, None, None)
            self.assertIsNotNone(ctrl_sysfs_paths)
            self.assertEqual(len(ctrl_sysfs_paths), 1)
            self.assertEqual(ctrl_sysfs_path, ctrl_sysfs_paths[0])

            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path + "xxx", None, None, None)
            self.assertEqual(len(ctrl_sysfs_paths), 0)
            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, self.SUBNQN, None, None)
            self.assertEqual(len(ctrl_sysfs_paths), 1)
            self.assertEqual(ctrl_sysfs_path, ctrl_sysfs_paths[0])
            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, self.SUBNQN, self.hostnqn, None)
            self.assertEqual(len(ctrl_sysfs_paths), 1)
            self.assertEqual(ctrl_sysfs_path, ctrl_sysfs_paths[0])

            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, "unknownsubsysnqn", None, None)
            self.assertEqual(len(ctrl_sysfs_paths), 0)
            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, None, "unknownhostnqn", None)
            self.assertEqual(len(ctrl_sysfs_paths), 0)
            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, self.SUBNQN, "unknownhostnqn", None)
            self.assertEqual(len(ctrl_sysfs_paths), 0)
            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, None, None, "unknownhostid")
            self.assertEqual(len(ctrl_sysfs_paths), 0)
            ctrl_sysfs_paths = BlockDev.nvme_find_ctrls_for_ns(ns_sysfs_path, self.SUBNQN, self.hostnqn, "unknownhostid")
            self.assertEqual(len(ctrl_sysfs_paths), 0)

        # disconnect
        BlockDev.nvme_disconnect_by_path(ctrls[0])
        for c in ctrls:
            self.assertFalse(os.path.exists(c))
        for ns in namespaces:
            self.assertFalse(os.path.exists(ns))
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)
        namespaces = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(namespaces), 0)


    @tag_test(TestTags.CORE)
    def test_host_nqn(self):
        """Test Host NQN/ID manipulation and a simple connect"""

        HOSTNQN_PATH = '/etc/nvme/hostnqn'
        HOSTID_PATH = '/etc/nvme/hostid'
        FAKE_HOSTNQN1 = 'nqn.2014-08.org.nvmexpress:uuid:ffffffff-ffff-ffff-ffff-ffffffffffff'
        FAKE_HOSTNQN2 = 'nqn.2014-08.org.nvmexpress:uuid:beefbeef-beef-beef-beef-beefdeadbeef'
        FAKE_HOSTID1 = 'aaaaaaaa-ffff-ffff-ffff-ffffffffffff'
        FAKE_HOSTID2 = 'beeeeeef-beef-beef-beef-beefdeadbeef'

        # libnvme might have been configured with a different prefix than libblockdev
        sysconf_dir = self._get_sysconf_dir()
        if sysconf_dir != '/etc':
            self.skipTest("libblockdev was not configured with standard prefix (/usr)")

        # test that no device node exists for given subsystem nqn
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        # save hostnqn and hostid files
        try:
            saved_hostnqn = read_file(HOSTNQN_PATH)
            self.addCleanup(write_file, HOSTNQN_PATH, saved_hostnqn)
        except:
            self.addCleanup(self._safe_unlink, HOSTNQN_PATH)
            pass
        try:
            saved_hostid = read_file(HOSTID_PATH)
            self.addCleanup(write_file, HOSTID_PATH, saved_hostid)
        except:
            self.addCleanup(self._safe_unlink, HOSTID_PATH)
            pass

        self._safe_unlink(HOSTNQN_PATH)
        self._safe_unlink(HOSTID_PATH)
        hostnqn = BlockDev.nvme_get_host_nqn()
        self.assertEqual(len(hostnqn), 0)
        hostnqn = BlockDev.nvme_generate_host_nqn()
        self.assertTrue(hostnqn.startswith('nqn.2014-08.org.nvmexpress:uuid:'))
        hostid = BlockDev.nvme_get_host_id()
        self.assertEqual(len(hostid), 0)

        # connection without hostnqn set
        self._setup_target(1)
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, None, None)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        self.assertTrue(ret)
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 1)
        if self.have_stable_nqn:
            sysfs_hostnqn = read_file('/sys/class/nvme/%s/hostnqn' % os.path.basename(ctrls[0]))
            self.assertEqual(sysfs_hostnqn.strip(), BlockDev.nvme_generate_host_nqn())
        BlockDev.nvme_disconnect(self.SUBNQN)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, FAKE_HOSTNQN1, FAKE_HOSTID1)
        self.assertTrue(ret)
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 1)
        sysfs_hostnqn = read_file('/sys/class/nvme/%s/hostnqn' % os.path.basename(ctrls[0]))
        sysfs_hostid = read_file('/sys/class/nvme/%s/hostid' % os.path.basename(ctrls[0]))
        self.assertEqual(sysfs_hostnqn.strip(), FAKE_HOSTNQN1)
        self.assertEqual(sysfs_hostid.strip(), FAKE_HOSTID1)
        BlockDev.nvme_disconnect(self.SUBNQN)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        # fill with custom IDs
        ret = BlockDev.nvme_set_host_nqn(FAKE_HOSTNQN1)
        self.assertTrue(ret)
        ret = BlockDev.nvme_set_host_id(FAKE_HOSTID1)
        self.assertTrue(ret)
        hostnqn = BlockDev.nvme_get_host_nqn()
        self.assertEqual(hostnqn, FAKE_HOSTNQN1)
        hostid = BlockDev.nvme_get_host_id()
        self.assertEqual(hostid, FAKE_HOSTID1)

        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, None, None)
        self.assertTrue(ret)
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 1)
        sysfs_hostnqn = read_file('/sys/class/nvme/%s/hostnqn' % os.path.basename(ctrls[0]))
        sysfs_hostid = read_file('/sys/class/nvme/%s/hostid' % os.path.basename(ctrls[0]))
        self.assertEqual(sysfs_hostnqn.strip(), FAKE_HOSTNQN1)
        self.assertEqual(sysfs_hostid.strip(), FAKE_HOSTID1)
        BlockDev.nvme_disconnect(self.SUBNQN)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, FAKE_HOSTNQN2, FAKE_HOSTID2)
        self.assertTrue(ret)
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 1)
        sysfs_hostnqn = read_file('/sys/class/nvme/%s/hostnqn' % os.path.basename(ctrls[0]))
        sysfs_hostid = read_file('/sys/class/nvme/%s/hostid' % os.path.basename(ctrls[0]))
        self.assertEqual(sysfs_hostnqn.strip(), FAKE_HOSTNQN2)
        self.assertEqual(sysfs_hostid.strip(), FAKE_HOSTID2)
        BlockDev.nvme_disconnect(self.SUBNQN)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        self._safe_unlink(HOSTNQN_PATH)
        self._safe_unlink(HOSTID_PATH)


    @tag_test(TestTags.CORE)
    def test_persistent_dc(self):
        """Test connecting a persistent Discovery Controller"""

        # test that no device node exists for given subsystem nqn
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 0)

        # nothing to disconnect
        with self.assertRaisesRegex(GLib.GError, r"No subsystems matching '.*' NQN found."):
            BlockDev.nvme_disconnect(self.DISCOVERY_NQN)

        # nothing to connect to
        msg = r'Error connecting the controller: '
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.nvme_connect(self.DISCOVERY_NQN, 'loop', None, None, None, None, self.hostnqn, None)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.nvme_connect(self.DISCOVERY_NQN, 'loop', '127.0.0.1', None, None, None, self.hostnqn, None)
        with self.assertRaisesRegex(GLib.GError, msg):
            BlockDev.nvme_connect(self.DISCOVERY_NQN, 'loop', None, None, None, None, None, None)

        self._setup_target(1)

        # make a connection
        ret = BlockDev.nvme_connect(self.DISCOVERY_NQN, 'loop', None, None, None, None, None, None)
        # TODO: there are some extra steps to make the DC actually persistent, a simple
        # connect like this appears to be working fine for our needs though.
        self.addCleanup(self._nvme_disconnect, self.DISCOVERY_NQN, ignore_errors=True)
        self.assertTrue(ret)

        ctrls = find_nvme_ctrl_devs_for_subnqn(self.DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 1)
        for c in ctrls:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+', c))
            self.assertTrue(os.path.exists(c))

        namespaces = find_nvme_ns_devs_for_subnqn(self.DISCOVERY_NQN)
        self.assertEqual(len(namespaces), 0)

        # issue an IDENTIFY_CTRL command
        info = BlockDev.nvme_get_controller_info(ctrls[0])
        self.assertEqual(info.ctrl_id, 1)
        self.assertEqual(info.pci_vendor_id, 0)
        self.assertEqual(info.pci_subsys_vendor_id, 0)
        self.assertIsNone(info.fguid)
        self.assertIn("Linux", info.model_number)
        self.assertGreater(len(info.serial_number), 0)
        self.assertGreater(len(info.firmware_ver), 0)
        self.assertGreater(len(info.nvme_ver), 0)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MULTIPORT)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MULTICTRL)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.SRIOV)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.ANA_REPORTING)
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
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.STORAGE_DEVICE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.ENCLOSURE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MGMT_PCIE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MGMT_SMBUS)
        self.assertEqual(info.controller_type, BlockDev.NVMEControllerType.DISCOVERY)
        self.assertEqual(info.selftest_ext_time, 0)
        self.assertEqual(info.hmb_pref_size, 0)
        self.assertEqual(info.hmb_min_size, 0)
        self.assertEqual(info.size_total, 0)
        self.assertEqual(info.size_unalloc, 0)
        self.assertEqual(info.num_namespaces, 0)
        self.assertEqual(info.subsysnqn, self.DISCOVERY_NQN)

        # disconnect
        with self.assertRaisesRegex(GLib.GError, r"No subsystems matching '.*' NQN found."):
            BlockDev.nvme_disconnect(self.DISCOVERY_NQN + "xx")
        with self.assertRaisesRegex(GLib.GError, r"No controllers matching the /dev/nvme.*xx device name found."):
            BlockDev.nvme_disconnect_by_path(ctrls[0] + "xx")
        BlockDev.nvme_disconnect(self.DISCOVERY_NQN)
        for c in ctrls:
            self.assertFalse(os.path.exists(c))
        for ns in namespaces:
            self.assertFalse(os.path.exists(ns))
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 0)
        namespaces = find_nvme_ns_devs_for_subnqn(self.DISCOVERY_NQN)
        self.assertEqual(len(namespaces), 0)
