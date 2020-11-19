import unittest
import os
import time
import subprocess
import tempfile
from contextlib import contextmanager
import utils
from utils import run, create_sparse_tempfile, mount, umount, TestTags, tag_test
import re
import six
import overrides_hack

from distutils.version import LooseVersion

from gi.repository import BlockDev, GLib


def check_output(args, ignore_retcode=True):
    """Just like subprocess.check_output(), but allows the return code of the process to be ignored"""

    try:
        return subprocess.check_output(args)
    except subprocess.CalledProcessError as e:
        if ignore_retcode:
            return e.output
        else:
            raise

@contextmanager
def mounted(device, where, ro=False):
    mount(device, where, ro)
    yield
    umount(where)

class FSTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("fs", "loop"))
    loop_size = 150 * 1024**2

    @classmethod
    def setUpClass(cls):
        BlockDev.switch_init_checks(False)
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)
        BlockDev.switch_init_checks(True)
        try:
            cls.ntfs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS,
                                                       BlockDev.FSTechMode.MKFS |
                                                       BlockDev.FSTechMode.RESIZE |
                                                       BlockDev.FSTechMode.REPAIR |
                                                       BlockDev.FSTechMode.CHECK |
                                                       BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.ntfs_avail = False

        try:
            # check only for mkfs, we have special checks for resize and check
            cls.f2fs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.MKFS)
        except:
            cls.f2fs_avail = False

        try:
            cls.reiserfs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.REISERFS,
                                                           BlockDev.FSTechMode.MKFS |
                                                           BlockDev.FSTechMode.RESIZE |
                                                           BlockDev.FSTechMode.REPAIR |
                                                           BlockDev.FSTechMode.CHECK |
                                                           BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.reiserfs_avail = False

        try:
            cls.nilfs2_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2,
                                                         BlockDev.FSTechMode.MKFS |
                                                         BlockDev.FSTechMode.RESIZE |
                                                         BlockDev.FSTechMode.SET_LABEL)
        except Exception:
            cls.nilfs2_avail = False

        try:
            cls.exfat_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT,
                                                        BlockDev.FSTechMode.MKFS |
                                                        BlockDev.FSTechMode.REPAIR |
                                                        BlockDev.FSTechMode.CHECK |
                                                        BlockDev.FSTechMode.SET_LABEL)
        except Exception :
            cls.exfat_avail = False

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = utils.create_sparse_tempfile("fs_test", self.loop_size)
        self.dev_file2 = utils.create_sparse_tempfile("fs_test", self.loop_size)
        try:
            self.loop_dev = utils.create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev2 = utils.create_lio_device(self.dev_file2)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="ext4_test")

    def _clean_up(self):
        try:
            utils.delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)

        try:
            utils.delete_lio_device(self.loop_dev2)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file2)

        try:
            umount(self.mount_dir)
        except:
            pass

    def setro(self, device):
        ret, _out, _err = utils.run_command("blockdev --setro %s" % device)
        if ret != 0:
            self.fail("Failed to set %s read-only" % device)

    def setrw(self, device):
        ret, _out, _err = utils.run_command("blockdev --setrw %s" % device)
        if ret != 0:
            self.fail("Failed to set %s read-write" % device)

class TestGenericWipe(FSTestCase):
    @tag_test(TestTags.CORE)
    def test_generic_wipe(self):
        """Verify that generic signature wipe works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_wipe("/non/existing/device", True)

        ret = run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        # now test the same multiple times in a row
        for i in range(10):
            ret = run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
            self.assertEqual(ret, 0)

            succ = BlockDev.fs_wipe(self.loop_dev, True)
            self.assertTrue(succ)

        # vfat has multiple signatures on the device so it allows us to test the
        # 'all' argument of fs_wipe()
        ret = run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        time.sleep(0.5)
        succ = BlockDev.fs_wipe(self.loop_dev, False)
        self.assertTrue(succ)

        # the second signature should still be there
        # XXX: lsblk uses the udev db so it we need to make sure it is up to date
        run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"vfat")

        # get rid of all the remaining signatures (there could be vfat + PMBR for some reason)
        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # now do the wipe all in a one step
        ret = run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # try to wipe empty device
        with six.assertRaisesRegex(self, GLib.GError, "No signature detected on the device"):
            BlockDev.fs_wipe(self.loop_dev, True)


class TestClean(FSTestCase):
    def test_clean(self):
        """Verify that device clean works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_clean("/non/existing/device")

        # empty device shouldn't fail
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        ret = run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        # XXX: lsblk uses the udev db so it we need to make sure it is up to date
        run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # vfat has multiple signatures on the device so it allows us to test
        # that clean removes all signatures
        ret = run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        time.sleep(0.5)
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")


class ExtTestMkfs(FSTestCase):
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

class ExtMkfsWithLabel(FSTestCase):
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

