import unittest
import os
import re
import overrides_hack

from utils import create_sparse_tempfile, create_nvmet_device, delete_nvmet_device, setup_nvme_target, teardown_nvme_target, find_nvme_ctrl_devs_for_subnqn, find_nvme_ns_devs_for_subnqn, run_command, TestTags, tag_test
from gi.repository import BlockDev, GLib
from distutils.spawn import find_executable


class NVMeTest(unittest.TestCase):
    requested_plugins = BlockDev.plugin_specs_from_names(("nvme", "loop"))

    @classmethod
    def setUpClass(cls):
        if not find_executable("nvme"):
            raise unittest.SkipTest("nvme executable (nvme-cli package) not found in $PATH, skipping.")
        if not find_executable("nvmetcli"):
            raise unittest.SkipTest("nvmetcli executable not found in $PATH, skipping.")

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)


class NVMeTestCase(NVMeTest):
    def setUp(self):
        self.dev_file = None
        self.loop_dev = None
        self.nvme_dev = None
        self.nvme_ns_dev = None

        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("nvme_test", 1024**3)

        ret, loop = BlockDev.loop_setup(self.dev_file)
        if not ret:
            raise RuntimeError("Failed to setup loop device %s for testing" % self.dev_file)
        self.loop_dev = "/dev/%s" % loop

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
        BlockDev.loop_teardown(self.loop_dev)
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
        self.assertEqual(len(info.lba_formats), 1)
        self.assertGreater(len(info.nguid), 0)
        self.assertEqual(info.nsid, 1)
        self.assertEqual(info.ncap, 2097152)
        self.assertEqual(info.nsize, 2097152)
        self.assertEqual(info.nuse, 2097152)
        self.assertGreater(len(info.uuid), 0)
        self.assertFalse(info.write_protected)
        self.assertEqual(info.current_lba_format.data_size, 512)
        self.assertEqual(info.current_lba_format.relative_performance, BlockDev.NVMELBAFormatRelativePerformance.BEST)

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
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.STORAGE_DEVICE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.ENCLOSURE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MGMT_PCIE)
        self.assertFalse(info.features & BlockDev.NVMEControllerFeature.MGMT_SMBUS)
        self.assertEqual(info.fguid, "")
        self.assertEqual(info.pci_vendor_id, 0)
        self.assertEqual(info.pci_subsys_vendor_id, 0)
        self.assertIn("Linux", info.model_number)
        self.assertGreater(len(info.serial_number), 0)
        self.assertGreater(len(info.firmware_ver), 0)
        self.assertGreater(len(info.nvme_ver), 0)
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

        message = r"NVMe Get Log Page - Device Self-test Log command error: Invalid Field in Command: A reserved coded value or an unsupported value in a defined field|NVMe Get Log Page - Device Self-test Log command error: unrecognized"
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

        message = r"NVMe Device Self-test command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field|NVMe Device Self-test command error: Invalid Queue Identifier: The creation of the I/O Completion Queue failed due to an invalid queue identifier specified as part of the command"
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
        message = r"Format NVM command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field|Format NVM command error: Invalid Queue Identifier: The creation of the I/O Completion Queue failed due to an invalid queue identifier specified as part of the command"
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_ns_dev, 0, BlockDev.NVMEFormatSecureErase.NONE)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_format(self.nvme_dev, 0, BlockDev.NVMEFormatSecureErase.NONE)


    @tag_test(TestTags.CORE)
    def test_sanitize_log(self):
        """Test sanitize log retrieval"""

        with self.assertRaisesRegexp(GLib.GError, r".*Failed to open device .*': No such file or directory"):
            BlockDev.nvme_get_sanitize_log("/dev/nonexistent")

        message = r"NVMe Get Log Page - Sanitize Status Log command error: Invalid Field in Command: A reserved coded value or an unsupported value in a defined field|NVMe Get Log Page - Sanitize Status Log command error: unrecognized"
        with self.assertRaisesRegexp(GLib.GError, message):
            # Cannot retrieve sanitize log on a nvme target loop devices
            BlockDev.nvme_get_sanitize_log(self.nvme_dev)
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_get_sanitize_log(self.nvme_ns_dev)


    @tag_test(TestTags.CORE)
    def test_sanitize(self):
        """Test issuing the sanitize command"""

        message = r".*Failed to open device .*': No such file or directory"
        with self.assertRaisesRegexp(GLib.GError, message):
            BlockDev.nvme_sanitize("/dev/nonexistent", BlockDev.NVMESanitizeAction.BLOCK_ERASE, False, 0, 0, False)

        message = r"Sanitize command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field|Sanitize command error: Invalid Queue Identifier: The creation of the I/O Completion Queue failed due to an invalid queue identifier specified as part of the command"
        for i in [BlockDev.NVMESanitizeAction.BLOCK_ERASE, BlockDev.NVMESanitizeAction.CRYPTO_ERASE, BlockDev.NVMESanitizeAction.OVERWRITE, BlockDev.NVMESanitizeAction.EXIT_FAILURE]:
            with self.assertRaisesRegexp(GLib.GError, message):
                BlockDev.nvme_sanitize(self.nvme_dev, i, False, 0, 0, False)
            with self.assertRaisesRegexp(GLib.GError, message):
                BlockDev.nvme_sanitize(self.nvme_ns_dev, i, False, 0, 0, False)


