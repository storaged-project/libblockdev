import unittest
import re
import overrides_hack
from utils import fake_utils

from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class UtilsExecLoggingTest(unittest.TestCase):
    log = ""

    def my_log_func(self, level, msg):
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    def test_logging(self):
        """Verify that setting up and using exec logging works as expected"""

        succ = BlockDev.utils_init_logging(self.my_log_func)
        self.assertTrue(succ)

        succ = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)

        succ, out = BlockDev.utils_exec_and_capture_output(["echo", "hi"])
        self.assertTrue(succ)
        self.assertEqual(out, "hi\n")

        match = re.search(r'Running \[(\d+)\] true', self.log)
        self.assertIsNot(match, None)
        task_id1 = match.group(1)
        match = re.search(r'Running \[(\d+)\] echo hi', self.log)
        self.assertIsNot(match, None)
        task_id2 = match.group(1)

        self.assertIn("...done [%s] (exit code: 0)" % task_id1, self.log)
        self.assertIn("stdout[%s]:" % task_id1, self.log)
        self.assertIn("stderr[%s]:" % task_id1, self.log)

        self.assertIn("stdout[%s]: hi" % task_id2, self.log)
        self.assertIn("stderr[%s]:" % task_id2, self.log)
        self.assertIn("...done [%s] (exit code: 0)" % task_id2, self.log)

        # reset logging -> nothing more should appear in the log
        succ = BlockDev.utils_init_logging(None)
        self.assertTrue(succ)

        old_log = self.log
        succ = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)
        self.assertEqual(old_log, self.log)

    def test_version_cmp(self):
        """Verify that version comparison works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.utils_version_cmp("malformed", "1.0")
        with self.assertRaises(GLib.GError):
            BlockDev.utils_version_cmp("1.0", "malformed")
        with self.assertRaises(GLib.GError):
            BlockDev.utils_version_cmp("1,0", "1.0")
        with self.assertRaises(GLib.GError):
            BlockDev.utils_version_cmp("1.0", "1,0")
        with self.assertRaises(GLib.GError):
            BlockDev.utils_version_cmp("1.x.0", "1.0")
        with self.assertRaises(GLib.GError):
            BlockDev.utils_version_cmp("1.0", "1.x.0")

        self.assertEqual(BlockDev.utils_version_cmp("1", "1"), 0)
        self.assertEqual(BlockDev.utils_version_cmp("1.0", "1.0"), 0)
        self.assertEqual(BlockDev.utils_version_cmp("1.0.1", "1.0.1"), 0)
        self.assertEqual(BlockDev.utils_version_cmp("1.0.1-1", "1.0.1-1"), 0)

        self.assertEqual(BlockDev.utils_version_cmp("1.1", "1"), 1)
        self.assertEqual(BlockDev.utils_version_cmp("1.1", "1.0"), 1)
        self.assertEqual(BlockDev.utils_version_cmp("1.1.1", "1.1"), 1)
        self.assertEqual(BlockDev.utils_version_cmp("1.1.1-1", "1.1.1"), 1)
        self.assertEqual(BlockDev.utils_version_cmp("1.2", "1.1.2"), 1)

        self.assertEqual(BlockDev.utils_version_cmp("1", "1.1"), -1)
        self.assertEqual(BlockDev.utils_version_cmp("1.0", "1.1"), -1)
        self.assertEqual(BlockDev.utils_version_cmp("1.1", "1.1.1"), -1)
        self.assertEqual(BlockDev.utils_version_cmp("1.1.1", "1.1.1-1"), -1)
        self.assertEqual(BlockDev.utils_version_cmp("1.1.2", "1.2"), -1)

    def test_util_version(self):
        """Verify that checking utility availability works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.utils_check_util_version("libblockdev-fake-util", None, None, None)

        with fake_utils("tests/utils_fake_util/"):
            with self.assertRaises(GLib.GError):
                # with no argument, the output is "Version: 1.2" which is not a
                # valid version without regexp
                BlockDev.utils_check_util_version("libblockdev-fake-util", "1.3", "", None)

            with self.assertRaises(GLib.GError):
                # libblockdev-fake-util with no arguments reports 1.2 which is too low
                BlockDev.utils_check_util_version("libblockdev-fake-util", "1.3", "", "Version:\\s(.*)")

            # just what we require
            self.assertTrue(BlockDev.utils_check_util_version("libblockdev-fake-util", "1.2", "", "Version:\\s(.*)"))

            with self.assertRaises(GLib.GError):
                # libblockdev-fake-util with "version" reports 1.1 which is too low
                BlockDev.utils_check_util_version("libblockdev-fake-util", "1.2", "version", "Version:\\s(.*)")

            # just what we require
            self.assertTrue(BlockDev.utils_check_util_version("libblockdev-fake-util", "1.1", "version", "Version:\\s(.*)"))

            with self.assertRaises(GLib.GError):
                # libblockdev-fake-util with "--version" reports 1.0 which is too low
                BlockDev.utils_check_util_version("libblockdev-fake-util", "1.1", "--version", None)

            # just what we require
            self.assertTrue(BlockDev.utils_check_util_version("libblockdev-fake-util", "1.0", "--version", None))

            # lower required version
            self.assertTrue(BlockDev.utils_check_util_version("libblockdev-fake-util", "0.9", "--version", None))
