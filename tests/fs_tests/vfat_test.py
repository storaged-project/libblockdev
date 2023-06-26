import re
import tempfile

from packaging.version import Version

from .fs_test import FSTestCase, FSNoDevTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


def _get_dosfstools_version():
    _ret, out, _err = utils.run_command("mkfs.vfat --help")
    # mkfs.fat 4.1 (2017-01-24)
    m = re.search(r"mkfs\.fat ([\d\.]+)", out)
    if not m or len(m.groups()) != 1:
        raise RuntimeError("Failed to determine dosfstools version from: %s" % out)
    return Version(m.groups()[0])


DOSFSTOOLS_VERSION = _get_dosfstools_version()


class VfatNoDevTestCase(FSNoDevTestCase):
    pass


class VfatTestCase(FSTestCase):
    def setUp(self):
        super(VfatTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="vfat_test")

        if DOSFSTOOLS_VERSION <= Version("4.1"):
            self._mkfs_options = None
        else:
            self._mkfs_options = [BlockDev.ExtraArg.new("--mbr=n", "")]


class VfatTestAvailability(VfatNoDevTestCase):

    def test_vfat_available(self):
        """Verify that it is possible to check vfat tech availability"""
        available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.QUERY |
                                              BlockDev.FSTechMode.REPAIR |
                                              BlockDev.FSTechMode.CHECK |
                                              BlockDev.FSTechMode.SET_LABEL |
                                              BlockDev.FSTechMode.RESIZE)
        self.assertTrue(available)

        if DOSFSTOOLS_VERSION >= Version("4.2"):
            uuid_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.SET_UUID)
            self.assertTrue(uuid_avail)
        else:
            with self.assertRaisesRegex(GLib.GError, "Too low version of fatlabel"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.SET_UUID)

        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkfs.vfat
        with utils.fake_path(all_but="mkfs.vfat"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkfs.vfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.MKFS)

        # now try without fsck.vfat
        with utils.fake_path(all_but="fsck.vfat"):
            with self.assertRaisesRegex(GLib.GError, "The 'fsck.vfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.CHECK)

            with self.assertRaisesRegex(GLib.GError, "The 'fsck.vfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.REPAIR)

            with self.assertRaisesRegex(GLib.GError, "The 'fsck.vfat' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.QUERY)

        # now try without fatlabel
        with utils.fake_path(all_but="fatlabel"):
            with self.assertRaisesRegex(GLib.GError, "The 'fatlabel' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.SET_LABEL)

            with self.assertRaisesRegex(GLib.GError, "The 'fatlabel' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.SET_UUID)

        # now try without vfat-resize
        with utils.fake_path(all_but="vfat-resize"):
            with self.assertRaisesRegex(GLib.GError, "The 'vfat-resize' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.VFAT, BlockDev.FSTechMode.RESIZE)


class VfatTestFeatures(VfatNoDevTestCase):

    def test_vfat_features(self):
        features = BlockDev.fs_features("vfat")
        self.assertIsNotNone(features)

        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_GROW)
        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_SHRINK)

        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NODISCARD)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.FORCE)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.NOPT)

        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.CHECK)
        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.REPAIR)

        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.LABEL)
        self.assertTrue(features.configure & BlockDev.FSConfigureFlags.UUID)

        self.assertEqual(features.features, BlockDev.FSFeatureFlags.PARTITION_TABLE)

        self.assertEqual(features.partition_id, "0x0c")
        self.assertEqual(features.partition_type, "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")


class VfatTestMkfs(VfatTestCase):
    def test_vfat_mkfs(self):
        """Verify that it is possible to create a new vfat file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_mkfs("/non/existing/device", self._mkfs_options)

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "vfat")

        BlockDev.fs_wipe(self.loop_dev, True)


class VfatMkfsWithLabel(VfatTestCase):
    def test_vfat_mkfs_with_label(self):
        """Verify that it is possible to create an vfat file system with label"""

        ea = BlockDev.ExtraArg.new("-n", "TEST_LABEL")
        if self._mkfs_options:
            succ = BlockDev.fs_vfat_mkfs(self.loop_dev, [ea] + self._mkfs_options)
        else:
            succ = BlockDev.fs_vfat_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")


class VfatTestCheck(VfatTestCase):
    def test_vfat_check(self):
        """Verify that it is possible to check an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check(self.loop_dev, None)
        self.assertTrue(succ)


class VfatTestRepair(VfatTestCase):
    def test_vfat_repair(self):
        """Verify that it is possible to repair an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_repair(self.loop_dev, None)
        self.assertTrue(succ)


class VfatGetInfo(VfatTestCase):
    def test_vfat_get_info(self):
        """Verify that it is possible to get info about an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)


class VfatSetLabel(VfatTestCase):
    def test_vfat_set_label(self):
        """Verify that it is possible to set label of an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_vfat_set_label(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = BlockDev.fs_vfat_set_label(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = BlockDev.fs_vfat_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_vfat_check_label("TEST_LABEL")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 11 characters long."):
            BlockDev.fs_vfat_check_label(12 * "a")


class VfatSetUUID(VfatTestCase):
    def test_vfat_set_uuid(self):
        """Verify that it is possible to set UUID/volume ID of an vfat file system"""

        if DOSFSTOOLS_VERSION <= Version("4.1"):
            self.skipTest("dosfstools >= 4.2 needed to set UUID")

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_set_uuid(self.loop_dev, "2E24EC82")
        self.assertTrue(succ)
        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, "2E24-EC82")

        # should be also support with the dash
        succ = BlockDev.fs_vfat_set_uuid(self.loop_dev, "2E24-EC82")
        self.assertTrue(succ)
        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, "2E24-EC82")

        succ = BlockDev.fs_vfat_set_uuid(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertTrue(fi.uuid)  # new random, not empty
        self.assertNotEqual(fi.uuid, "2E24-EC82")

        succ = BlockDev.fs_vfat_check_uuid("2E24EC82")
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check_uuid("2E24-EC82")
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check_uuid("0000-0000")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "must be a hexadecimal number."):
            BlockDev.fs_vfat_check_uuid("z")

        with self.assertRaisesRegex(GLib.GError, "must be a hexadecimal number."):
            BlockDev.fs_vfat_check_uuid("aaaa-")

        with self.assertRaisesRegex(GLib.GError, "must fit into 32 bits."):
            BlockDev.fs_vfat_check_uuid(10 * "f")


class VfatResize(VfatTestCase):
    def test_vfat_resize(self):
        """Verify that it is possible to resize an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, self._mkfs_options)
        self.assertTrue(succ)

        # shrink
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 130 * 1024**2)
        self.assertTrue(succ)

        # grow
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 140 * 1024**2)
        self.assertTrue(succ)

        # shrink again
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 130 * 1024**2)
        self.assertTrue(succ)

        # resize to maximum size
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 0)
        self.assertTrue(succ)
