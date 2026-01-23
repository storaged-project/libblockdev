import os
import unittest
import overrides_hack

from utils import create_sparse_tempfile, create_sparse_file, TestTags, tag_test, required_plugins

import gi
gi.require_version('BlockDev', '3.0')
gi.require_version('GLib', '2.0')
from gi.repository import BlockDev
from gi.repository import GLib


@required_plugins(("loop",))
class LoopTestCase(unittest.TestCase):
    _loop_size = 100 * 1024**2

    requested_plugins = BlockDev.plugin_specs_from_names(("loop",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("loop_test", self._loop_size)
        self.loop = None

    def _clean_up(self):
        try:
            BlockDev.loop_teardown(self.loop)
        except:
            pass
        os.unlink(self.dev_file)

    def _get_loop_size(self):
        with open("/sys/block/%s/size" % self.loop, "r") as f:
            return int(f.read()) * 512

class LoopPluginVersionCase(LoopTestCase):
    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LOOP), "libbd_loop.so.3")

    @tag_test(TestTags.NOSTORAGE)
    def test_tech_available(self):
        """Verify that checking plugin availability works as expected"""
        succ = BlockDev.loop_is_tech_avail(BlockDev.LoopTech.LOOP, 0)
        self.assertTrue(succ)


class LoopTestSetupBasic(LoopTestCase):
    @tag_test(TestTags.CORE)
    def test_loop_setup_teardown_basic(self):
        """Verify that basic loop_setup and loop_teardown work as expected"""

        with self.assertRaisesRegex(GLib.GError, "Failed to open the backing file"):
            BlockDev.loop_setup("/non/existing", 10 * 1024**2)

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertEqual(info.backing_file, self.dev_file)
        self.assertEqual(info.offset, 0)
        self.assertFalse(info.autoclear)
        self.assertFalse(info.direct_io)
        self.assertTrue(info.part_scan)
        self.assertFalse(info.read_only)

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)


class LoopTestSetupOffset(LoopTestCase):
    def test_loop_setup_with_offset(self):
        """Verify that loop_setup with offset specified works as expected"""

        # now test with the offset
        succ, self.loop = BlockDev.loop_setup(self.dev_file, 10 * 1024**2)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertEqual(info.offset, 10 * 1024**2)

        # should have smaller size due to the offset
        self.assertEqual(self._get_loop_size(), self._loop_size - 10 * 1024 **2)

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)


class LoopTestSetupOffsetSize(LoopTestCase):
    def test_loop_setup_with_offset_and_size(self):
        """Verify that loop_setup with offset and size specified works as expected"""

        # now test with the offset and size
        succ, self.loop = BlockDev.loop_setup(self.dev_file, 10 * 1024**2, 50 * 1024**2)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        # should have size as specified
        self.assertEqual(self._get_loop_size(), 50 * 1024**2)

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)


class LoopTestSetupReadOnly(LoopTestCase):
    def test_loop_setup_read_only(self):
        """Verify that loop_setup with read_only specified works as expected"""
        # test read-only
        succ, self.loop = BlockDev.loop_setup(self.dev_file, 0, 0, True)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertTrue(info.read_only)

        # should be read-only
        with open("/sys/block/%s/ro" % self.loop, "r") as f:
            self.assertEqual(f.read().strip(), "1")


class LoopTestSetupSectorSize(LoopTestCase):
    def test_loop_setup_sector_size(self):
        """Verify that loop_setup with sector_size specified works as expected"""
        # test 4k sector size
        succ, self.loop = BlockDev.loop_setup(self.dev_file, sector_size=4096)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        # logical_block_size should be 4096
        with open("/sys/block/%s/queue/logical_block_size" % self.loop, "r") as f:
            self.assertEqual(f.read().strip(), "4096")


class LoopTestSetupPartprobe(LoopTestCase):
    def test_loop_setup_partprobe(self):
        """Verify that loop_setup with part_scan specified works as expected"""
        # part scan on
        succ, self.loop = BlockDev.loop_setup(self.dev_file, part_scan=True)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertTrue(info.part_scan)

        with open("/sys/block/%s/loop/partscan" % self.loop, "r") as f:
            self.assertEqual(f.read().strip(), "1")

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)

        # part scan off
        succ, self.loop = BlockDev.loop_setup(self.dev_file, part_scan=False)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertFalse(info.part_scan)

        with open("/sys/block/%s/loop/partscan" % self.loop, "r") as f:
            self.assertEqual(f.read().strip(), "0")

        succ = BlockDev.loop_teardown(self.loop)
        self.assertTrue(succ)


class LoopTestGetLoopName(LoopTestCase):
    @tag_test(TestTags.CORE)
    def test_loop_get_loop_name(self):
        """Verify that loop_get_loop_name works as expected"""

        self.assertIs(BlockDev.loop_get_loop_name("/non/existing"), None)

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        ret_loop = BlockDev.loop_get_loop_name(self.dev_file)
        self.assertEqual(ret_loop, self.loop)


class LoopTestGetSetAutoclear(LoopTestCase):
    def test_loop_get_set_autoclear(self):
        """Verify that getting and setting the autoclear flag works as expected"""

        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ)
        self.assertTrue(self.loop)

        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertFalse(info.autoclear)

        # open the loop device so that it doesn't disappear once we set
        # autoclear to True (it's otherwise not being used so it may get cleared
        # automatically)
        fd = os.open("/dev/" + self.loop, os.O_RDONLY)
        self.addCleanup(os.close, fd)

        self.assertTrue(BlockDev.loop_set_autoclear(self.loop, True))
        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertTrue(info.autoclear)

        self.assertTrue(BlockDev.loop_set_autoclear(self.loop, False))
        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertFalse(info.autoclear)

        # now the same, but with the "/dev/" prefix
        loop = "/dev/" + self.loop
        self.assertTrue(BlockDev.loop_set_autoclear(loop, True))
        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertTrue(info.autoclear)

        self.assertTrue(BlockDev.loop_set_autoclear(loop, False))
        info = BlockDev.loop_info(self.loop)
        self.assertIsNotNone(info)
        self.assertFalse(info.autoclear)

        with self.assertRaisesRegex(GLib.GError, "Failed to open device"):
            BlockDev.loop_set_autoclear("/non/existing", True)


class LoopTestSetCapacity(LoopTestCase):
    def test_loop_set_capacity(self):
        succ, self.loop = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ)
        self.assertTrue(self.loop)
        self.assertEqual(self._get_loop_size(), self._loop_size)

        # enlarge the backing file
        create_sparse_file(self.dev_file, self._loop_size * 2)

        # size shouldn't change without forcing re-read
        self.assertEqual(self._get_loop_size(), self._loop_size)

        succ = BlockDev.loop_set_capacity(self.loop)
        self.assertTrue(succ)

        # now the size should be updated
        self.assertEqual(self._get_loop_size(), self._loop_size * 2)

        with self.assertRaisesRegex(GLib.GError, "Failed to open device"):
            BlockDev.loop_set_capacity("/non/existing")
