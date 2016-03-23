import unittest
import os
import tempfile
from contextlib import contextmanager
from utils import create_sparse_tempfile
import overrides_hack

from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

def mount(device, where):
    if not os.path.isdir(where):
        os.makedirs(where)
    os.system("mount %s %s" % (device, where))

def umount(what):
    try:
        os.system("umount %s &>/dev/null" % what)
    except OSError:
        # no such file or directory
        pass

@contextmanager
def mounted(device, where):
    mount(device, where)
    yield
    umount(where)

class FSTestCase(unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("part_test", 100 * 1024**2)
        self.dev_file2 = create_sparse_tempfile("part_test", 100 * 1024**2)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="ext4_test")

    def _clean_up(self):
        succ = BlockDev.loop_teardown(self.loop_dev)
        if not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")
        os.unlink(self.dev_file)
        succ = BlockDev.loop_teardown(self.loop_dev2)
        if  not succ:
            os.unlink(self.dev_file2)
            raise RuntimeError("Failed to tear down loop device used for testing")
        os.unlink(self.dev_file2)
        try:
            umount(self.mount_dir)
        except:
            pass
        os.rmdir(self.mount_dir)

class Ext4TestMkfs(FSTestCase):
    def test_ext4_mkfs(self):
        """Verify that it is possible to create a new ext4 file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_mkfs("/non/existing/device")

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        BlockDev.fs_wipe(self.loop_dev, True)

class Ext4TestWipe(FSTestCase):
    def test_ext4_wipe(self):
        """Verify that it is possible to wipe an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_ext4_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_wipe(self.loop_dev)

        os.system("pvcreate %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an ext4 file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        os.system("mkfs.ext2 %s &>/dev/null" % self.loop_dev)

        # ext2, not an ext4 file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class Ext4TestCheck(FSTestCase):
    def test_ext4_check(self):
        """Verify that it is possible to check an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_ext4_check(self.loop_dev)
        self.assertTrue(succ)

        # mounted, but can be checked
        with mounted(self.loop_dev, self.mount_dir):
            succ = BlockDev.fs_ext4_check(self.loop_dev)
            self.assertTrue(succ)

        succ = BlockDev.fs_ext4_check(self.loop_dev)
        self.assertTrue(succ)

class Ext4TestRepair(FSTestCase):
    def test_ext4_repair(self):
        """Verify that it is possible to repair an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_ext4_repair(self.loop_dev, False)
        self.assertTrue(succ)

        # unsafe operations should work here too
        succ = BlockDev.fs_ext4_repair(self.loop_dev, True)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            with self.assertRaises(GLib.GError):
                BlockDev.fs_ext4_repair(self.loop_dev, False)

        succ = BlockDev.fs_ext4_repair(self.loop_dev, False)
        self.assertTrue(succ)

class Ext4GetInfo(FSTestCase):
    def test_ext4_get_info(self):
        """Verify that it is possible to get info about an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertTrue(fi.state, "clean")

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_ext4_get_info(self.loop_dev)
            self.assertEqual(fi.block_size, 1024)
            self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
            # at least 90 % should be available, so it should be reported
            self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)
            self.assertEqual(fi.label, "")
            # should be an non-empty string
            self.assertTrue(fi.uuid)
            self.assertTrue(fi.state, "clean")

class Ext4SetLabel(FSTestCase):
    def test_ext4_set_label(self):
        """Verify that it is possible to set label of an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_ext4_set_label(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = BlockDev.fs_ext4_set_label(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = BlockDev.fs_ext4_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

class Ext4Resize(FSTestCase):
    def test_ext4_resize(self):
        """Verify that it is possible to set label of an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)

        succ = BlockDev.fs_ext4_resize(self.loop_dev, 50 * 1024**2)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 50 * 1024**2 / 1024)

        # resize back
        succ = BlockDev.fs_ext4_resize(self.loop_dev, 100 * 1024**2)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)

        # resize again
        succ = BlockDev.fs_ext4_resize(self.loop_dev, 50 * 1024**2)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 50 * 1024**2 / 1024)

        # resize back again, this time to maximum size
        succ = BlockDev.fs_ext4_resize(self.loop_dev, 0)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)
