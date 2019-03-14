import os
import unittest
import time
import overrides_hack

from utils import create_sparse_tempfile, TestTags, tag_test
from gi.repository import BlockDev, GLib


class LoopTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("loop",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("loop_test", 1024**3)
        self.loop = None

    def _clean_up(self):
        try:
            BlockDev.loop_teardown(self.loop)
        except:
            pass
        os.unlink(self.dev_file)

class LoopTestSetupBasic(LoopTestCase):
    @tag_test(TestTags.CORE)
    def testLoop_setup_teardown_basic(self):
        """Verify that basic loop_setup and loop_teardown work as expected"""

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)

        # give kernel+udev time to update stuff under /sys and try to get the
        # backing file -- there should be none after the teardown
        time.sleep(1)
        b_file = BlockDev.loop_get_backing_file(self.loop)
        self.assertIsNone(b_file)

class LoopTestSetupOffset(LoopTestCase):
    def testLoop_setup_with_offset(self):
        """Verify that loop_setup with offset specified works as expected"""

        # now test with the offset
        succ, self.loop = BlockDev.loop_setup(self.dev_file, 10 * 1024**2)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        # should have smaller size due to the offset
        with open("/sys/block/%s/size" % self.loop, "r") as f:
            size = int(f.read()) * 512
        self.assertEqual(size, 1024**3 - 10 * 1024 **2)

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)


class LoopTestSetupOffsetSize(LoopTestCase):
    def testLoop_setup_with_offset_and_size(self):
        """Verify that loop_setup with offset and size specified works as expected"""

        # now test with the offset and size
        succ, self.loop = BlockDev.loop_setup(self.dev_file, 10 * 1024**2, 50 * 1024**2)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        # should have size as specified
        with open("/sys/block/%s/size" % self.loop, "r") as f:
            size = int(f.read()) * 512
        self.assertEqual(size, 50 * 1024**2)

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)

class LoopTestSetupReadOnly(LoopTestCase):
    def testLoop_setup_read_only(self):
        """Verify that loop_setup with read_only specified works as expected"""
        # test read-only
        succ, self.loop = BlockDev.loop_setup(self.dev_file, 0, 0, True)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        # should be read-only
        with open("/sys/block/%s/ro" % self.loop, "r") as f:
            self.assertEqual(f.read().strip(), "1")

# XXX: any sane way how to test part_probe=True/False?

class LoopTestGetLoopName(LoopTestCase):
    @tag_test(TestTags.CORE)
    def testLoop_get_loop_name(self):
        """Verify that loop_get_loop_name works as expected"""

        self.assertIs(BlockDev.loop_get_loop_name("/non/existing"), None)

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        ret_loop = BlockDev.loop_get_loop_name(self.dev_file)
        self.assertEqual(ret_loop, self.loop)

class LoopTestGetBackingFile(LoopTestCase):
    @tag_test(TestTags.CORE)
    def testLoop_get_backing_file(self):
        """Verify that loop_get_backing_file works as expected"""

        self.assertIs(BlockDev.loop_get_backing_file("/non/existing"), None)

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        f_name = BlockDev.loop_get_backing_file(self.loop)
        self.assertEqual(f_name, self.dev_file)

class LoopTestGetSetAutoclear(LoopTestCase):
    def testLoop_get_set_autoclear(self):
        """Verify that getting and setting the autoclear flag works as expected"""

        with self.assertRaises(GLib.Error):
            BlockDev.loop_get_autoclear("/non/existing")

        with self.assertRaises(GLib.Error):
            BlockDev.loop_set_autoclear("/non/existing", True)

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        self.assertFalse(BlockDev.loop_get_autoclear(self.loop))

        # open the loop device so that it doesn't disappear once we set
        # autoclear to True (it's otherwise not being used so it may get cleared
        # automatically)
        fd = os.open("/dev/" + self.loop, os.O_RDONLY)
        self.addCleanup(os.close, fd)

        self.assertTrue(BlockDev.loop_set_autoclear(self.loop, True))
        self.assertTrue(BlockDev.loop_get_autoclear(self.loop))

        self.assertTrue(BlockDev.loop_set_autoclear(self.loop, False))
        self.assertFalse(BlockDev.loop_get_autoclear(self.loop))

        # now the same, but with the "/dev/" prefix
        loop = "/dev/" + self.loop
        self.assertTrue(BlockDev.loop_set_autoclear(loop, True))
        self.assertTrue(BlockDev.loop_get_autoclear(loop))

        self.assertTrue(BlockDev.loop_set_autoclear(loop, False))
        self.assertFalse(BlockDev.loop_get_autoclear(loop))
