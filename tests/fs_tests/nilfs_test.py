import tempfile

from .fs_test import FSTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class NILFS2TestCase(FSTestCase):
    def setUp(self):
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")

        super(NILFS2TestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="nilfs2_test")


class NILFS2TestAvailability(NILFS2TestCase):

    def setUp(self):
        super(NILFS2TestAvailability, self).setUp()

        # set everything back and reinit just to be sure
        self.addCleanup(BlockDev.switch_init_checks, True)
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    def test_nilfs2_available(self):
        """Verify that it is possible to check nilfs2 tech availability"""
        available = BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2,
                                              BlockDev.FSTechMode.MKFS |
                                              BlockDev.FSTechMode.SET_LABEL |
                                              BlockDev.FSTechMode.QUERY |
                                              BlockDev.FSTechMode.RESIZE |
                                              BlockDev.FSTechMode.SET_UUID)
        self.assertTrue(available)

        with self.assertRaisesRegex(GLib.GError, "doesn't support filesystem check"):
            BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2, BlockDev.FSTechMode.CHECK)

        with self.assertRaisesRegex(GLib.GError, "doesn't support filesystem repair"):
            BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2, BlockDev.FSTechMode.REPAIR)

        BlockDev.switch_init_checks(False)
        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkfs.nilfs2
        with utils.fake_path(all_but="mkfs.nilfs2"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkfs.nilfs2' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2, BlockDev.FSTechMode.MKFS)

        # now try without nilfs-tune
        with utils.fake_path(all_but="nilfs-tune"):
            with self.assertRaisesRegex(GLib.GError, "The 'nilfs-tune' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2, BlockDev.FSTechMode.QUERY)

            with self.assertRaisesRegex(GLib.GError, "The 'nilfs-tune' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2, BlockDev.FSTechMode.SET_LABEL)

        # now try without nilfs-resize
        with utils.fake_path(all_but="nilfs-resize"):
            with self.assertRaisesRegex(GLib.GError, "The 'nilfs-resize' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2, BlockDev.FSTechMode.RESIZE)


class NILFS2TestMkfs(NILFS2TestCase):
    def test_nilfs2_mkfs(self):
        """Verify that it is possible to create a new nilfs2 file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_nilfs2_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "nilfs2")

        BlockDev.fs_wipe(self.loop_dev, True)


class NILFS2MkfsWithLabel(NILFS2TestCase):
    def test_nilfs2_mkfs_with_label(self):
        """Verify that it is possible to create an nilfs2 file system with label"""

        ea = BlockDev.ExtraArg.new("-L", "test_label")
        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")


class NILFS2TestWipe(NILFS2TestCase):
    def test_nilfs2_wipe(self):
        """Verify that it is possible to wipe an nilfs2 file system"""

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_nilfs2_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_nilfs2_wipe(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an nilfs2 file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_nilfs2_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an nilfs2 file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_nilfs2_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)


class NILFS2GetInfo(NILFS2TestCase):
    def test_nilfs2_get_info(self):
        """Verify that it is possible to get info about an nilfs2 file system"""

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertGreater(fi.block_size, 0)
        self.assertGreater(fi.size, 0)
        self.assertLess(fi.free_blocks * fi.block_size, fi.size)


class NILFS2SetLabel(NILFS2TestCase):
    def test_nilfs2_set_label(self):
        """Verify that it is possible to set label of an nilfs2 file system"""

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_nilfs2_set_label(self.loop_dev, "test_label")
        self.assertTrue(succ)
        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")

        succ = BlockDev.fs_nilfs2_set_label(self.loop_dev, "test_label2")
        self.assertTrue(succ)
        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label2")

        succ = BlockDev.fs_nilfs2_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_nilfs2_check_label("test_label")
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 80 characters long."):
            BlockDev.fs_nilfs2_check_label(81 * "a")


class NILFS2Resize(NILFS2TestCase):
    def test_nilfs2_resize(self):
        """Verify that it is possible to resize an nilfs2 file system"""

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "is not currently mounted"):
            BlockDev.fs_nilfs2_resize(self.loop_dev, 100 * 1024**2)

        with mounted(self.loop_dev, self.mount_dir):
            # shrink
            succ = BlockDev.fs_nilfs2_resize(self.loop_dev, 100 * 1024**2)
            self.assertTrue(succ)

            # grow
            succ = BlockDev.fs_nilfs2_resize(self.loop_dev, 120 * 1024**2)
            self.assertTrue(succ)

            # shrink again
            succ = BlockDev.fs_nilfs2_resize(self.loop_dev, 100 * 1024**2)
            self.assertTrue(succ)

            # resize to maximum size
            succ = BlockDev.fs_nilfs2_resize(self.loop_dev, 0)
            self.assertTrue(succ)


class NILFS2SetUUID(NILFS2TestCase):

    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def test_nilfs2_set_uuid(self):
        """Verify that it is possible to set UUID of an nilfs2 file system"""

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_nilfs2_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        # no uuid -> random
        succ = BlockDev.fs_nilfs2_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_nilfs2_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_nilfs2_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.fs_nilfs2_check_uuid("aaaaaaa")