class NVMeFabricsTestCase(NVMeTest):
    HOSTNQN = 'libblockdev_nvmeof_hostnqn'
    SUBNQN = 'libblockdev_nvmeof_subnqn'

    def setUp(self):
        self.loop_devs = []
        self.dev_files = []

    def _setup_target(self, num_devices):
        self.addCleanup(self._clean_up)
        for i in range(num_devices):
            self.dev_files += [create_sparse_tempfile("nvmeof_test%d" % i, 1024**3)]

            ret, loop = BlockDev.loop_setup(self.dev_files[i])
            if not ret:
                raise RuntimeError("Failed to setup loop device %s for testing" % self.dev_files[i])
            self.loop_devs += ["/dev/%s" % loop]
        setup_nvme_target(self.loop_devs, self.SUBNQN, self.HOSTNQN)

    def _clean_up(self):
        teardown_nvme_target()

        # detach loop devices
        for i in self.loop_devs:
            BlockDev.loop_teardown(i)
        for i in self.dev_files:
            os.unlink(i)

    def _nvme_disconnect(self, subnqn, ignore_errors=False):
        ret, out, err = run_command("nvme disconnect --nqn=%s" % subnqn)
        if not ignore_errors and (ret != 0 or 'disconnected 0 ' in out):
            raise RuntimeError("Error disconnecting the '%s' subsystem NQN: '%s %s'" % (subnqn, out, err))

    @tag_test(TestTags.CORE)
    def test_connect_single_ns(self):
        """Test simple connect and disconnect"""

        # test that no device node exists for given subsystem nqn
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 0)

        # nothing to disconnect
        with self.assertRaisesRegexp(GLib.GError, r"No subsystems matching '.*' NQN found."):
            BlockDev.nvme_disconnect(self.SUBNQN)

        # nothing to connect to
        with self.assertRaisesRegexp(GLib.GError, r"Error connecting the controller: "):
            BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, self.HOSTNQN, None)

        self._setup_target(1)

        # make a connection
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, self.HOSTNQN, None)
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
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, self.HOSTNQN, None)
        self.assertTrue(ret)

        # should see two controllers now
        ctrls = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrls), 2)
        for c in ctrls:
            self.assertTrue(re.match(r'/dev/nvme[0-9]+', c))
            self.assertTrue(os.path.exists(c))

        # disconnect
        with self.assertRaisesRegexp(GLib.GError, r"No subsystems matching '.*' NQN found."):
            BlockDev.nvme_disconnect(self.SUBNQN + "xx")
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
        ret = BlockDev.nvme_connect(self.SUBNQN, 'loop', None, None, None, None, self.HOSTNQN, None)
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

        # disconnect
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
    def test_discovery(self):
        """Test discovery"""

        DISCOVERY_NQN = 'nqn.2014-08.org.nvmexpress.discovery'

        # nvme target unavailable
        ctrls = find_nvme_ctrl_devs_for_subnqn(DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 0)
        with self.assertRaisesRegexp(GLib.GError, r"Invalid discovery controller device specified"):
            BlockDev.nvme_discover('nonsense', True, 'loop', None, None, None, None, self.HOSTNQN, None)
        with self.assertRaisesRegexp(GLib.GError, r"Couldn't access the discovery controller device specified"):
            BlockDev.nvme_discover('/dev/nvmenonsense', True, 'loop', None, None, None, None, self.HOSTNQN, None)
        with self.assertRaisesRegexp(GLib.GError, r"Error connecting the controller: (Input/output error|No such file or directory|failed to write to nvme-fabrics device)"):
            BlockDev.nvme_discover(None, False, 'loop', None, None, None, None, self.HOSTNQN, None)

        self._setup_target(1)

        # non-persistent discovery connection
        entries = BlockDev.nvme_discover(None, False, 'loop', None, None, None, None, self.HOSTNQN, None)
        self.assertGreater(len(entries), 0)
        self.assertEqual(entries[0].transport_type, BlockDev.NVMETransportType.LOOP)
        self.assertEqual(entries[0].address_family, BlockDev.NVMEAddressFamily.PCI)
        self.assertEqual(entries[0].port_id, 1)
        self.assertEqual(entries[0].ctrl_id, 65535)
        self.assertIn(self.SUBNQN, [entry.subsys_nqn for entry in entries])
        self.assertEqual(entries[0].tcp_security, BlockDev.NVMETCPSecurity.NONE)
        ctrls = find_nvme_ctrl_devs_for_subnqn(DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 0)

        # persistent discovery connection
        entries = BlockDev.nvme_discover(None, True, 'loop', None, None, None, None, self.HOSTNQN, None)
        self.addCleanup(self._nvme_disconnect, DISCOVERY_NQN, ignore_errors=True)
        self.assertGreater(len(entries), 0)
        self.assertIn(self.SUBNQN, [entry.subsys_nqn for entry in entries])
        ctrls = find_nvme_ctrl_devs_for_subnqn(DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 1)

        # reuse the persistent connection
        entries = BlockDev.nvme_discover(ctrls[0], False, 'loop', None, None, None, None, self.HOSTNQN, None)
        self.assertGreater(len(entries), 0)
        self.assertIn(self.SUBNQN, [entry.subsys_nqn for entry in entries])
        ctrls = find_nvme_ctrl_devs_for_subnqn(DISCOVERY_NQN)
        self.assertEqual(len(ctrls), 1)

        # close the persistent connection
        BlockDev.nvme_disconnect(DISCOVERY_NQN)
