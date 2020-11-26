import tempfile
import six

from .fs_test import FSTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class ExfatTestCase(FSTestCase):
    def setUp(self):
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")

        super(ExfatTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="exfat_test")


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


class ExfatTestWipe(ExfatTestCase):
    def test_exfat_wipe(self):
        """Verify that it is possible to wipe an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_wipe(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an exfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an exfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)


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

        with six.assertRaisesRegex(self, GLib.GError, "at most 11 characters long."):
            BlockDev.fs_exfat_check_label(12 * "a")
