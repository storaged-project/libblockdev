import tempfile
import re

from packaging.version import Version

from .fs_test import FSTestCase, FSNoDevTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


def _check_fsck_f2fs_version():
    # if it can run -V to get version it can do the check
    ret, _out, _err = utils.run_command("fsck.f2fs -V")
    return ret == 0


def _can_resize_f2fs():
    ret, out, _err = utils.run_command("resize.f2fs -V")
    if ret != 0:
        # we can't even check the version
        return False

    m = re.search(r"resize.f2fs ([\d\.]+)", out)
    if not m or len(m.groups()) != 1:
        raise RuntimeError("Failed to determine f2fs version from: %s" % out)
    return Version(m.groups()[0]) >= Version("1.12.0")


class F2FSNoDevTestCase(FSNoDevTestCase):
    def setUp(self):
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")

        super(F2FSNoDevTestCase, self).setUp()


class F2FSTestCase(FSTestCase):
    def setUp(self):
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")

        super(F2FSTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="f2fs_test")


class F2FSTestAvailability(F2FSNoDevTestCase):

    def test_f2fs_available(self):
        """Verify that it is possible to check f2fs tech availability"""
        available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.QUERY)
        self.assertTrue(available)

        with self.assertRaisesRegex(GLib.GError, "doesn't support setting UUID"):
            BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.SET_UUID)

        with self.assertRaisesRegex(GLib.GError, "doesn't support setting label"):
            BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.SET_LABEL)

        if _check_fsck_f2fs_version():
            available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS,
                                                  BlockDev.FSTechMode.CHECK |
                                                  BlockDev.FSTechMode.REPAIR)
            self.assertTrue(available)

        if _can_resize_f2fs():
            available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS,
                                                  BlockDev.FSTechMode.RESIZE)
            self.assertTrue(available)

        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkfs.f2fs
        with utils.fake_path(all_but="mkfs.f2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkfs.f2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.MKFS)

        # now try without fsck.f2fs
        with utils.fake_path(all_but="fsck.f2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'fsck.f2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.CHECK)

            with self.assertRaisesRegex(GLib.GError, "The 'fsck.f2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.REPAIR)

        # now try without dump.f2fs
        with utils.fake_path(all_but="dump.f2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'dump.f2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.QUERY)

        # now try without resize.f2fs
        with utils.fake_path(all_but="resize.f2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'resize.f2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.RESIZE)

    def test_f2fs_fsck_too_low(self):
        BlockDev.reinit(self.requested_plugins, True, None)

        # now try fake "low version of f2fs
        with utils.fake_utils("tests/fake_utils/fsck_f2fs_low_version/"):
            with self.assertRaisesRegex(GLib.GError, "Too low version of fsck.f2fs. At least 1.11.0 required."):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.CHECK)

class F2FSTestFeatures(F2FSNoDevTestCase):

    def test_f2fs_features(self):
        features = BlockDev.fs_features("f2fs")
        self.assertIsNotNone(features)

        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_GROW)
        self.assertTrue(features.resize & BlockDev.FSResizeFlags.OFFLINE_SHRINK)

        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.NODISCARD)
        self.assertTrue(features.mkfs & BlockDev.FSMkfsOptionsFlags.FORCE)
        self.assertFalse(features.mkfs & BlockDev.FSMkfsOptionsFlags.NOPT)

        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.CHECK)
        self.assertTrue(features.fsck & BlockDev.FSFsckFlags.REPAIR)

        self.assertEqual(features.configure, 0)

        self.assertTrue(features.features & BlockDev.FSFeatureFlags.OWNERS)
        self.assertFalse(features.features & BlockDev.FSFeatureFlags.PARTITION_TABLE)

        self.assertEqual(features.partition_id, "0x83")
        self.assertEqual(features.partition_type, "0fc63daf-8483-4772-8e79-3d69d8477de4")


class F2FSTestMkfs(F2FSTestCase):
    def test_f2fs_mkfs(self):
        """Verify that it is possible to create a new f2fs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_f2fs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "f2fs")

        BlockDev.fs_wipe(self.loop_dev, True)


class F2FSMkfsWithLabel(F2FSTestCase):
    def test_f2fs_mkfs_with_label(self):
        """Verify that it is possible to create an f2fs file system with label"""

        ea = BlockDev.ExtraArg.new("-l", "TEST_LABEL")
        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")


class F2FSMkfsWithFeatures(F2FSTestCase):
    def test_f2fs_mkfs_with_features(self):
        """Verify that it is possible to create an f2fs file system with extra features enabled"""

        ea = BlockDev.ExtraArg.new("-O", "encrypt")
        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertTrue(fi.features & BlockDev.FSF2FSFeature.ENCRYPT)


class F2FSTestCheck(F2FSTestCase):
    def test_f2fs_check(self):
        """Verify that it is possible to check an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        if not _check_fsck_f2fs_version():
            with self.assertRaisesRegex(GLib.GError, "Too low version of fsck.f2fs. At least 1.11.0 required."):
                BlockDev.fs_f2fs_check(self.loop_dev, None)
        else:
            succ = BlockDev.fs_f2fs_check(self.loop_dev, None)
            self.assertTrue(succ)


class F2FSTestRepair(F2FSTestCase):
    def test_f2fs_repair(self):
        """Verify that it is possible to repair an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_f2fs_repair(self.loop_dev, None)
        self.assertTrue(succ)


class F2FSGetInfo(F2FSTestCase):
    def test_f2fs_get_info(self):
        """Verify that it is possible to get info about an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)


class F2FSResize(F2FSTestCase):
    @tag_test(TestTags.UNSTABLE)
    def test_f2fs_resize(self):
        """Verify that it is possible to resize an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # shrink without the safe option -- should fail
        with self.assertRaises(GLib.GError):
            BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, False)

        # if we can't shrink we'll just check it returns some sane error
        if not _can_resize_f2fs():
            with self.assertRaisesRegex(GLib.GError, "Too low version of resize.f2fs. At least 1.12.0 required."):
                BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, True)
            return

        succ = BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, True)
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        if fi.sector_size == 0:
            # XXX latest versions of dump.f2fs don't print the sector size
            self.skipTest("Cannot get sector size of the f2fs filesystem, skipping")

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertEqual(fi.sector_count * fi.sector_size, 100 * 1024**2)

        # grow
        succ = BlockDev.fs_f2fs_resize(self.loop_dev, 120 * 1024**2 / 512, True)
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertEqual(fi.sector_count * fi.sector_size, 120 * 1024**2)

        # shrink again
        succ = BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, True)
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertEqual(fi.sector_count * fi.sector_size, 100 * 1024**2)

        # resize to maximum size
        succ = BlockDev.fs_f2fs_resize(self.loop_dev, 0, False)
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertEqual(fi.sector_count * fi.sector_size, self.loop_size)
