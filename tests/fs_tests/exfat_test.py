import tempfile

from .fs_test import FSTestCase, FSNoDevTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class ExfatNoDevTestCase(FSNoDevTestCase):
    def setUp(self):
        super(ExfatNoDevTestCase, self).setUp()

        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")


class ExfatTestCase(FSTestCase):
    def setUp(self):
        super(ExfatTestCase, self).setUp()

        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="exfat_test")


class ExfatTestAvailability(ExfatNoDevTestCase):

    def test_exfat_available(self):
        """Verify that it is possible to check exfat tech availability"""
        available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.REPAIR |
                                              BlockDev.FSTechMode.CHECK |
                                              BlockDev.FSTechMode.SET_LABEL |
                                              BlockDev.FSTechMode.SET_UUID)
        self.assertTrue(available)

        with self.assertRaisesRegex(GLib.GError, "doesn't support resizing"):
            BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.RESIZE)

        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkfs.exfat
        with utils.fake_path(all_but="mkfs.exfat"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkfs.exfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.MKFS)

        # now try without fsck.exfat
        with utils.fake_path(all_but="fsck.exfat"):
            with self.assertRaisesRegex(GLib.GError, "The 'fsck.exfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.CHECK)

            with self.assertRaisesRegex(GLib.GError, "The 'fsck.exfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.REPAIR)

        # now try without tune.exfat
        with utils.fake_path(all_but="tune.exfat"):
            with self.assertRaisesRegex(GLib.GError, "The 'tune.exfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.QUERY)

            with self.assertRaisesRegex(GLib.GError, "The 'tune.exfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.SET_LABEL)

            with self.assertRaisesRegex(GLib.GError, "The 'tune.exfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT, BlockDev.FSTechMode.SET_UUID)


class ExfatTestFeatures(ExfatNoDevTestCase):

    def test_exfat_features(self):
        features = BlockDev.fs_features("exfat")
        self.assertIsNotNone(features)

        self.assertEqual(features.resize, 0)

        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NODISCARD)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NOPT)

        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.CHECK)
        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.REPAIR)

        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.LABEL)
        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.UUID)

        self.assertEqual(features.features, 0)

        self.assertEqual(features.partition_id, "0x07")
        self.assertEqual(features.partition_type, "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")


class ExfatTestMkfs(ExfatTestCase):
    def test_exfat_mkfs(self):
        """Verify that it is possible to create a new exfat file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "exfat")

        BlockDev.fs_wipe(self.loop_dev, True)


class ExfatMkfsWithLabel(ExfatTestCase):
    def test_exfat_mkfs_with_label(self):
        """Verify that it is possible to create an exfat file system with label"""

        ea = BlockDev.ExtraArg.new("-n", "test_label")
        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")


class ExfatTestCheck(ExfatTestCase):
    def test_exfat_check(self):
        """Verify that it is possible to check an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_check(self.loop_dev)
        self.assertTrue(succ)


class ExfatTestRepair(ExfatTestCase):
    def test_exfat_repair(self):
        """Verify that it is possible to repair an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_repair(self.loop_dev)
        self.assertTrue(succ)


class ExfatGetInfo(ExfatTestCase):
    def test_exfat_get_info(self):
        """Verify that it is possible to get info about an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertGreater(fi.sector_size, 0)
        self.assertGreater(fi.sector_count, 0)
        self.assertGreater(fi.cluster_count, 0)


class ExfatSetLabel(ExfatTestCase):
    def test_exfat_set_label(self):
        """Verify that it is possible to set label of an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_exfat_set_label(self.loop_dev, "test_label")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")

        succ = BlockDev.fs_exfat_set_label(self.loop_dev, "test_label2")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label2")

        succ = BlockDev.fs_exfat_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_exfat_check_label("TEST_LABEL")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "is too long."):
            BlockDev.fs_exfat_check_label(12 * "a")


class ExfatSetUUID(ExfatTestCase):
    def test_exfat_set_uuid(self):
        """Verify that it is possible to set UUID/volume ID of an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_set_uuid(self.loop_dev, "0x2E24EC82")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, "2E24-EC82")

        succ = BlockDev.fs_exfat_set_uuid(self.loop_dev, "2E24EC82")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, "2E24-EC82")

        # should be also support with the dash
        succ = BlockDev.fs_exfat_set_uuid(self.loop_dev, "2E24-EC82")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, "2E24-EC82")

        succ = BlockDev.fs_exfat_set_uuid(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_exfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertTrue(fi.uuid)  # new random, not empty
        self.assertNotEqual(fi.uuid, "2E24-EC82")

        succ = BlockDev.fs_exfat_check_uuid("0x2E24EC82")
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_check_uuid("2E24EC82")
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_check_uuid("2E24-EC82")
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_check_uuid("0000-0000")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "must be a hexadecimal number."):
            BlockDev.fs_exfat_check_uuid("z")

        with self.assertRaisesRegex(GLib.GError, "must be a hexadecimal number."):
            BlockDev.fs_exfat_check_uuid("aaaa-")

        with self.assertRaisesRegex(GLib.GError, "must fit into 32 bits."):
            BlockDev.fs_exfat_check_uuid(10 * "f")
