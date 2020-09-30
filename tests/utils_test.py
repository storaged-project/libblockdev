import unittest
import re
import os
import six
import overrides_hack
from utils import fake_utils, create_sparse_tempfile, create_lio_device, delete_lio_device, run_command, TestTags, tag_test, read_file

from gi.repository import BlockDev, GLib


class UtilsTestCase(unittest.TestCase):

    requested_plugins = []

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

class UtilsExecProgressTest(UtilsTestCase):
    log = []

    def my_progress_func(self, task, status, completion, msg):
        self.assertTrue(isinstance(completion, int))
        self.log.append(completion)

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_initialization(self):
        """ Verify that progress report can (de)initialized"""

        succ = BlockDev.utils_prog_reporting_initialized()
        self.assertFalse(succ)

        succ = BlockDev.utils_init_prog_reporting(self.my_progress_func)
        self.assertTrue(succ)

        succ = BlockDev.utils_prog_reporting_initialized()
        self.assertTrue(succ)

        succ = BlockDev.utils_init_prog_reporting(None)
        self.assertTrue(succ)

        succ = BlockDev.utils_prog_reporting_initialized()
        self.assertFalse(succ)


class UtilsExecLoggingTest(UtilsTestCase):
    log = ""

    def my_log_func(self, level, msg):
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_logging(self):
        """Verify that setting up and using exec logging works as expected"""

        succ = BlockDev.utils_init_logging(self.my_log_func)
        self.assertTrue(succ)

        succ = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, r"Process reported exit code 1"):
            succ = BlockDev.utils_exec_and_report_error(["/bin/false"])

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

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
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

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
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

            # version on stderr
            self.assertTrue(BlockDev.utils_check_util_version("libblockdev-fake-util-stderr", "1.1", "version", "Version:\\s(.*)"))

            # exit code != 0
            self.assertTrue(BlockDev.utils_check_util_version("libblockdev-fake-util-fail", "1.1", "version", "Version:\\s(.*)"))

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_exec_locale(self):
        """Verify that setting locale for exec functions works as expected"""

        succ, out = BlockDev.utils_exec_and_capture_output(["locale"])
        self.assertTrue(succ)
        self.assertIn("LC_ALL=C", out)

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_exec_buffer_bloat(self):
        """Verify that very large output from a command is handled properly"""

        # easy 64kB of data
        cnt = 65536
        succ, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "for i in {1..%d}; do echo -n .; done" % cnt])
        self.assertTrue(succ)
        self.assertEquals(len(out), cnt)

        succ, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "for i in {1..%d}; do echo -n .; echo -n \# >&2; done" % cnt])
        self.assertTrue(succ)
        self.assertEquals(len(out), cnt)

        # now exceed the system pipe buffer size
        # pipe(7): The maximum size (in bytes) of individual pipes that can be set by users without the CAP_SYS_RESOURCE capability.
        cnt = int(read_file("/proc/sys/fs/pipe-max-size")) + 100
        self.assertGreater(cnt, 0)

        succ, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "for i in {1..%d}; do echo -n .; done" % cnt])
        self.assertTrue(succ)
        self.assertEquals(len(out), cnt)

        succ, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "for i in {1..%d}; do echo -n .; echo -n \# >&2; done" % cnt])
        self.assertTrue(succ)
        self.assertEquals(len(out), cnt)

        # make use of some newlines
        succ, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "for i in {1..%d}; do echo -n .; if [ $(($i%%500)) -eq 0 ]; then echo ''; fi; done" % cnt])
        self.assertTrue(succ)
        self.assertGreater(len(out), cnt)

        succ, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "for i in {1..%d}; do echo -n .; echo -n \# >&2; if [ $(($i%%500)) -eq 0 ]; then echo ''; echo '' >&2; fi; done" % cnt])
        self.assertTrue(succ)
        self.assertGreater(len(out), cnt)


    EXEC_PROGRESS_MSG = "Aloha, I'm the progress line you should match."

    def my_exec_progress_func_concat(self, line):
        """Expect an concatenated string"""
        s = ""
        for i in range(10):
            s += self.EXEC_PROGRESS_MSG
        self.assertEqual(line, s)
        self.num_matches += 1
        return 0

    def my_exec_progress_func(self, line):
        self.assertTrue(re.match(r".*%s.*" % self.EXEC_PROGRESS_MSG, line))
        self.num_matches += 1
        return 0

    def test_exec_buffer_bloat_progress(self):
        """Verify that progress reporting can handle large output"""

        # no newlines, should match just a single occurrence on EOF
        cnt = 10
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..10}; do echo -n \"%s\"; done" % self.EXEC_PROGRESS_MSG], None, self.my_exec_progress_func_concat)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, 1)

        # ten matches
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo \"%s\"; done" % (cnt, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt)

        # 100k matches
        cnt = 100000
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo \"%s\"; done" % (cnt, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt)

        # 100k matches on stderr
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo \"%s\" >&2; done" % (cnt, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt)

        # 100k matches on stderr and stdout
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo \"%s\"; echo \"%s\" >&2; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt * 2)

        # stdout and stderr output, non-zero return from the command and very long exception message
        self.num_matches = 0
        with six.assertRaisesRegex(self, GLib.GError, r"Process reported exit code 66"):
            status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo \"%s\"; echo \"%s\" >&2; done; exit 66" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertEqual(self.num_matches, cnt * 2)

        # no progress reporting callback given, tests slightly different code path
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo \"%s\"; echo \"%s\" >&2; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, None)
        self.assertTrue(status)

    def test_exec_null_bytes(self):
        """Verify that null bytes in the output are processed properly"""

        TEST_DATA = ["First line", "Second line", "Third line"]

        status, out = BlockDev.utils_exec_and_capture_output(["bash", "-c", "echo -e \"%s\\0%s\\n%s\"" % (TEST_DATA[0], TEST_DATA[1], TEST_DATA[2])])
        self.assertTrue(status)
        self.assertTrue(TEST_DATA[0] in out)
        self.assertTrue(TEST_DATA[1] in out)
        self.assertTrue(TEST_DATA[2] in out)
        self.assertFalse("kuku!" in out)

        # ten matches
        cnt = 10
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo -e \"%s\\0%s\"; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt * 2)

        # 100k matches
        cnt = 100000
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo -e \"%s\\0%s\"; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt * 2)

        # 100k matches on stderr
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo -e \"%s\\0%s\" >&2; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt * 2)

        # 100k matches on stderr and stdout
        self.num_matches = 0
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo -e \"%s\\0%s\"; echo -e \"%s\\0%s\" >&2; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertTrue(status)
        self.assertEqual(self.num_matches, cnt * 4)

        # stdout and stderr output, non-zero return from the command and very long exception message
        self.num_matches = 0
        with six.assertRaisesRegex(self, GLib.GError, r"Process reported exit code 66"):
            status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo -e \"%s\\0%s\"; echo -e \"%s\\0%s\" >&2; done; exit 66" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, self.my_exec_progress_func)
        self.assertEqual(self.num_matches, cnt * 4)

        # no progress reporting callback given, tests slightly different code path
        status = BlockDev.utils_exec_and_report_progress(["bash", "-c", "for i in {1..%d}; do echo -e \"%s\\0%s\"; echo -e \"%s\\0%s\" >&2; done" % (cnt, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG, self.EXEC_PROGRESS_MSG)], None, None)
        self.assertTrue(status)