class ExtTestWipe(FSTestCase):
    def _test_ext_wipe(self, mkfs_function, wipe_function):
        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        succ = wipe_function(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            wipe_function(self.loop_dev)

        run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an ext4 file system
        with self.assertRaises(GLib.GError):
            wipe_function(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)

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

class ExtTestCheck(FSTestCase):
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

class ExtTestRepair(FSTestCase):
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

class ExtGetInfo(FSTestCase):
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

class ExtSetLabel(FSTestCase):
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

        with six.assertRaisesRegex(self, GLib.GError, "at most 16 characters long."):
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

class ExtResize(FSTestCase):
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

class ExtSetUUID(FSTestCase):

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

        with six.assertRaisesRegex(self, GLib.GError, "not a valid RFC-4122 UUID"):
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

class XfsTestMkfs(FSTestCase):
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

class XfsTestWipe(FSTestCase):
    def test_xfs_wipe(self):
        """Verify that it is possible to wipe an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_xfs_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

        run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an xfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an xfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class XfsTestCheck(FSTestCase):
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

class XfsTestRepair(FSTestCase):
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

class XfsGetInfo(FSTestCase):
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

class XfsSetLabel(FSTestCase):
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

class XfsResize(FSTestCase):
    def _destroy_lvm(self):
        run("vgremove --yes libbd_fs_tests >/dev/null 2>&1")
        run("pvremove --yes %s >/dev/null 2>&1" % self.loop_dev)

    def test_xfs_resize(self):
        """Verify that it is possible to resize an xfs file system"""

        run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        run("vgcreate -s10M libbd_fs_tests %s >/dev/null 2>&1" % self.loop_dev)
        run("lvcreate -n xfs_test -L50M libbd_fs_tests >/dev/null 2>&1")
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

        run("lvresize -L70M libbd_fs_tests/xfs_test >/dev/null 2>&1")
        # should grow
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 70 * 1024**2)

        run("lvresize -L90M libbd_fs_tests/xfs_test >/dev/null 2>&1")
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

class XfsSetUUID(FSTestCase):

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

class VfatTestMkfs(FSTestCase):
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

class VfatMkfsWithLabel(FSTestCase):
    def test_vfat_mkfs_with_label(self):
        """Verify that it is possible to create an vfat file system with label"""

        ea = BlockDev.ExtraArg.new("-n", "TEST_LABEL")
        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

class VfatTestWipe(FSTestCase):
    def test_vfat_wipe(self):
        """Verify that it is possible to wipe an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an vfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an vfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class VfatTestCheck(FSTestCase):
    def test_vfat_check(self):
        """Verify that it is possible to check an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_check(self.loop_dev, None)
        self.assertTrue(succ)

class VfatTestRepair(FSTestCase):
    def test_vfat_repair(self):
        """Verify that it is possible to repair an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_vfat_repair(self.loop_dev, None)
        self.assertTrue(succ)

class VfatGetInfo(FSTestCase):
    def test_vfat_get_info(self):
        """Verify that it is possible to get info about an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_vfat_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)

class VfatSetLabel(FSTestCase):
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

class VfatResize(FSTestCase):
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

class ReiserFSTestCase(FSTestCase):
    def setUp(self):
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")

        super(ReiserFSTestCase, self).setUp()

class ReiserFSTestMkfs(ReiserFSTestCase):
    def test_reiserfs_mkfs(self):
        """Verify that it is possible to create a new reiserfs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "reiserfs")

        BlockDev.fs_wipe(self.loop_dev, True)

class ReiserFSMkfsWithLabel(ReiserFSTestCase):
    def test_reiserfs_mkfs_with_label(self):
        """Verify that it is possible to create an reiserfs file system with label"""

        ea = BlockDev.ExtraArg.new("-l", "test_label")
        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")

class ReiserFSTestWipe(ReiserFSTestCase):
    def test_reiserfs_wipe(self):
        """Verify that it is possible to wipe an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_wipe(self.loop_dev)

        run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an reiserfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an reiserfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_reiserfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class ReiserFSTestCheck(ReiserFSTestCase):
    def test_reiserfs_check(self):
        """Verify that it is possible to check an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_check(self.loop_dev, None)
        self.assertTrue(succ)

class ReiserFSTestRepair(ReiserFSTestCase):
    def test_reiserfs_repair(self):
        """Verify that it is possible to repair an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_repair(self.loop_dev, None)
        self.assertTrue(succ)

class ReiserFSGetInfo(ReiserFSTestCase):
    def test_reiserfs_get_info(self):
        """Verify that it is possible to get info about an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertGreater(fi.block_size, 0)
        self.assertGreater(fi.block_count, 0)
        self.assertLess(fi.free_blocks, fi.block_count)

class ReiserFSSetLabel(ReiserFSTestCase):
    def test_reiserfs_set_label(self):
        """Verify that it is possible to set label of an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_reiserfs_set_label(self.loop_dev, "test_label")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label")

        succ = BlockDev.fs_reiserfs_set_label(self.loop_dev, "test_label2")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "test_label2")

        succ = BlockDev.fs_reiserfs_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_reiserfs_check_label("test_label")
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, "at most 16 characters long."):
            BlockDev.fs_reiserfs_check_label(17 * "a")

class ReiserFSResize(ReiserFSTestCase):
    def test_reiserfs_resize(self):
        """Verify that it is possible to resize an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # shrink
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)

        # grow
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 100 * 1024**2)
        self.assertTrue(succ)

        # shrink again
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)


        # resize to maximum size
        succ = BlockDev.fs_reiserfs_resize(self.loop_dev, 0)
        self.assertTrue(succ)

class ReiserFSSetUUID(ReiserFSTestCase):

    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def test_reiserfs_set_uuid(self):
        """Verify that it is possible to set UUID of an reiserfs file system"""

        succ = BlockDev.fs_reiserfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_reiserfs_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_reiserfs_set_uuid(self.loop_dev, "random")
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, self.test_uuid)
        random_uuid = fi.uuid

        # no uuid -> random
        succ = BlockDev.fs_reiserfs_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_reiserfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, random_uuid)

        succ = BlockDev.fs_reiserfs_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.fs_reiserfs_check_uuid("aaaaaaa")

