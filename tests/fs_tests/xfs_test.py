import tempfile

from packaging.version import Version

from .fs_test import FSTestCase, FSNoDevTestCase, mounted, check_output

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class XfsNoDevTestCase(FSNoDevTestCase):
    pass


class XfsTestCase(FSTestCase):
    loop_size = 500 * 1024**2

    def setUp(self):
        super(XfsTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="xfs_test")


class XfsTestAvailability(XfsNoDevTestCase):

    def test_xfs_available(self):
        """Verify that it is possible to check xfs tech availability"""
        available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.QUERY |
                                              BlockDev.FSTechMode.REPAIR |
                                              BlockDev.FSTechMode.CHECK |
                                              BlockDev.FSTechMode.SET_LABEL |
                                              BlockDev.FSTechMode.RESIZE |
                                              BlockDev.FSTechMode.SET_UUID)
        self.assertTrue(available)

        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkfs.xfs
        with utils.fake_path(all_but="mkfs.xfs"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkfs.xfs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.MKFS)

        # now try without xfs_db
        with utils.fake_path(all_but="xfs_db"):
            with self.assertRaisesRegex(GLib.GError, "The 'xfs_db' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.CHECK)

        # now try without xfs_repair
        with utils.fake_path(all_but="xfs_repair"):
            with self.assertRaisesRegex(GLib.GError, "The 'xfs_repair' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.REPAIR)

        # now try without xfs_admin
        with utils.fake_path(all_but="xfs_admin"):
            with self.assertRaisesRegex(GLib.GError, "The 'xfs_admin' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.QUERY)

            with self.assertRaisesRegex(GLib.GError, "The 'xfs_admin' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.SET_LABEL)

            with self.assertRaisesRegex(GLib.GError, "The 'xfs_admin' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.SET_UUID)

        # now try without xfs_growfs
        with utils.fake_path(all_but="xfs_growfs"):
            with self.assertRaisesRegex(GLib.GError, "The 'xfs_growfs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.RESIZE)


class XfsTestFeatures(XfsNoDevTestCase):

    def test_xfs_features(self):
        features = BlockDev.fs_features("xfs")
        self.assertIsNotNone(features)

        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_GROW)
        self.assertTrue(features.resize & BlockDev.FSResizeFlags.ONLINE_GROW)

        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
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

        self.assertEqual(features.min_size, 300 * 1024**2)
        self.assertEqual(features.max_size, 16 * 1024**6 - 1)


class XfsTestMkfs(XfsTestCase):
    @tag_test(TestTags.CORE)
    def test_xfs_mkfs(self):
        """Verify that it is possible to create a new xfs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "xfs")

        BlockDev.fs_wipe(self.loop_dev, True)


class XfsTestCheck(XfsTestCase):
    def test_xfs_check(self):
        """Verify that it is possible to check an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_xfs_check(self.loop_dev)
        self.assertTrue(succ)

        # mounted RO, can be checked and nothing happened in/to the file system,
        # so it should be just reported as clean
        with mounted(self.loop_dev, self.mount_dir, ro=True):
            succ = BlockDev.fs_xfs_check(self.loop_dev)
            self.assertTrue(succ)

        succ = BlockDev.fs_xfs_check(self.loop_dev)
        self.assertTrue(succ)


class XfsTestRepair(XfsTestCase):
    def test_xfs_repair(self):
        """Verify that it is possible to repair an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_xfs_repair(self.loop_dev, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            with self.assertRaises(GLib.GError):
                BlockDev.fs_xfs_repair(self.loop_dev, None)

        succ = BlockDev.fs_xfs_repair(self.loop_dev, None)
        self.assertTrue(succ)


class XfsGetInfo(XfsTestCase):
    @tag_test(TestTags.CORE)
    def test_xfs_get_info(self):
        """Verify that it is possible to get info about an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertEqual(fi.block_size, 4096)
        self.assertEqual(fi.block_count, self.loop_size / 4096)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)


class XfsSetLabel(XfsTestCase):
    def test_xfs_set_label(self):
        """Verify that it is possible to set label of an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_xfs_set_label(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = BlockDev.fs_xfs_set_label(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = BlockDev.fs_xfs_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_xfs_check_label("TEST_LABEL")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 12 characters long."):
            BlockDev.fs_xfs_check_label(13 * "a")

        with self.assertRaisesRegex(GLib.GError, "cannot contain spaces"):
            BlockDev.fs_xfs_check_label("TEST LABEL")


class XfsResize(XfsTestCase):
    def test_xfs_resize(self):
        """Verify that it is possible to resize an xfs file system"""

        lv = self._setup_lvm(vgname="libbd_fs_tests", lvname="xfs_test", lvsize="350M")

        succ = BlockDev.fs_xfs_mkfs(lv, None)
        self.assertTrue(succ)

        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 350 * 1024**2)

        # no change, nothing should happen
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)

        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 350 * 1024**2)

        # (still) impossible to shrink an XFS file system
        xfs_version = self._get_xfs_version()
        if xfs_version < Version("5.12"):
            with mounted(lv, self.mount_dir):
                with self.assertRaises(GLib.GError):
                    succ = BlockDev.fs_xfs_resize(self.mount_dir, 40 * 1024**2 / fi.block_size, None)

        self._lvresize("libbd_fs_tests", "xfs_test", "400M")
        # should grow to 400 MiB (full size of the LV)
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 400 * 1024**2)

        self._lvresize("libbd_fs_tests", "xfs_test", "450M")
        # grow just to 430 MiB
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 430 * 1024**2 / fi.block_size, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 430 * 1024**2)

        # should grow to 450 MiB (full size of the LV)
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 450 * 1024**2)


class XfsSetUUID(XfsTestCase):

    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def test_xfs_set_uuid(self):
        """Verify that it is possible to set UUID of an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_xfs_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_xfs_set_uuid(self.loop_dev, "nil")
        self.assertTrue(succ)

        # can't use libblockdev because XFS without UUID can't be mounted
        fs_type = check_output(["blkid", "-ovalue", "-sUUID", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        succ = BlockDev.fs_xfs_set_uuid(self.loop_dev, "generate")
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        random_uuid = fi.uuid

        # no uuid -> random
        succ = BlockDev.fs_xfs_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, random_uuid)

        succ = BlockDev.fs_xfs_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        succ = BlockDev.fs_xfs_check_uuid(self.test_uuid.upper())
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.fs_xfs_check_uuid("aaaaaaa")
