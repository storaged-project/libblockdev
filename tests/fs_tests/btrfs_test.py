import tempfile

from .fs_test import FSTestCase, FSNoDevTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class BtrfsNoDevTestCase(FSNoDevTestCase):
    def setUp(self):
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")

        super(BtrfsNoDevTestCase, self).setUp()


class BtrfsTestCase(FSTestCase):

    loop_size = 500 * 1024**2

    def setUp(self):
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")

        super(BtrfsTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="btrfs_test")


class BtrfsTestFeatures(BtrfsNoDevTestCase):

    def test_btrfs_features(self):
        features = BlockDev.fs_features("btrfs")
        self.assertIsNotNone(features)

        self.assertTrue(features.resize & BlockDev.FSResizeFlags.ONLINE_GROW)
        self.assertTrue(features.resize & BlockDev.FSResizeFlags.ONLINE_GROW)

        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.NODISCARD)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.FORCE)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NOPT)

        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.CHECK)
        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.REPAIR)

        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.LABEL)
        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.UUID)

        self.assertTrue(features.features & BlockDev.FSFeatureFlags.OWNERS)
        self.assertFalse(features.features & BlockDev.FSFeatureFlags.PARTITION_TABLE)

        self.assertEqual(features.partition_id, "0x83")
        self.assertEqual(features.partition_type, "0fc63daf-8483-4772-8e79-3d69d8477de4")


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
        BlockDev.fs_wipe(self.loop_dev2, True)

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