class F2FSTestCase(FSTestCase):
    def setUp(self):
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")

        super(F2FSTestCase, self).setUp()

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
    def test_f2fs_mkfs_with_label(self):
        """Verify that it is possible to create an f2fs file system with extra features enabled"""

        ea = BlockDev.ExtraArg.new("-O", "encrypt")
        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_f2fs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertTrue(fi.features & BlockDev.FSF2FSFeature.ENCRYPT)

class F2FSTestWipe(F2FSTestCase):
    def test_f2fs_wipe(self):
        """Verify that it is possible to wipe an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_f2fs_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_f2fs_wipe(self.loop_dev)

        run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an f2fs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_f2fs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an f2fs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_f2fs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class F2FSTestCheck(F2FSTestCase):
    def _check_fsck_f2fs_version(self):
        # if it can run -V to get version it can do the check
        ret, _out, _err = utils.run_command("fsck.f2fs -V")
        return ret == 0

    def test_f2fs_check(self):
        """Verify that it is possible to check an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        if not self._check_fsck_f2fs_version():
            with six.assertRaisesRegex(self, GLib.GError, "Too low version of fsck.f2fs. At least 1.11.0 required."):
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
    def _can_resize_f2fs(self):
        ret, out, _err = utils.run_command("resize.f2fs -V")
        if ret != 0:
            # we can't even check the version
            return False

        m = re.search(r"resize.f2fs ([\d\.]+)", out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError("Failed to determine f2fs version from: %s" % out)
        return LooseVersion(m.groups()[0]) >= LooseVersion("1.12.0")

    @tag_test(TestTags.UNSTABLE)
    def test_f2fs_resize(self):
        """Verify that it is possible to resize an f2fs file system"""

        succ = BlockDev.fs_f2fs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # shrink without the safe option -- should fail
        with self.assertRaises(GLib.GError):
            BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, False)

        # if we can't shrink we'll just check it returns some sane error
        if not self._can_resize_f2fs():
            with six.assertRaisesRegex(self, GLib.GError, "Too low version of resize.f2fs. At least 1.12.0 required."):
                BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, True)
            return

        succ = BlockDev.fs_f2fs_resize(self.loop_dev, 100 * 1024**2 / 512, True)
        self.assertTrue(succ)

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


class NTFSSetLabel(FSTestCase):
    def test_ntfs_set_label(self):
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

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

        with six.assertRaisesRegex(self, GLib.GError, "at most 128 characters long."):
            BlockDev.fs_ntfs_check_label(129 * "a")


class NTFSSetUUID(ReiserFSTestCase):

    test_uuid = "54E1629A44FD724B"

    def test_ntfs_set_uuid(self):
        """Verify that it is possible to set UUID of an ntfs file system"""

        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

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

        with six.assertRaisesRegex(self, GLib.GError, "8 or 16 characters long"):
            BlockDev.fs_ntfs_check_uuid(9 * "a")

        with six.assertRaisesRegex(self, GLib.GError, "must be a hexadecimal number"):
            BlockDev.fs_ntfs_check_uuid(16 * "z")



class ExfatTestCase(FSTestCase):
    def setUp(self):
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")

        super(ExfatTestCase, self).setUp()

class ExfatTestMkfs(ExfatTestCase):
    def test_exfat_mkfs(self):
        """Verify that it is possible to create a new exfat file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
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

        run("pvcreate -ff -y %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an exfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        run("mkfs.ext2 -F %s >/dev/null 2>&1" % self.loop_dev)

        # ext2, not an exfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_exfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class ExfatTestCheck(ExfatTestCase):
    def test_exfat_check(self):
        """Verify that it is possible to check an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_check(self.loop_dev, None)
        self.assertTrue(succ)

class ExfatTestRepair(ExfatTestCase):
    def test_exfat_repair(self):
        """Verify that it is possible to repair an exfat file system"""

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_exfat_repair(self.loop_dev, None)
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

