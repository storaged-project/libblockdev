import tempfile
import six

from .fs_test import FSTestCase, mounted, check_output

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class XfsTestCase(FSTestCase):
    def setUp(self):
        super(XfsTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="xfs_test")


class XfsTestAvailability(XfsTestCase):

    def setUp(self):
        super(XfsTestAvailability, self).setUp()

        # set everything back and reinit just to be sure
        self.addCleanup(BlockDev.switch_init_checks, True)
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

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

        BlockDev.switch_init_checks(False)
        BlockDev.reinit(self.requested_plugins, True, None)

        # now try without mkfs.xfs
        with utils.fake_path(all_but="mkfs.xfs"):
            with six.assertRaisesRegex(self, GLib.GError, "The 'mkfs.xfs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.MKFS)

        # now try without xfs_db
        with utils.fake_path(all_but="xfs_db"):
            with six.assertRaisesRegex(self, GLib.GError, "The 'xfs_db' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.CHECK)

        # now try without xfs_repair
        with utils.fake_path(all_but="xfs_repair"):
            with six.assertRaisesRegex(self, GLib.GError, "The 'xfs_repair' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.REPAIR)

        # now try without xfs_admin
        with utils.fake_path(all_but="xfs_admin"):
            with six.assertRaisesRegex(self, GLib.GError, "The 'xfs_admin' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.QUERY)

            with six.assertRaisesRegex(self, GLib.GError, "The 'xfs_admin' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.SET_LABEL)

            with six.assertRaisesRegex(self, GLib.GError, "The 'xfs_admin' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.SET_UUID)

        # now try without xfs_growfs
        with utils.fake_path(all_but="xfs_growfs"):
            with six.assertRaisesRegex(self, GLib.GError, "The 'xfs_growfs' utility is not available"):
                BlockDev.fs_is_tech_avail(BlockDev.FSTech.XFS, BlockDev.FSTechMode.RESIZE)


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


class XfsTestWipe(XfsTestCase):
    def test_xfs_wipe(self):
        """Verify that it is possible to wipe an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_xfs_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

        utils.run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an xfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        utils.run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an xfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

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

        with six.assertRaisesRegex(self, GLib.GError, "not mounted"):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)

        with mounted(self.loop_dev, self.mount_dir):
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

        with six.assertRaisesRegex(self, GLib.GError, "at most 12 characters long."):
            BlockDev.fs_xfs_check_label(13 * "a")

        with six.assertRaisesRegex(self, GLib.GError, "cannot contain spaces"):
            BlockDev.fs_xfs_check_label("TEST LABEL")


class XfsResize(XfsTestCase):
    def _destroy_lvm(self):
        utils.run("vgremove --yes libbd_fs_tests >/dev/null 2>&1")
        utils.run("pvremove --yes %s >/dev/null 2>&1" % self.loop_dev)

    def test_xfs_resize(self):
        """Verify that it is possible to resize an xfs file system"""

        utils.run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        utils.run("vgcreate -s10M libbd_fs_tests %s >/dev/null 2>&1" % self.loop_dev)
        utils.run("lvcreate -n xfs_test -L50M libbd_fs_tests >/dev/null 2>&1")
        self.addCleanup(self._destroy_lvm)
        lv = "/dev/libbd_fs_tests/xfs_test"

        succ = BlockDev.fs_xfs_mkfs(lv, None)
        self.assertTrue(succ)

        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 50 * 1024**2)

        # no change, nothing should happen
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)

        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 50 * 1024**2)

        # (still) impossible to shrink an XFS file system
        with mounted(lv, self.mount_dir):
            with self.assertRaises(GLib.GError):
                succ = BlockDev.fs_xfs_resize(self.mount_dir, 40 * 1024**2 / fi.block_size, None)

        utils.run("lvresize -L70M libbd_fs_tests/xfs_test >/dev/null 2>&1")
        # should grow
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 70 * 1024**2)

        utils.run("lvresize -L90M libbd_fs_tests/xfs_test >/dev/null 2>&1")
        # should grow just to 80 MiB
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 80 * 1024**2 / fi.block_size, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 80 * 1024**2)

        # should grow to 90 MiB
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 90 * 1024**2)


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

        with six.assertRaisesRegex(self, GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.fs_xfs_check_uuid("aaaaaaa")
