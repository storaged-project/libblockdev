import tempfile
import overrides_hack

from .fs_test import FSTestCase, FSNoDevTestCase, mounted

import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class NTFSNoDevTestCase(FSNoDevTestCase):
    def setUp(self):
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

        super(NTFSNoDevTestCase, self).setUp()


class NTFSTestCase(FSTestCase):
    def setUp(self):
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

        super(NTFSTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="ntfs_test")


class NTFSTestAvailability(NTFSNoDevTestCase):

    def test_ntfs_available(self):
        """Verify that it is possible to check ntfs tech availability"""
        available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.QUERY |
                                              BlockDev.FSTechMode.REPAIR |
                                              BlockDev.FSTechMode.CHECK |
                                              BlockDev.FSTechMode.SET_LABEL |
                                              BlockDev.FSTechMode.RESIZE |
                                              BlockDev.FSTechMode.SET_UUID)
        self.assertTrue(available)

        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkntfs
        with utils.fake_path(all_but="mkntfs"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkntfs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.MKFS)

        # now try without ntfsfix
        with utils.fake_path(all_but="ntfsfix"):
            with self.assertRaisesRegex(GLib.GError, "The 'ntfsfix' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.CHECK)

            with self.assertRaisesRegex(GLib.GError, "The 'ntfsfix' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.REPAIR)

        # now try without ntfsinfo
        with utils.fake_path(all_but="ntfsinfo"):
            with self.assertRaisesRegex(GLib.GError, "The 'ntfsinfo' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.QUERY)

        # now try without ntfsresize
        with utils.fake_path(all_but="ntfsresize"):
            with self.assertRaisesRegex(GLib.GError, "The 'ntfsresize' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.RESIZE)

        # now try without ntfslabel
        with utils.fake_path(all_but="ntfslabel"):
            with self.assertRaisesRegex(GLib.GError, "The 'ntfslabel' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.SET_LABEL)

            with self.assertRaisesRegex(GLib.GError, "The 'ntfslabel' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS, BlockDev.FSTechMode.SET_UUID)


class NTFSTestFeatures(NTFSNoDevTestCase):

    def test_ntfs_features(self):
        features = BlockDev.fs_features("ntfs")
        self.assertIsNotNone(features)

        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_GROW)
        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_SHRINK)

        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NODISCARD)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NOPT)

        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.CHECK)
        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.REPAIR)

        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.LABEL)
        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.UUID)

        self.assertEqual(features.features, 0)

        self.assertEqual(features.partition_id, "0x07")
        self.assertEqual(features.partition_type, "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")


class NTFSTestMkfs(NTFSTestCase):
    def test_ntfs_mkfs(self):
        """Verify that it is possible to create a new NTFS file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_ntfs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "ntfs")

        BlockDev.fs_wipe(self.loop_dev, True)


class NTFSMkfsWithLabel(NTFSTestCase):
    def test_ntfs_mkfs_with_label(self):
        """Verify that it is possible to create an NTFS file system with label"""

        ea = BlockDev.ExtraArg.new("-L", "test_label")
        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")


class NTFSGetInfo(NTFSTestCase):
    def test_ntfs_get_info(self):
        """Verify that it is possible to get info about an NTFS file system"""

        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertGreater(fi.size, 0)
        self.assertLess(fi.free_space, fi.size)


class NTFSResize(NTFSTestCase):
    def test_ntfs_resize(self):
        """Verify that it is possible to resize an NTFS file system"""

        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_ntfs_repair(self.loop_dev)
        self.assertTrue(succ)

        # shrink
        succ = BlockDev.fs_ntfs_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)

        succ = BlockDev.fs_ntfs_repair(self.loop_dev)
        self.assertTrue(succ)

        # resize to maximum size
        succ = BlockDev.fs_ntfs_resize(self.loop_dev, 0)
        self.assertTrue(succ)


class NTFSSetLabel(NTFSTestCase):
    def test_ntfs_set_label(self):
        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_ntfs_set_label(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = BlockDev.fs_ntfs_set_label(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = BlockDev.fs_ntfs_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_ntfs_check_label("TEST_LABEL")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 128 characters long."):
            BlockDev.fs_ntfs_check_label(129 * "a")


class NTFSSetUUID(NTFSTestCase):

    test_uuid = "54E1629A44FD724B"

    def test_ntfs_set_uuid(self):
        """Verify that it is possible to set UUID of an ntfs file system"""
        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_ntfs_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        # no uuid -> random
        succ = BlockDev.fs_ntfs_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_ntfs_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "8 or 16 characters long"):
            BlockDev.fs_ntfs_check_uuid(9 * "a")

        with self.assertRaisesRegex(GLib.GError, "must be a hexadecimal number"):
            BlockDev.fs_ntfs_check_uuid(16 * "z")