class CanResizeRepairCheckLabel(FSTestCase):
    def test_can_resize(self):
        """Verify that tooling query works for resize"""

        avail, mode, util = BlockDev.fs_can_resize("ext4")
        self.assertTrue(avail)
        self.assertEqual(util, None)

        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        avail, mode, util = BlockDev.fs_can_resize("ext4")
        os.environ["PATH"] = old_path
        self.assertFalse(avail)
        self.assertEqual(util, "resize2fs")
        self.assertEqual(mode, BlockDev.FsResizeFlags.ONLINE_GROW |
                               BlockDev.FsResizeFlags.OFFLINE_GROW |
                               BlockDev.FsResizeFlags.OFFLINE_SHRINK)

        avail = BlockDev.fs_can_resize("vfat")
        self.assertTrue(avail)

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_resize("non-existing-fs")

    def test_can_repair(self):
        """Verify that tooling query works for repair"""

        avail, util = BlockDev.fs_can_repair("xfs")
        self.assertTrue(avail)
        self.assertEqual(util, None)

        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        avail, util = BlockDev.fs_can_repair("xfs")
        os.environ["PATH"] = old_path
        self.assertFalse(avail)
        self.assertEqual(util, "xfs_repair")

        avail = BlockDev.fs_can_repair("vfat")
        self.assertTrue(avail)

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_repair("nilfs2")

    def test_can_check(self):
        """Verify that tooling query works for consistency check"""

        avail, util = BlockDev.fs_can_check("xfs")
        self.assertTrue(avail)
        self.assertEqual(util, None)

        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        avail, util = BlockDev.fs_can_check("xfs")
        os.environ["PATH"] = old_path
        self.assertFalse(avail)
        self.assertEqual(util, "xfs_db")

        avail = BlockDev.fs_can_check("vfat")
        self.assertTrue(avail)

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_check("nilfs2")

    def test_can_set_label(self):
        """Verify that tooling query works for setting the label"""

        avail, util = BlockDev.fs_can_set_label("xfs")
        self.assertTrue(avail)
        self.assertEqual(util, None)

        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        avail, util = BlockDev.fs_can_set_label("xfs")
        os.environ["PATH"] = old_path
        self.assertFalse(avail)
        self.assertEqual(util, "xfs_admin")

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_set_label("non-existing-fs")

    def test_can_get_size(self):
        """Verify that tooling query works for getting size"""

        avail, util = BlockDev.fs_can_get_size("xfs")
        self.assertTrue(avail)
        self.assertEqual(util, None)

        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        avail, util = BlockDev.fs_can_get_size("xfs")
        os.environ["PATH"] = old_path
        self.assertFalse(avail)
        self.assertEqual(util, "xfs_admin")

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_get_size("non-existing-fs")

    def test_can_get_free_space(self):
        """Verify that tooling query works for getting free space"""

        avail, util = BlockDev.fs_can_get_free_space("ext4")
        self.assertTrue(avail)
        self.assertEqual(util, None)

        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ""
        avail, util = BlockDev.fs_can_get_free_space("ext4")
        os.environ["PATH"] = old_path
        self.assertFalse(avail)
        self.assertEqual(util, "dumpe2fs")

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_get_free_space("xfs")

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_get_free_space("non-existing-fs")

