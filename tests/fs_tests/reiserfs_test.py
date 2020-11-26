import tempfile
import six

from .fs_test import FSTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class ReiserFSTestCase(FSTestCase):
    def setUp(self):
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")

        super(ReiserFSTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="reiserfs_test")


class ReiserFSTestMkfs(ReiserFSTestCase):
    def test_reiserfs_mkfs(self):
        """Verify that it is possible to create a new reiserfs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "reiserfs")

        BlockDev.fs_wipe(self.loop_dev, True)


class ReiserFSMkfsWithLabel(ReiserFSTestCase):
    def test_reiserfs_mkfs_with_label(self):
        """Verify that it is possible to create an reiserfs file system with label"""

        ea = BlockDev.ExtraArg.new("-l", "test_label")
        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")


class ReiserFSTestWipe(ReiserFSTestCase):
    def test_reiserfs_wipe(self):
        """Verify that it is possible to wipe an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_wipe(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an reiserfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an reiserfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)


class ReiserFSTestCheck(ReiserFSTestCase):
    def test_reiserfs_check(self):
        """Verify that it is possible to check an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_check(self.loop_dev, None)
        self.assertTrue(succ)


class ReiserFSTestRepair(ReiserFSTestCase):
    def test_reiserfs_repair(self):
        """Verify that it is possible to repair an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_repair(self.loop_dev, None)
        self.assertTrue(succ)


class ReiserFSGetInfo(ReiserFSTestCase):
    def test_reiserfs_get_info(self):
        """Verify that it is possible to get info about an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertGreater(fi.block_size, 0)
        self.assertGreater(fi.block_count, 0)
        self.assertLess(fi.free_blocks, fi.block_count)


class ReiserFSSetLabel(ReiserFSTestCase):
    def test_reiserfs_set_label(self):
        """Verify that it is possible to set label of an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_reiserfs_set_label(self.loop_dev, "test_label")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")

        succ = BlockDev.fs_reiserfs_set_label(self.loop_dev, "test_label2")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label2")

        succ = BlockDev.fs_reiserfs_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_reiserfs_check_label("test_label")
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, "at most 16 characters long."):
            BlockDev.fs_reiserfs_check_label(17 * "a")


class ReiserFSResize(ReiserFSTestCase):
    def test_reiserfs_resize(self):
        """Verify that it is possible to resize an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # shrink
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)

        # grow
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 100 * 1024**2)
        self.assertTrue(succ)

        # shrink again
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)


        # resize to maximum size
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 0)
        self.assertTrue(succ)


class ReiserFSSetUUID(ReiserFSTestCase):

    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def test_reiserfs_set_uuid(self):
        """Verify that it is possible to set UUID of an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_reiserfs_set_uuid(self.loop_dev, "random")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, self.test_uuid)
        random_uuid = fi.uuid

        # no uuid -> random
        succ = BlockDev.fs_reiserfs_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, random_uuid)

        succ = BlockDev.fs_reiserfs_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.fs_reiserfs_check_uuid("aaaaaaa")
