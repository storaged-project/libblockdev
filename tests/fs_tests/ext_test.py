import tempfile

from .fs_test import FSTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class ExtTestCase(FSTestCase):
    def setUp(self):
        super(ExtTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="ext_test")


class ExtTestAvailability(ExtTestCase):

    def setUp(self):
        super(ExtTestAvailability, self).setUp()

        # set everything back and reinit just to be sure
        self.addCleanup(BlockDev.switch_init_checks, True)
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    def _test_ext_available(self, tech):
        available = BlockDev.fs_is_tech_avail(tech,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.QUERY |
                                              BlockDev.FSTechMode.REPAIR |
                                              BlockDev.FSTechMode.CHECK |
                                              BlockDev.FSTechMode.SET_LABEL |
                                              BlockDev.FSTechMode.RESIZE |
                                              BlockDev.FSTechMode.SET_UUID)
        self.assertTrue(available)

        BlockDev.switch_init_checks(False)
        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mke2fs
        with utils.fake_path(all_but="mke2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'mke2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.MKFS)

        # now try without e2fsck
        with utils.fake_path(all_but="e2fsck"):
            with self.assertRaisesRegex(GLib.GError, "The 'e2fsck' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.CHECK)

            with self.assertRaisesRegex(GLib.GError, "The 'e2fsck' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.REPAIR)

        # now try without dumpe2fs
        with utils.fake_path(all_but="dumpe2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'dumpe2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.QUERY)

        # now try without tune2fs
        with utils.fake_path(all_but="tune2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'tune2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.SET_LABEL)

            with self.assertRaisesRegex(GLib.GError, "The 'tune2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.SET_UUID)

        # now try without resize2fs
        with utils.fake_path(all_but="resize2fs"):
            with self.assertRaisesRegex(GLib.GError, "The 'resize2fs' utility is not available"):
                BlockDev.fs_is_tech_avail(tech, BlockDev.FSTechMode.RESIZE)

    @tag_test(TestTags.CORE)
    def test_ext2_available(self):
        """Verify that it is possible to check ext2 tech availability"""
        self._test_ext_available(tech=BlockDev.FSTech.EXT2)

    @tag_test(TestTags.CORE)
    def test_ext3_available(self):
        """Verify that it is possible to check ext3 tech availability"""
        self._test_ext_available(tech=BlockDev.FSTech.EXT3)

    @tag_test(TestTags.CORE)
    def test_ext4_available(self):
        """Verify that it is possible to check ext4 tech availability"""
        self._test_ext_available(tech=BlockDev.FSTech.EXT4)


class ExtTestMkfs(ExtTestCase):
    def _test_ext_mkfs(self, mkfs_function, ext_version):
        with self.assertRaises(GLib.GError):
            mkfs_function("/non/existing/device", None)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, ext_version)

        BlockDev.fs_wipe(self.loop_dev, True)

    @tag_test(TestTags.CORE)
    def test_ext2_mkfs(self):
        """Verify that it is possible to create a new ext2 file system"""
        self._test_ext_mkfs(mkfs_function=BlockDev.fs_ext2_mkfs,
                            ext_version="ext2")

    @tag_test(TestTags.CORE)
    def test_ext3_mkfs(self):
        """Verify that it is possible to create a new ext3 file system"""
        self._test_ext_mkfs(mkfs_function=BlockDev.fs_ext3_mkfs,
                            ext_version="ext3")

    @tag_test(TestTags.CORE)
    def test_ext4_mkfs(self):
        """Verify that it is possible to create a new ext4 file system"""
        self._test_ext_mkfs(mkfs_function=BlockDev.fs_ext4_mkfs,
                            ext_version="ext4")


class ExtMkfsWithLabel(ExtTestCase):
    def _test_ext_mkfs_with_label(self, mkfs_function, info_function):
        ea = BlockDev.ExtraArg.new("-L", "TEST_LABEL")
        succ = mkfs_function(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

    def test_ext2_mkfs_with_label(self):
        """Verify that it is possible to create an ext2 file system with label"""
        self._test_ext_mkfs_with_label(mkfs_function=BlockDev.fs_ext2_mkfs,
                                       info_function=BlockDev.fs_ext2_get_info)

    def test_ext3_mkfs_with_label(self):
        """Verify that it is possible to create an ext3 file system with label"""
        self._test_ext_mkfs_with_label(mkfs_function=BlockDev.fs_ext3_mkfs,
                                       info_function=BlockDev.fs_ext3_get_info)

    def test_ext4_mkfs_with_label(self):
        """Verify that it is possible to create an ext4 file system with label"""
        self._test_ext_mkfs_with_label(mkfs_function=BlockDev.fs_ext4_mkfs,
                                       info_function=BlockDev.fs_ext4_get_info)


class ExtTestWipe(ExtTestCase):
    def _test_ext_wipe(self, mkfs_function, wipe_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        succ = wipe_function(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            wipe_function(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an ext4 file system
        with self.assertRaises(GLib.GError):
            wipe_function(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)

        # vfat, not an ext4 file system
        with self.assertRaises(GLib.GError):
            wipe_function(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

    def test_ext2_wipe(self):
        """Verify that it is possible to wipe an ext2 file system"""
        self._test_ext_wipe(mkfs_function=BlockDev.fs_ext2_mkfs,
                            wipe_function=BlockDev.fs_ext2_wipe)

    def test_ext3_wipe(self):
        """Verify that it is possible to wipe an ext3 file system"""
        self._test_ext_wipe(mkfs_function=BlockDev.fs_ext3_mkfs,
                            wipe_function=BlockDev.fs_ext3_wipe)

    def test_ext4_wipe(self):
        """Verify that it is possible to wipe an ext4 file system"""
        self._test_ext_wipe(mkfs_function=BlockDev.fs_ext4_mkfs,
                            wipe_function=BlockDev.fs_ext4_wipe)


class ExtTestCheck(ExtTestCase):
    def _test_ext_check(self, mkfs_function, check_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        succ = check_function(self.loop_dev, None)
        self.assertTrue(succ)

        # mounted, but can be checked
        with mounted(self.loop_dev, self.mount_dir):
            succ = check_function(self.loop_dev, None)
            self.assertTrue(succ)

        succ = check_function(self.loop_dev, None)
        self.assertTrue(succ)

    def test_ext2_check(self):
        """Verify that it is possible to check an ext2 file system"""
        self._test_ext_check(mkfs_function=BlockDev.fs_ext2_mkfs,
                             check_function=BlockDev.fs_ext2_check)

    def test_ext3_check(self):
        """Verify that it is possible to check an ext3 file system"""
        self._test_ext_check(mkfs_function=BlockDev.fs_ext3_mkfs,
                             check_function=BlockDev.fs_ext3_check)

    def test_ext4_check(self):
        """Verify that it is possible to check an ext4 file system"""
        self._test_ext_check(mkfs_function=BlockDev.fs_ext4_mkfs,
                             check_function=BlockDev.fs_ext4_check)


class ExtTestRepair(ExtTestCase):
    def _test_ext_repair(self, mkfs_function, repair_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        succ = repair_function(self.loop_dev, False, None)
        self.assertTrue(succ)

        # unsafe operations should work here too
        succ = repair_function(self.loop_dev, True, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            with self.assertRaises(GLib.GError):
                repair_function(self.loop_dev, False, None)

        succ = repair_function(self.loop_dev, False, None)
        self.assertTrue(succ)

    def test_ext2_repair(self):
        """Verify that it is possible to repair an ext2 file system"""
        self._test_ext_repair(mkfs_function=BlockDev.fs_ext2_mkfs,
                              repair_function=BlockDev.fs_ext2_repair)

    def test_ext3_repair(self):
        """Verify that it is possible to repair an ext3 file system"""
        self._test_ext_repair(mkfs_function=BlockDev.fs_ext3_mkfs,
                              repair_function=BlockDev.fs_ext3_repair)

    def test_ext4_repair(self):
        """Verify that it is possible to repair an ext4 file system"""
        self._test_ext_repair(mkfs_function=BlockDev.fs_ext4_mkfs,
                              repair_function=BlockDev.fs_ext4_repair)


class ExtGetInfo(ExtTestCase):
    def _test_ext_get_info(self, mkfs_function, info_function):
        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, self.loop_size / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * self.loop_size / 1024)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertTrue(fi.state, "clean")

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_ext4_get_info(self.loop_dev)
            self.assertEqual(fi.block_size, 1024)
            self.assertEqual(fi.block_count, self.loop_size / 1024)
            # at least 90 % should be available, so it should be reported
            self.assertGreater(fi.free_blocks, 0.90 * self.loop_size / 1024)
            self.assertEqual(fi.label, "")
            # should be an non-empty string
            self.assertTrue(fi.uuid)
            self.assertTrue(fi.state, "clean")

    @tag_test(TestTags.CORE)
    def test_ext2_get_info(self):
        """Verify that it is possible to get info about an ext2 file system"""
        self._test_ext_get_info(mkfs_function=BlockDev.fs_ext2_mkfs,
                                info_function=BlockDev.fs_ext2_get_info)

    @tag_test(TestTags.CORE)
    def test_ext3_get_info(self):
        """Verify that it is possible to get info about an ext3 file system"""
        self._test_ext_get_info(mkfs_function=BlockDev.fs_ext3_mkfs,
                                info_function=BlockDev.fs_ext3_get_info)

    @tag_test(TestTags.CORE)
    def test_ext4_get_info(self):
        """Verify that it is possible to get info about an ext4 file system"""
        self._test_ext_get_info(mkfs_function=BlockDev.fs_ext4_mkfs,
                                info_function=BlockDev.fs_ext4_get_info)


class ExtSetLabel(ExtTestCase):
    def _test_ext_set_label(self, mkfs_function, info_function, label_function, check_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = label_function(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = label_function(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = label_function(self.loop_dev, "")
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = check_function("TEST_LABEL")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 16 characters long."):
            check_function(20 * "a")

    def test_ext2_set_label(self):
        """Verify that it is possible to set label of an ext2 file system"""
        self._test_ext_set_label(mkfs_function=BlockDev.fs_ext2_mkfs,
                                 info_function=BlockDev.fs_ext2_get_info,
                                 label_function=BlockDev.fs_ext2_set_label,
                                 check_function=BlockDev.fs_ext2_check_label)

    def test_ext3_set_label(self):
        """Verify that it is possible to set label of an ext3 file system"""
        self._test_ext_set_label(mkfs_function=BlockDev.fs_ext3_mkfs,
                                 info_function=BlockDev.fs_ext3_get_info,
                                 label_function=BlockDev.fs_ext3_set_label,
                                 check_function=BlockDev.fs_ext3_check_label)

    def test_ext4_set_label(self):
        """Verify that it is possible to set label of an ext4 file system"""
        self._test_ext_set_label(mkfs_function=BlockDev.fs_ext4_mkfs,
                                 info_function=BlockDev.fs_ext4_get_info,
                                 label_function=BlockDev.fs_ext4_set_label,
                                 check_function=BlockDev.fs_ext4_check_label)


class ExtResize(ExtTestCase):
    def _test_ext_resize(self, mkfs_function, info_function, resize_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, self.loop_size / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * self.loop_size / 1024)

        succ = resize_function(self.loop_dev, 50 * 1024**2, None)
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 50 * 1024**2 / 1024)

        # resize back
        succ = resize_function(self.loop_dev, self.loop_size, None)
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, self.loop_size / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * self.loop_size / 1024)

        # resize again
        succ = resize_function(self.loop_dev, 50 * 1024**2, None)
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 50 * 1024**2 / 1024)

        # resize back again, this time to maximum size
        succ = resize_function(self.loop_dev, 0, None)
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, self.loop_size / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * self.loop_size / 1024)

    def test_ext2_resize(self):
        """Verify that it is possible to resize an ext2 file system"""
        self._test_ext_resize(mkfs_function=BlockDev.fs_ext2_mkfs,
                              info_function=BlockDev.fs_ext2_get_info,
                              resize_function=BlockDev.fs_ext2_resize)

    def test_ext3_resize(self):
        """Verify that it is possible to resize an ext3 file system"""
        self._test_ext_resize(mkfs_function=BlockDev.fs_ext3_mkfs,
                              info_function=BlockDev.fs_ext3_get_info,
                              resize_function=BlockDev.fs_ext3_resize)

    def test_ext4_resize(self):
        """Verify that it is possible to resize an ext4 file system"""
        self._test_ext_resize(mkfs_function=BlockDev.fs_ext4_mkfs,
                              info_function=BlockDev.fs_ext4_get_info,
                              resize_function=BlockDev.fs_ext4_resize)


class ExtSetUUID(ExtTestCase):

    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def _test_ext_set_uuid(self, mkfs_function, info_function, label_function, check_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        fi = info_function(self.loop_dev)
        self.assertTrue(fi)

        succ = label_function(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        succ = label_function(self.loop_dev, "clear")
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, "")

        succ = label_function(self.loop_dev, "random")
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        random_uuid = fi.uuid

        succ = label_function(self.loop_dev, "time")
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, random_uuid)
        time_uuid = fi.uuid

        # no UUID -> random
        succ = label_function(self.loop_dev, None)
        self.assertTrue(succ)
        fi = info_function(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, time_uuid)

        succ = check_function(self.test_uuid)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "not a valid RFC-4122 UUID"):
            check_function("aaaaaaa")

    def test_ext2_set_uuid(self):
        """Verify that it is possible to set UUID of an ext2 file system"""
        self._test_ext_set_uuid(mkfs_function=BlockDev.fs_ext2_mkfs,
                                info_function=BlockDev.fs_ext2_get_info,
                                label_function=BlockDev.fs_ext2_set_uuid,
                                check_function=BlockDev.fs_ext2_check_uuid)

    def test_ext3_set_uuid(self):
        """Verify that it is possible to set UUID of an ext3 file system"""
        self._test_ext_set_uuid(mkfs_function=BlockDev.fs_ext3_mkfs,
                                info_function=BlockDev.fs_ext3_get_info,
                                label_function=BlockDev.fs_ext3_set_uuid,
                                check_function=BlockDev.fs_ext3_check_uuid)

    def test_ext4_set_uuid(self):
        """Verify that it is possible to set UUID of an ext4 file system"""
        self._test_ext_set_uuid(mkfs_function=BlockDev.fs_ext4_mkfs,
                                info_function=BlockDev.fs_ext4_get_info,
                                label_function=BlockDev.fs_ext4_set_uuid,
                                check_function=BlockDev.fs_ext4_check_uuid)