class MountTest(FSTestCase):

    username = "bd_mount_test"

    def _add_user(self):
        ret, _out, err = utils.run_command("useradd -M -p \"\" %s" % self.username)
        if ret != 0:
            self.fail("Failed to create user '%s': %s" % (self.username, err))

        ret, uid, err = utils.run_command("id -u %s" % self.username)
        if ret != 0:
            self.fail("Failed to get UID for user '%s': %s" % (self.username, err))

        ret, gid, err = utils.run_command("id -g %s" % self.username)
        if ret != 0:
            self.fail("Failed to get GID for user '%s': %s" % (self.username, err))

        return (uid, gid)

    def _remove_user(self):
        ret, _out, err = utils.run_command("userdel %s" % self.username)
        if ret != 0:
            self.fail("Failed to remove user user '%s': %s" % (self.username, err))

    @tag_test(TestTags.CORE)
    def test_mount(self):
        """ Test basic mounting and unmounting """

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_test")
        self.addCleanup(os.rmdir, tmp)

        self.addCleanup(umount, self.loop_dev)

        succ = BlockDev.fs_mount(self.loop_dev, tmp, "vfat", None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_is_mountpoint(tmp)
        self.assertTrue(tmp)

        mnt = BlockDev.fs_get_mountpoint(self.loop_dev)
        self.assertEqual(mnt, tmp)

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        succ = BlockDev.fs_is_mountpoint(tmp)
        self.assertFalse(succ)

        mnt = BlockDev.fs_get_mountpoint(self.loop_dev)
        self.assertIsNone(mnt)

        # mount again to test unmount using the mountpoint
        succ = BlockDev.fs_mount(self.loop_dev, tmp, None, None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(tmp, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        # mount with some options
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "vfat", "ro,noexec")
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))
        _ret, out, _err = utils.run_command("grep %s /proc/mounts" % tmp)
        self.assertTrue(out)
        self.assertIn("ro,noexec", out)

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        # mount with UID=0 and GUID=0
        succ = BlockDev.fs_mount(self.loop_dev, tmp, run_as_uid="0", run_as_gid="0")
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        with self.assertRaises(GLib.GError):
            BlockDev.fs_mount(self.loop_dev, tmp, run_as_uid="a", run_as_gid="a")

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

    def test_mount_ro_device(self):
        """ Test mounting an FS on a RO device """

        backing_file = create_sparse_tempfile("ro_mount", 50 * 1024**2)
        self.addCleanup(os.unlink, backing_file)
        self.assertTrue(BlockDev.fs_xfs_mkfs(backing_file, None))

        succ, dev = BlockDev.loop_setup(backing_file, 0, 0, True, False)
        self.assertTrue(succ)
        self.addCleanup(BlockDev.loop_teardown, dev)

        tmp_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_test")
        self.addCleanup(os.rmdir, tmp_dir)

        loop_dev = "/dev/" + dev
        # without any options, the mount should fall back to RO
        self.assertTrue(BlockDev.fs_mount(loop_dev, tmp_dir, None, None, None))
        self.addCleanup(umount, dev)
        self.assertTrue(os.path.ismount(tmp_dir))

        succ = BlockDev.fs_unmount(tmp_dir, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp_dir))

        # explicit "ro" should work just fine too
        self.assertTrue(BlockDev.fs_mount(loop_dev, tmp_dir, None, "ro", None))
        self.assertTrue(os.path.ismount(tmp_dir))

        succ = BlockDev.fs_unmount(tmp_dir, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp_dir))

        # explicit "rw" should fail
        with self.assertRaises(GLib.GError):
            BlockDev.fs_mount(loop_dev, tmp_dir, None, "rw", None)
        self.assertFalse(os.path.ismount(tmp_dir))

    @tag_test(TestTags.UNSAFE)
    def test_mount_fstab(self):
        """ Test mounting and unmounting devices in /etc/fstab """
        # this test will change /etc/fstab, we want to revert the changes when it finishes
        fstab = utils.read_file("/etc/fstab")
        self.addCleanup(utils.write_file, "/etc/fstab", fstab)

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_fstab_test")
        self.addCleanup(os.rmdir, tmp)

        utils.write_file("/etc/fstab", "%s %s vfat defaults 0 0\n" % (self.loop_dev, tmp))

        # try to mount and unmount using the device
        self.addCleanup(umount, self.loop_dev)
        succ = BlockDev.fs_mount(device=self.loop_dev)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(self.loop_dev)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        # try to mount and unmount just using the mountpoint
        self.addCleanup(umount, self.loop_dev)
        succ = BlockDev.fs_mount(mountpoint=tmp)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(tmp)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

    @tag_test(TestTags.UNSAFE)
    def test_mount_fstab_user(self):
        """ Test mounting and unmounting devices in /etc/fstab as non-root user """
        # this test will change /etc/fstab, we want to revert the changes when it finishes
        fstab = utils.read_file("/etc/fstab")
        self.addCleanup(utils.write_file, "/etc/fstab", fstab)

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_fstab_user_test")
        self.addCleanup(os.rmdir, tmp)

        utils.write_file("/etc/fstab", "%s %s vfat defaults,users 0 0\n" % (self.loop_dev, tmp))

        uid, gid = self._add_user()
        self.addCleanup(self._remove_user)

        # try to mount and unmount the device as the user
        self.addCleanup(umount, self.loop_dev)
        succ = BlockDev.fs_mount(device=self.loop_dev, run_as_uid=uid, run_as_gid=gid)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(self.loop_dev, run_as_uid=uid, run_as_gid=gid)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        # remove the 'users' option
        utils.write_file("/etc/fstab", "%s %s vfat defaults 0 0\n" % (self.loop_dev, tmp))

        # try to mount and unmount the device as the user --> should fail now
        with self.assertRaises(GLib.GError):
            BlockDev.fs_mount(device=self.loop_dev, run_as_uid=uid, run_as_gid=gid)

        self.assertFalse(os.path.ismount(tmp))

        # now mount as root to test unmounting
        self.addCleanup(umount, self.loop_dev)
        succ = BlockDev.fs_mount(device=self.loop_dev)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        with self.assertRaises(GLib.GError):
            BlockDev.fs_unmount(self.loop_dev, run_as_uid=uid, run_as_gid=gid)
        self.assertTrue(os.path.ismount(tmp))

    def test_mount_ntfs(self):
        """ Test basic mounting and unmounting with NTFS filesystem"""
        # using NTFS because it uses a helper program (mount.ntfs) and libmount
        # behaves differently because of that

        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_test")
        self.addCleanup(os.rmdir, tmp)

        self.addCleanup(umount, self.loop_dev)

        succ = BlockDev.fs_mount(self.loop_dev, tmp, "ntfs", None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        mnt = BlockDev.fs_get_mountpoint(self.loop_dev)
        self.assertIsNone(mnt)

        # mount again to test unmount using the mountpoint
        succ = BlockDev.fs_mount(self.loop_dev, tmp, None, None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(tmp, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        # mount with some options
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "ntfs", "ro")
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))
        _ret, out, _err = utils.run_command("grep %s /proc/mounts" % tmp)
        self.assertTrue(out)
        self.assertIn("ro", out)

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

    def test_mount_ntfs_ro(self):
        """ Test mounting and unmounting read-only device with NTFS filesystem"""

        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_test")
        self.addCleanup(os.rmdir, tmp)

        # set the device read-only
        self.setro(self.loop_dev)
        self.addCleanup(self.setrw, self.loop_dev)

        # forced rw mount should fail
        with self.assertRaises(GLib.GError):
            BlockDev.fs_mount(self.loop_dev, tmp, "ntfs", "rw")

        # read-only mount should work
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "ntfs", "ro")
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