class UtilsDevUtilsTestCase(UtilsTestCase):
    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_resolve_device(self):
        """Verify that resolving device spec works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.utils_resolve_device("no_such_device")

        dev = "/dev/libblockdev-test-dev"
        with open(dev, "w"):
            pass
        self.addCleanup(os.unlink, dev)

        # full path, no symlink, should just return the same
        self.assertEqual(BlockDev.utils_resolve_device(dev), dev)

        # just the name of the device, should return the full path
        self.assertEqual(BlockDev.utils_resolve_device(dev[5:]), dev)

        dev_dir = "/dev/libblockdev-test-dir"
        os.mkdir(dev_dir)
        self.addCleanup(os.rmdir, dev_dir)

        dev_link = dev_dir + "/test-dev-link"
        os.symlink("../" + dev[5:], dev_link)
        self.addCleanup(os.unlink, dev_link)

        # should resolve the symlink
        self.assertEqual(BlockDev.utils_resolve_device(dev_link), dev)

        # should resolve the symlink even without the "/dev" prefix
        self.assertEqual(BlockDev.utils_resolve_device(dev_link[5:]), dev)


class UtilsDevUtilsSymlinksTestCase(UtilsTestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("lvm_test", 1024**3)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)

    @tag_test(TestTags.CORE)
    def test_get_device_symlinks(self):
        """Verify that getting device symlinks works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.utils_get_device_symlinks("no_such_device")

        symlinks = BlockDev.utils_get_device_symlinks(self.loop_dev)
        # there should be at least 2 symlinks for something like "/dev/sda" (in /dev/disk/by-id/)
        self.assertGreaterEqual(len(symlinks), 2)

        symlinks = BlockDev.utils_get_device_symlinks(self.loop_dev[5:])
        self.assertGreaterEqual(len(symlinks), 2)

        # create an LV to get a device with more symlinks
        ret, _out, _err = run_command ("pvcreate %s" % self.loop_dev)
        self.assertEqual(ret, 0)
        self.addCleanup(run_command, "pvremove %s" % self.loop_dev)

        ret, _out, _err = run_command ("vgcreate utilsTestVG %s" % self.loop_dev)
        self.assertEqual(ret, 0)
        self.addCleanup(run_command, "vgremove -y utilsTestVG")

        ret, _out, _err = run_command ("lvcreate -n utilsTestLV -L 12M utilsTestVG")
        self.assertEqual(ret, 0)
        self.addCleanup(run_command, "lvremove -y utilsTestVG/utilsTestLV")

        symlinks = BlockDev.utils_get_device_symlinks("utilsTestVG/utilsTestLV")
        # there should be at least 4 symlinks for an LV
        self.assertGreaterEqual(len(symlinks), 4)


class UtilsLinuxKernelVersionTest(UtilsTestCase):
    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_initialization(self):
        """ Test Linux kernel version detection"""

        ver = BlockDev.utils_get_linux_version()
        self.assertGreater(ver.major, 0)

        ret = BlockDev.utils_check_linux_version(ver.major, ver.minor, ver.micro)
        self.assertEqual(ret, 0)

        ret = BlockDev.utils_check_linux_version(ver.major - 1, ver.minor, ver.micro)
        self.assertGreater(ret, 0)

        ret = BlockDev.utils_check_linux_version(ver.major + 1, ver.minor, ver.micro)
        self.assertLess(ret, 0)

        ver2 = BlockDev.utils_get_linux_version()
        self.assertEqual(ver.major, ver2.major)
        self.assertEqual(ver.minor, ver2.minor)
        self.assertEqual(ver.micro, ver2.micro)
