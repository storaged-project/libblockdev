import tempfile

from .fs_test import FSTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class BtrfsTestCase(FSTestCase):

    loop_size = 500 * 1024**2

    def setUp(self):
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")

        super(BtrfsTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="btrfs_test")


class BtrfsTestMkfs(BtrfsTestCase):
    def test_btrfs_mkfs(self):
        """Verify that it is possible to create a new btrfs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_btrfs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "btrfs")

        BlockDev.fs_wipe(self.loop_dev, True)


class BtrfsMkfsWithLabel(BtrfsTestCase):
    def test_btrfs_mkfs_with_label(self):
        """Verify that it is possible to create a btrfs file system with label"""

        ea = BlockDev.ExtraArg.new("-L", "test_label")
        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")


class BtrfsTestWipe(BtrfsTestCase):
    def test_btrfs_wipe(self):
        """Verify that it is possible to wipe an exfat file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_btrfs_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_btrfs_wipe(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not a btrfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_btrfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not a btrfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_btrfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)


class BtrfsTestCheck(BtrfsTestCase):
    def test_btrfs_check(self):
        """Verify that it is possible to check an btrfs file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_btrfs_check(self.loop_dev, None)
        self.assertTrue(succ)


class BtrfsTestRepair(BtrfsTestCase):
    def test_btrfs_repair(self):
        """Verify that it is possible to repair a btrfs file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_btrfs_repair(self.loop_dev, None)
        self.assertTrue(succ)


class BtrfsGetInfo(BtrfsTestCase):
    def test_btrfs_get_info(self):
        """Verify that it is possible to get info about a btrfs file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)

        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertGreater(fi.size, 0)
        self.assertGreater(fi.free_space, 0)
        self.assertGreater(fi.size, fi.free_space)


class BtrfsSetLabel(BtrfsTestCase):
    def test_btrfs_set_label(self):
        """Verify that it is possible to set label of a btrfs file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertTrue(fi)
            self.assertEqual(fi.label, "")

            succ = BlockDev.fs_btrfs_set_label(self.mount_dir, "test_label")
            self.assertTrue(succ)
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertTrue(fi)
            self.assertEqual(fi.label, "test_label")

            succ = BlockDev.fs_btrfs_set_label(self.mount_dir, "test_label2")
            self.assertTrue(succ)
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertTrue(fi)
            self.assertEqual(fi.label, "test_label2")

            succ = BlockDev.fs_btrfs_set_label(self.mount_dir, "")
            self.assertTrue(succ)
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertTrue(fi)
            self.assertEqual(fi.label, "")

        succ = BlockDev.fs_btrfs_check_label("test_label")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 256 characters long."):
            BlockDev.fs_btrfs_check_label(257 * "a")

        with self.assertRaisesRegex(GLib.GError, "cannot contain new lines."):
            BlockDev.fs_btrfs_check_label("a\nb")


class BtrfsSetUUID(BtrfsTestCase):

    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def test_btrfs_set_uuid(self):
        """Verify that it is possible to set UUID of an btrfs file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_btrfs_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        # no uuid -> random
        succ = BlockDev.fs_btrfs_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_btrfs_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.fs_btrfs_check_uuid("aaaaaaa")


class BtrfsResize(BtrfsTestCase):
    def test_btrfs_resize(self):
        """Verify that it is possible to resize a btrfs file system"""

        succ = BlockDev.fs_btrfs_mkfs(self.loop_dev)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            succ = BlockDev.fs_btrfs_resize(self.mount_dir, 300 * 1024**2)
            self.assertTrue(succ)

            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertEqual(fi.size, 300 * 1024**2)

            # grow
            succ = BlockDev.fs_btrfs_resize(self.mount_dir, 350 * 1024**2)
            self.assertTrue(succ)

            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertEqual(fi.size, 350 * 1024**2)

            # shrink again
            succ = BlockDev.fs_btrfs_resize(self.mount_dir, 300 * 1024**2)
            self.assertTrue(succ)

            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertEqual(fi.size, 300 * 1024**2)

            # resize to maximum size
            succ = BlockDev.fs_btrfs_resize(self.mount_dir, 0)
            self.assertTrue(succ)

            fi = BlockDev.fs_btrfs_get_info(self.mount_dir)
            self.assertEqual(fi.size, self.loop_size)


class BtrfsMultiDevice(BtrfsTestCase):

    def _clean_up(self):
        utils.umount(self.mount_dir)
        BlockDev.fs_btrfs_wipe(self.loop_dev2)

        super(BtrfsMultiDevice, self)._clean_up()

    def test_btrfs_multidevice(self):
        """Verify that filesystem plugin returns errors when used on multidevice volumes"""

        ret, _out, _err = utils.run_command("mkfs.btrfs %s %s" % (self.loop_dev, self.loop_dev2))
        self.assertEqual(ret, 0)

        with mounted(self.loop_dev, self.mount_dir):
            with self.assertRaisesRegex(GLib.GError, "Filesystem plugin is not suitable for multidevice Btrfs volumes"):
                    BlockDev.fs_btrfs_get_info(self.mount_dir)

        with mounted(self.loop_dev, self.mount_dir):
            with self.assertRaisesRegex(GLib.GError, "Filesystem plugin is not suitable for multidevice Btrfs volumes"):
                BlockDev.fs_btrfs_resize(self.mount_dir, 0)