class GenericCheck(FSTestCase):
    log = []

    def _test_generic_check(self, mkfs_function):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        self.log = []
        # check for consistency (expected to be ok)
        succ = BlockDev.fs_check(self.loop_dev)
        self.assertTrue(succ)

    def _my_progress_func(self, task, status, completion, msg):
        self.assertTrue(isinstance(completion, int))
        self.log.append(completion)

    def _verify_progress(self, log):
        # at least 2 members
        self.assertLessEqual(2, len(log))
        # non-decreasing members
        self.assertTrue(all(x<=y for x, y in zip(log, log[1:])))

    def test_ext4_generic_check(self):
        """Test generic check function with an ext4 file system"""
        self._test_generic_check(mkfs_function=BlockDev.fs_ext4_mkfs)

    def test_ext4_progress_check(self):
        """Test check function with an ext4 file system and progress reporting"""

        succ = BlockDev.utils_init_prog_reporting(self._my_progress_func)
        self.assertTrue(succ)

        self._test_generic_check(mkfs_function=BlockDev.fs_ext4_mkfs)
        self._verify_progress(self.log)

        succ = BlockDev.utils_init_prog_reporting(None)

    def test_xfs_generic_check(self):
        """Test generic check function with an ext4 file system"""
        self._test_generic_check(mkfs_function=BlockDev.fs_xfs_mkfs)

    def test_ntfs_generic_check(self):
        """Test generic check function with an ntfs file system"""
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        self._test_generic_check(mkfs_function=BlockDev.fs_ntfs_mkfs)

    def _check_fsck_f2fs_version(self):
        # if it can run -V to get version it can do the check
        ret, _out, _err = utils.run_command("fsck.f2fs -V")
        return ret == 0

    def test_f2fs_generic_check(self):
        """Test generic check function with an f2fs file system"""
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")
        if not self._check_fsck_f2fs_version():
            with six.assertRaisesRegex(self, GLib.GError, "Too low version of fsck.f2fs. At least 1.11.0 required."):
                self._test_generic_check(mkfs_function=BlockDev.fs_f2fs_mkfs)
        else:
            self._test_generic_check(mkfs_function=BlockDev.fs_f2fs_mkfs)

    def test_reiserfs_generic_check(self):
        """Test generic check function with an reiserfs file system"""
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        self._test_generic_check(mkfs_function=BlockDev.fs_reiserfs_mkfs)

    def test_nilfs2_generic_check(self):
        """Test generic check function with an nilfs2 file system"""
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        with self.assertRaises(GLib.GError):
            # nilfs2 doesn't support check
            self._test_generic_check(mkfs_function=BlockDev.fs_nilfs2_mkfs)

    def test_exfat_generic_check(self):
        """Test generic check function with an exfat file system"""
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")
        self._test_generic_check(mkfs_function=BlockDev.fs_exfat_mkfs)

class GenericRepair(FSTestCase):
    def _test_generic_repair(self, mkfs_function):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        # repair (expected to succeed)
        succ = BlockDev.fs_repair(self.loop_dev)
        self.assertTrue(succ)

    def test_ext4_generic_repair(self):
        """Test generic repair function with an ext4 file system"""
        self._test_generic_repair(mkfs_function=BlockDev.fs_ext4_mkfs)

    def test_xfs_generic_repair(self):
        """Test generic repair function with an xfs file system"""
        self._test_generic_repair(mkfs_function=BlockDev.fs_xfs_mkfs)

    def test_ntfs_generic_repair(self):
        """Test generic repair function with an ntfs file system"""
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        self._test_generic_repair(mkfs_function=BlockDev.fs_ntfs_mkfs)

    def test_f2fs_generic_repair(self):
        """Test generic repair function with an f2fs file system"""
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")
        self._test_generic_repair(mkfs_function=BlockDev.fs_f2fs_mkfs)

    def test_reiserfs_generic_repair(self):
        """Test generic repair function with an reiserfs file system"""
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        self._test_generic_repair(mkfs_function=BlockDev.fs_reiserfs_mkfs)

    def test_nilfs2_generic_repair(self):
        """Test generic repair function with an nilfs2 file system"""
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        with self.assertRaises(GLib.GError):
            # nilfs2 doesn't support repair
            self._test_generic_repair(mkfs_function=BlockDev.fs_nilfs2_mkfs)

    def test_exfat_generic_repair(self):
        """Test generic repair function with an exfat file system"""
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")
        self._test_generic_repair(mkfs_function=BlockDev.fs_exfat_mkfs)

class GenericSetLabel(FSTestCase):
    def _test_generic_set_label(self, mkfs_function):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        # set label (expected to succeed)
        succ = BlockDev.fs_set_label(self.loop_dev, "new_label")
        self.assertTrue(succ)

    def test_ext4_generic_set_label(self):
        """Test generic set_label function with an ext4 file system"""
        self._test_generic_set_label(mkfs_function=BlockDev.fs_ext4_mkfs)

    def test_xfs_generic_set_label(self):
        """Test generic set_label function with a xfs file system"""
        self._test_generic_set_label(mkfs_function=BlockDev.fs_xfs_mkfs)

    def test_ntfs_generic_set_label(self):
        """Test generic set_label function with a ntfs file system"""
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        self._test_generic_set_label(mkfs_function=BlockDev.fs_ntfs_mkfs)

    def test_f2fs_generic_set_label(self):
        """Test generic set_label function with a f2fs file system"""
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")
        with self.assertRaises(GLib.GError):
            # f2fs doesn't support relabeling
            self._test_generic_set_label(mkfs_function=BlockDev.fs_f2fs_mkfs)

    def test_reiserfs_generic_set_label(self):
        """Test generic set_label function with a reiserfs file system"""
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        self._test_generic_set_label(mkfs_function=BlockDev.fs_reiserfs_mkfs)

    def test_nilfs2_generic_set_label(self):
        """Test generic set_label function with a nilfs2 file system"""
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        self._test_generic_set_label(mkfs_function=BlockDev.fs_nilfs2_mkfs)

    def test_exfat_generic_set_label(self):
        """Test generic set_label function with a exfat file system"""
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")
        self._test_generic_set_label(mkfs_function=BlockDev.fs_exfat_mkfs)

