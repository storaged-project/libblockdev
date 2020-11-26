import tempfile
import six

from .fs_test import FSTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class VfatTestCase(FSTestCase):
    def setUp(self):
        super(VfatTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="vfat_test")


class VfatTestMkfs(VfatTestCase):
    def test_vfat_mkfs(self):
        """Verify that it is possible to create a new vfat file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
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
        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")


class VfatTestWipe(VfatTestCase):
    def test_vfat_wipe(self):
        """Verify that it is possible to wipe an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an vfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an vfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)


class VfatTestCheck(VfatTestCase):
    def test_vfat_check(self):
        """Verify that it is possible to check an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check(self.loop_dev, None)
        self.assertTrue(succ)


class VfatTestRepair(VfatTestCase):
    def test_vfat_repair(self):
        """Verify that it is possible to repair an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_repair(self.loop_dev, None)
        self.assertTrue(succ)


class VfatGetInfo(VfatTestCase):
    def test_vfat_get_info(self):
        """Verify that it is possible to get info about an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)


class VfatSetLabel(VfatTestCase):
    def test_vfat_set_label(self):
        """Verify that it is possible to set label of an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
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

        with six.assertRaisesRegex(self, GLib.GError, "at most 11 characters long."):
            BlockDev.fs_vfat_check_label(12 * "a")


class VfatResize(VfatTestCase):
    def test_vfat_resize(self):
        """Verify that it is possible to resize an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
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
