import unittest
import os
import time
import subprocess
import tempfile
from contextlib import contextmanager
import utils
import six
import overrides_hack

from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

def mount(device, where):
    if not os.path.isdir(where):
        os.makedirs(where)
    os.system("mount %s %s" % (device, where))

def umount(what):
    try:
        os.system("umount %s &>/dev/null" % what)
    except OSError:
        # no such file or directory
        pass

@contextmanager
def mounted(device, where):
    mount(device, where)
    yield
    umount(where)

class FSTestCase(unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = utils.create_sparse_tempfile("part_test", 100 * 1024**2)
        self.dev_file2 = utils.create_sparse_tempfile("part_test", 100 * 1024**2)
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
        os.rmdir(self.mount_dir)

class TestGenericWipe(FSTestCase):
    def test_generic_wipe(self):
        """Verify that generic signature wipe works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_wipe("/non/existing/device", True)

        ret = os.system("pvcreate %s &>/dev/null" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        # now test the same multiple times in a row
        for i in range(10):
            ret = os.system("pvcreate %s &>/dev/null" % self.loop_dev)
            self.assertEqual(ret, 0)

            succ = BlockDev.fs_wipe(self.loop_dev, True)
            self.assertTrue(succ)

        # vfat has multiple signatures on the device so it allows us to test the
        # 'all' argument of fs_wipe()
        ret = os.system("mkfs.vfat -I %s &>/dev/null" % self.loop_dev)
        self.assertEqual(ret, 0)

        time.sleep(0.5)
        succ = BlockDev.fs_wipe(self.loop_dev, False)
        self.assertTrue(succ)

        # the second signature should still be there
        # XXX: lsblk uses the udev db so it we need to make sure it is up to date
        os.system("udevadm settle")
        fs_type = subprocess.check_output(["lsblk", "-n", "-oFSTYPE", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"vfat")

        # get rid of all the remaining signatures (there could be vfat + PMBR for some reason)
        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        os.system("udevadm settle")
        fs_type = subprocess.check_output(["lsblk", "-n", "-oFSTYPE", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # now do the wipe all in a one step
        ret = os.system("mkfs.vfat -I %s &>/dev/null" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        os.system("udevadm settle")
        fs_type = subprocess.check_output(["lsblk", "-n", "-oFSTYPE", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # try to wipe empty device
        with six.assertRaisesRegex(self, GLib.GError, "No signature detected on the device"):
            BlockDev.fs_wipe(self.loop_dev, True)


class Ext4TestMkfs(FSTestCase):
    def test_ext4_mkfs(self):
        """Verify that it is possible to create a new ext4 file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        BlockDev.fs_wipe(self.loop_dev, True)

class Ext4MkfsWithLabel(FSTestCase):
    def test_ext4_mkfs_with_label(self):
        """Verify that it is possible to create an ext4 file system with label"""

        ea = BlockDev.ExtraArg.new("-L", "TEST_LABEL")
        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, [ea])
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

class Ext4TestWipe(FSTestCase):
    def test_ext4_wipe(self):
        """Verify that it is possible to wipe an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_ext4_wipe(self.loop_dev)
        self.assertTrue(succ)

        # already wiped, should fail this time
        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_wipe(self.loop_dev)

        os.system("pvcreate %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an ext4 file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        os.system("mkfs.ext2 -F %s &>/dev/null" % self.loop_dev)

        # ext2, not an ext4 file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_ext4_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

class Ext4TestCheck(FSTestCase):
    def test_ext4_check(self):
        """Verify that it is possible to check an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_ext4_check(self.loop_dev, None)
        self.assertTrue(succ)

        # mounted, but can be checked
        with mounted(self.loop_dev, self.mount_dir):
            succ = BlockDev.fs_ext4_check(self.loop_dev, None)
            self.assertTrue(succ)

        succ = BlockDev.fs_ext4_check(self.loop_dev, None)
        self.assertTrue(succ)

class Ext4TestRepair(FSTestCase):
    def test_ext4_repair(self):
        """Verify that it is possible to repair an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_ext4_repair(self.loop_dev, False, None)
        self.assertTrue(succ)

        # unsafe operations should work here too
        succ = BlockDev.fs_ext4_repair(self.loop_dev, True, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            with self.assertRaises(GLib.GError):
                BlockDev.fs_ext4_repair(self.loop_dev, False, None)

        succ = BlockDev.fs_ext4_repair(self.loop_dev, False, None)
        self.assertTrue(succ)

class Ext4GetInfo(FSTestCase):
    def test_ext4_get_info(self):
        """Verify that it is possible to get info about an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)
        self.assertEqual(fi.label, "")
        # should be an non-empty string
        self.assertTrue(fi.uuid)
        self.assertTrue(fi.state, "clean")

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_ext4_get_info(self.loop_dev)
            self.assertEqual(fi.block_size, 1024)
            self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
            # at least 90 % should be available, so it should be reported
            self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)
            self.assertEqual(fi.label, "")
            # should be an non-empty string
            self.assertTrue(fi.uuid)
            self.assertTrue(fi.state, "clean")