class GenericSetUUID(FSTestCase):
    def _test_generic_set_uuid(self, mkfs_function, test_uuid="4d7086c4-a4d3-432f-819e-73da03870df9"):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)

        # set uuid (expected to succeed)
        succ = BlockDev.fs_set_uuid(self.loop_dev, test_uuid)
        self.assertTrue(succ)

        fs_uuid = check_output(["blkid", "-ovalue", "-sUUID", "-p", self.loop_dev]).decode().strip()
        self.assertEqual(fs_uuid, test_uuid)

    def test_ext4_generic_set_uuid(self):
        """Test generic set_uuid function with an ext4 file system"""
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_ext4_mkfs)

    def test_xfs_generic_set_uuid(self):
        """Test generic set_uuid function with a xfs file system"""
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_xfs_mkfs)

    def test_ntfs_generic_set_uuid(self):
        """Test generic set_uuid function with a ntfs file system"""
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_ntfs_mkfs, test_uuid="1C2716ED53F63962")

    def test_vfat_generic_set_uuid(self):
        """Test generic set_uuid function with a vfat file system"""
        with self.assertRaises(GLib.GError):
            # vfat doesn't support setting UUID
            self._test_generic_set_uuid(mkfs_function=BlockDev.fs_vfat_mkfs)

    def test_f2fs_generic_set_uuid(self):
        """Test generic set_uuid function with a f2fs file system"""
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")
        with self.assertRaises(GLib.GError):
            # f2fs doesn't support setting UUID
            self._test_generic_set_uuid(mkfs_function=BlockDev.fs_f2fs_mkfs)

    def test_reiserfs_generic_set_uuid(self):
        """Test generic set_uuid function with a reiserfs file system"""
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_reiserfs_mkfs)

    def test_nilfs2_generic_set_uuid(self):
        """Test generic set_uuid function with a nilfs2 file system"""
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_nilfs2_mkfs)

    def test_exfat_generic_set_uuid(self):
        """Test generic set_uuid function with a exfat file system"""
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")
        with six.assertRaisesRegex(self, GLib.GError, "Setting UUID of filesystem 'exfat' is not supported."):
            # exfat doesn't support setting UUID
            self._test_generic_set_uuid(mkfs_function=BlockDev.fs_exfat_mkfs)