class Ext4SetLabel(FSTestCase):
    def test_ext4_set_label(self):
        """Verify that it is possible to set label of an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_ext4_set_label(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = BlockDev.fs_ext4_set_label(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = BlockDev.fs_ext4_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

class Ext4Resize(FSTestCase):
    def test_ext4_resize(self):
        """Verify that it is possible to resize an ext4 file system"""

        succ = BlockDev.fs_ext4_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)

        succ = BlockDev.fs_ext4_resize(self.loop_dev, 50 * 1024**2, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 50 * 1024**2 / 1024)

        # resize back
        succ = BlockDev.fs_ext4_resize(self.loop_dev, 100 * 1024**2, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)

        # resize again
        succ = BlockDev.fs_ext4_resize(self.loop_dev, 50 * 1024**2, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 50 * 1024**2 / 1024)

        # resize back again, this time to maximum size
        succ = BlockDev.fs_ext4_resize(self.loop_dev, 0, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_ext4_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size, 1024)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 1024)
        # at least 90 % should be available, so it should be reported
        self.assertGreater(fi.free_blocks, 0.90 * 100 * 1024**2 / 1024)

class XfsTestMkfs(FSTestCase):
    def test_xfs_mkfs(self):
        """Verify that it is possible to create a new xfs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

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

        os.system("pvcreate %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an xfs file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_xfs_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        os.system("mkfs.ext2 -F %s &>/dev/null" % self.loop_dev)

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

        # mounted, but can be checked and nothing happened in/to the file
        # system, so it should be just reported as clean
        with mounted(self.loop_dev, self.mount_dir):
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
    def test_xfs_get_info(self):
        """Verify that it is possible to get info about an xfs file system"""

        succ = BlockDev.fs_xfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        with mounted(self.loop_dev, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(self.loop_dev)

        self.assertEqual(fi.block_size, 4096)
        self.assertEqual(fi.block_count, 100 * 1024**2 / 4096)
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

class XfsResize(FSTestCase):
    def _destroy_lvm(self):
        os.system("vgremove --yes libbd_fs_tests &>/dev/null")
        os.system("pvremove --yes %s &>/dev/null" % self.loop_dev)

    def test_xfs_resize(self):
        """Verify that it is possible to resize an xfs file system"""

        os.system("pvcreate -ff -y %s &>/dev/null" % self.loop_dev)
        os.system("vgcreate -s10M libbd_fs_tests %s &>/dev/null" % self.loop_dev)
        os.system("lvcreate -n xfs_test -L50M libbd_fs_tests &>/dev/null")
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

        os.system("lvresize -L70M libbd_fs_tests/xfs_test &>/dev/null")
        # should grow
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_xfs_resize(self.mount_dir, 0, None)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 70 * 1024**2)

        os.system("lvresize -L90M libbd_fs_tests/xfs_test &>/dev/null")
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

        os.system("pvcreate %s >/dev/null" % self.loop_dev)

        # LVM PV signature, not an vfat file system
        with self.assertRaises(GLib.GError):
            BlockDev.fs_vfat_wipe(self.loop_dev)

        BlockDev.fs_wipe(self.loop_dev, True)

        os.system("mkfs.ext2 -F %s &>/dev/null" % self.loop_dev)

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

class VfatResize(FSTestCase):
    def test_vfat_resize(self):
        """Verify that it is possible to resize an vfat file system"""

        succ = BlockDev.fs_vfat_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # shrink
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)

        # grow
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 100 * 1024**2)
        self.assertTrue(succ)

        # shrink again
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 80 * 1024**2)
        self.assertTrue(succ)

        # resize to maximum size
        succ = BlockDev.fs_vfat_resize(self.loop_dev, 0)
        self.assertTrue(succ)


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

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

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

    @unittest.skipUnless("JENKINS_HOME" in os.environ, "skipping test that modifies system configuration")
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

    @unittest.skipUnless("JENKINS_HOME" in os.environ, "skipping test that modifies system configuration")
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