class GenericResize(FSTestCase):
    def _test_generic_resize(self, mkfs_function, size_delta=0):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)
        size = BlockDev.fs_get_size(self.loop_dev)

        # shrink
        succ = BlockDev.fs_resize(self.loop_dev, 130 * 1024**2)
        self.assertTrue(succ)
        new_size = BlockDev.fs_get_size(self.loop_dev)
        self.assertAlmostEqual(new_size, 130 * 1024**2, delta=size_delta)

        # resize to maximum size
        succ = BlockDev.fs_resize(self.loop_dev, 0)
        self.assertTrue(succ)
        new_size = BlockDev.fs_get_size(self.loop_dev)
        # should be back to original size
        self.assertAlmostEqual(new_size, size, delta=size_delta)

    def test_ext2_generic_resize(self):
        """Test generic resize function with an ext2 file system"""
        self._test_generic_resize(mkfs_function=BlockDev.fs_ext2_mkfs)

    def test_ext3_check_generic_resize(self):
        """Test generic resize function with an ext3 file system"""
        self._test_generic_resize(mkfs_function=BlockDev.fs_ext3_mkfs)

    def test_ext4_generic_resize(self):
        """Test generic resize function with an ext4 file system"""
        self._test_generic_resize(mkfs_function=BlockDev.fs_ext4_mkfs)

    def test_ntfs_generic_resize(self):
        """Test generic resize function with an ntfs file system"""
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        def mkfs_prepare(drive):
            return BlockDev.fs_ntfs_mkfs(drive, None) and BlockDev.fs_repair(drive)
        def ntfs_size(drive):
            # man ntfsresize says "The filesystem size is set to be at least one
            # sector smaller" (maybe depending on alignment as well?), thus on a
            # loop device as in this test it is 4096 bytes smaller than requested
            BlockDev.fs_repair(drive)
            fi = BlockDev.fs_ntfs_get_info(drive)
            return fi.size + 4096

        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_prepare(self.loop_dev)
        self.assertTrue(succ)

        size = ntfs_size(self.loop_dev)

        # shrink
        succ = BlockDev.fs_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)
        new_size = ntfs_size(self.loop_dev)
        self.assertEqual(new_size, 80 * 1024**2)

        # resize to maximum size
        succ = BlockDev.fs_resize(self.loop_dev, 0)
        self.assertTrue(succ)
        new_size = ntfs_size(self.loop_dev)
        # should be back to original size
        self.assertEqual(new_size, size)

    @tag_test(TestTags.UNSTABLE)
    def test_vfat_generic_resize(self):
        """Test generic resize function with a vfat file system"""
        self._test_generic_resize(mkfs_function=BlockDev.fs_vfat_mkfs, size_delta=1024**2)

    def _destroy_lvm(self):
        run("vgremove --yes libbd_fs_tests >/dev/null 2>&1")
        run("pvremove --yes %s >/dev/null 2>&1" % self.loop_dev)

    def test_xfs_generic_resize(self):
        """Test generic resize function with an xfs file system"""

        run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        run("vgcreate -s10M libbd_fs_tests %s >/dev/null 2>&1" % self.loop_dev)
        run("lvcreate -n xfs_test -L50M libbd_fs_tests >/dev/null 2>&1")
        self.addCleanup(self._destroy_lvm)

        lv = "/dev/libbd_fs_tests/xfs_test"

        # clean the device
        succ = BlockDev.fs_clean(lv)

        succ = BlockDev.fs_xfs_mkfs(lv, None)
        self.assertTrue(succ)

        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 50 * 1024**2)

        # no change, nothing should happen
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_resize(lv, 0)
        self.assertTrue(succ)

        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 50 * 1024**2)

        # (still) impossible to shrink an XFS file system
        with mounted(lv, self.mount_dir):
            with self.assertRaises(GLib.GError):
                succ = BlockDev.fs_resize(lv, 40 * 1024**2)

        run("lvresize -L70M libbd_fs_tests/xfs_test >/dev/null 2>&1")
        # should grow
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_resize(lv, 0)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 70 * 1024**2)

        run("lvresize -L90M libbd_fs_tests/xfs_test >/dev/null 2>&1")
        # should grow just to 80 MiB
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_resize(lv, 80 * 1024**2)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 80 * 1024**2)

        # should grow to 90 MiB
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_resize(lv, 0)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 90 * 1024**2)

    def _can_resize_f2fs(self):
        ret, out, _err = utils.run_command("resize.f2fs -V")
        if ret != 0:
            # we can't even check the version
            return False

        m = re.search(r"resize.f2fs ([\d\.]+)", out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError("Failed to determine f2fs version from: %s" % out)
        return LooseVersion(m.groups()[0]) >= LooseVersion("1.12.0")

    def test_f2fs_generic_resize(self):
        """Verify that it is possible to resize an f2fs file system"""
        if not self.f2fs_avail:
            self.skipTest("skipping F2FS: not available")
        if not self._can_resize_f2fs():
            with six.assertRaisesRegex(self, GLib.GError, "Too low version of resize.f2fs. At least 1.12.0 required."):
                self._test_generic_resize(mkfs_function=BlockDev.fs_f2fs_mkfs)
        else:
            self._test_generic_resize(mkfs_function=BlockDev.fs_f2fs_mkfs)

    def test_reiserfs_generic_resize(self):
        """Test generic resize function with an reiserfs file system"""
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        self._test_generic_resize(mkfs_function=BlockDev.fs_reiserfs_mkfs)

    def test_nilfs2_generic_resize(self):
        """Test generic resize function with an nilfs2 file system"""
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        self._test_generic_resize(mkfs_function=BlockDev.fs_nilfs2_mkfs)

    def test_exfat_generic_resize(self):
        """Test generic resize function with an exfat file system"""
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")

        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = BlockDev.fs_exfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # no resize support for exFAT
        with six.assertRaisesRegex(self, GLib.GError, "Resizing filesystem 'exfat' is not supported."):
            BlockDev.fs_resize(self.loop_dev, 80 * 1024**2)

class GenericGetFreeSpace(FSTestCase):
    def _test_get_free_space(self, mkfs_function, size_delta=0):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)
        size = BlockDev.fs_get_size(self.loop_dev)
        free = BlockDev.fs_get_free_space(self.loop_dev)
        self.assertNotEqual(free, 0)
        self.assertLessEqual(free, size)

    def test_ext2_get_free_space(self):
        """Test generic resize function with an ext2 file system"""
        self._test_get_free_space(mkfs_function=BlockDev.fs_ext2_mkfs)

    def test_ext3_check_get_free_space(self):
        """Test generic resize function with an ext3 file system"""
        self._test_get_free_space(mkfs_function=BlockDev.fs_ext3_mkfs)

    def test_ext4_get_free_space(self):
        """Test generic resize function with an ext4 file system"""
        self._test_get_free_space(mkfs_function=BlockDev.fs_ext4_mkfs)

    def test_ntfs_get_free_space(self):
        """Test generic resize function with an ntfs file system"""
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        self._test_get_free_space(mkfs_function=BlockDev.fs_ntfs_mkfs)

    def test_vfat_get_free_space(self):
        """Test generic resize function with a vfat file system"""
        self._test_get_free_space(mkfs_function=BlockDev.fs_vfat_mkfs)

    def test_reiserfs_get_free_space(self):
        """Test generic resize function with an reiserfs file system"""
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        self._test_get_free_space(mkfs_function=BlockDev.fs_reiserfs_mkfs)

    def test_nilfs2_get_free_space(self):
        """Test generic resize function with an nilfs2 file system"""
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        self._test_get_free_space(mkfs_function=BlockDev.fs_nilfs2_mkfs)


class FSFreezeTest(FSTestCase):

    def _clean_up(self):
        try:
            BlockDev.fs_unfreeze(self.loop_dev)
        except:
            pass

        BlockDev.fs_wipe(self.loop_dev, True)

        super(FSFreezeTest, self)._clean_up()

    def test_freeze_xfs(self):
        """ Test basic freezing and un-freezing with XFS """

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # try to freeze with non-existing mountpoint
        with self.assertRaises(GLib.GError):
            BlockDev.fs_freeze("/not/a/mountpoint")

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="freeze_test")
        self.addCleanup(os.rmdir, tmp)

        self.addCleanup(umount, self.loop_dev)
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "xfs", None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_freeze(tmp)
        self.assertTrue(succ)

        # try to freeze again (should fail)
        with self.assertRaises(GLib.GError):
            BlockDev.fs_freeze(tmp)

        # and unfreeze
        succ = BlockDev.fs_unfreeze(tmp)
        self.assertTrue(succ)

    def test_freeze_vfat(self):
        """ Test basic freezing and un-freezing with FAT """

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="freeze_test")
        self.addCleanup(os.rmdir, tmp)

        self.addCleanup(umount, self.loop_dev)
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "vfat", None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        # FAT doesn't support freezing
        with self.assertRaises(GLib.GError):
            BlockDev.fs_freeze(tmp)
