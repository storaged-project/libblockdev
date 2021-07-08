import os
import time
import tempfile
import re

from distutils.version import LooseVersion


from .fs_test import FSTestCase, mounted, check_output

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class GenericTestCase(FSTestCase):

    loop_size = 500 * 1024**2

    def setUp(self):
        super(GenericTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="generic_test")

        self._vfat_version = self._get_dosfstools_version()

    def _get_dosfstools_version(self):
        _ret, out, _err = utils.run_command("mkfs.vfat --help")
        # mkfs.fat 4.1 (2017-01-24)
        m = re.search(r"mkfs\.fat ([\d\.]+)", out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError("Failed to determine dosfstools version from: %s" % out)
        return LooseVersion(m.groups()[0])


class TestGenericWipe(GenericTestCase):
    @tag_test(TestTags.CORE)
    def test_generic_wipe(self):
        """Verify that generic signature wipe works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_wipe("/non/existing/device", True)

        ret = utils.run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        # now test the same multiple times in a row
        for i in range(10):
            ret = utils.run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
            self.assertEqual(ret, 0)

            succ = BlockDev.fs_wipe(self.loop_dev, True)
            self.assertTrue(succ)

        # vfat has multiple signatures on the device so it allows us to test the
        # 'all' argument of fs_wipe()
        if self._vfat_version >= LooseVersion("4.2"):
            ret = utils.run("mkfs.vfat -I %s >/dev/null 2>&1 --mbr=n" % self.loop_dev)
        else:
            ret = utils.run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        time.sleep(0.5)
        succ = BlockDev.fs_wipe(self.loop_dev, False)
        self.assertTrue(succ)

        # the second signature should still be there
        # XXX: lsblk uses the udev db so it we need to make sure it is up to date
        utils.run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"vfat")

        # get rid of all the remaining signatures (there could be vfat + PMBR for some reason)
        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        utils.run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # now do the wipe all in a one step
        if self._vfat_version >= LooseVersion("4.2"):
            ret = utils.run("mkfs.vfat -I %s >/dev/null 2>&1 --mbr=n" % self.loop_dev)
        else:
            ret = utils.run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_wipe(self.loop_dev, True)
        self.assertTrue(succ)

        utils.run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # try to wipe empty device
        with self.assertRaisesRegex(GLib.GError, "No signature detected on the device"):
            BlockDev.fs_wipe(self.loop_dev, True)


class TestClean(GenericTestCase):
    def test_clean(self):
        """Verify that device clean works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_clean("/non/existing/device")

        # empty device shouldn't fail
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        ret = utils.run("pvcreate -ff -y %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        # XXX: lsblk uses the udev db so it we need to make sure it is up to date
        utils.run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")

        # vfat has multiple signatures on the device so it allows us to test
        # that clean removes all signatures
        if self._vfat_version >= LooseVersion("4.2"):
            ret = utils.run("mkfs.vfat -I %s >/dev/null 2>&1 --mbr=n" % self.loop_dev)
        else:
            ret = utils.run("mkfs.vfat -I %s >/dev/null 2>&1" % self.loop_dev)
        self.assertEqual(ret, 0)

        time.sleep(0.5)
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        utils.run("udevadm settle")
        fs_type = check_output(["blkid", "-ovalue", "-sTYPE", "-p", self.loop_dev]).strip()
        self.assertEqual(fs_type, b"")


class CanResizeRepairCheckLabel(GenericTestCase):
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

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_resize("udf")

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

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_repair("udf")

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

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_check("udf")

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

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_get_free_space("exfat")

        with self.assertRaises(GLib.GError):
            BlockDev.fs_can_get_free_space("udf")


class GenericMkfs(GenericTestCase):

    def _test_ext_generic_mkfs(self, fsname, info_fn, label=None, uuid=None, extra=None):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        supported, flags, _util = BlockDev.fs_can_mkfs(fsname)
        if not supported:
            self.skipTest("skipping %s: not available" % fsname)

        if flags & BlockDev.FSMkfsOptionsFlags.DRY_RUN:
            # try dry run first
            options = BlockDev.FSMkfsOptions(None, None, True, False)
            succ = BlockDev.fs_mkfs(self.loop_dev, fsname, options)
            self.assertTrue(succ)

            fstype = BlockDev.fs_get_fstype (self.loop_dev)
            self.assertIsNone(fstype)

        options = BlockDev.FSMkfsOptions(label, uuid, False, False)

        succ = BlockDev.fs_mkfs(self.loop_dev, fsname, options, extra)
        self.assertTrue(succ)

        fstype = BlockDev.fs_get_fstype (self.loop_dev)
        self.assertEqual(fstype, fsname)

        info = info_fn(self.loop_dev)
        self.assertIsNotNone(info)
        if label:
            self.assertEqual(info.label, label)
        if uuid:
            self.assertEqual(info.uuid, uuid)

    def test_exfat_generic_mkfs(self):
        """ Test generic mkfs with exFAT """
        if not self.exfat_avail:
            self.skipTest("skipping exFAT: not available")
        label = "label"
        self._test_ext_generic_mkfs("exfat", BlockDev.fs_exfat_get_info, label, None)

    def test_ext2_generic_mkfs(self):
        """ Test generic mkfs with ext2 """
        label = "label"
        uuid = "8802574c-587b-43b9-a6be-9de77759d2c5"
        self._test_ext_generic_mkfs("ext2", BlockDev.fs_ext2_get_info, label, uuid)

        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        options = BlockDev.FSMkfsOptions(label, uuid, False, False)

        # and try with a custom extra arg (we can get block size from the info)
        extra = BlockDev.ExtraArg("-b", "4096")

        succ = BlockDev.fs_mkfs(self.loop_dev, "ext2", options, [extra])
        self.assertTrue(succ)

        fstype = BlockDev.fs_get_fstype (self.loop_dev)
        self.assertEqual(fstype, "ext2")

        info = BlockDev.fs_ext2_get_info(self.loop_dev)
        self.assertEqual(info.label, label)
        self.assertEqual(info.uuid, uuid)
        self.assertEqual(info.block_size, 4096)

    def test_ext3_generic_mkfs(self):
        """ Test generic mkfs with ext3 """
        label = "label"
        uuid = "8802574c-587b-43b9-a6be-9de77759d2c5"
        self._test_ext_generic_mkfs("ext3", BlockDev.fs_ext3_get_info, label, uuid)

    def test_ext4_generic_mkfs(self):
        """ Test generic mkfs with ext4 """
        label = "label"
        uuid = "8802574c-587b-43b9-a6be-9de77759d2c5"
        self._test_ext_generic_mkfs("ext4", BlockDev.fs_ext4_get_info, label, uuid)

    def test_f2fs_generic_mkfs(self):
        """ Test generic mkfs with F2FS """
        label = "label"
        self._test_ext_generic_mkfs("f2fs", BlockDev.fs_f2fs_get_info, label, None)

    def test_nilfs2_generic_mkfs(self):
        """ Test generic mkfs with nilfs2 """
        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS2: not available")
        label = "label"
        self._test_ext_generic_mkfs("nilfs2", BlockDev.fs_nilfs2_get_info, label, None)

    def test_ntfs_generic_mkfs(self):
        """ Test generic mkfs with NTFS """
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")
        label = "label"
        self._test_ext_generic_mkfs("ntfs", BlockDev.fs_ntfs_get_info, label, None)

    def test_reiserfs_generic_mkfs(self):
        """ Test generic mkfs with ReiserFS """
        if not self.reiserfs_avail:
            self.skipTest("skipping ReiserFS: not available")
        label = "label"
        uuid = "8802574c-587b-43b9-a6be-9de77759d2c5"
        self._test_ext_generic_mkfs("reiserfs", BlockDev.fs_reiserfs_get_info, label, uuid)

    def test_vfat_generic_mkfs(self):
        """ Test generic mkfs with vfat """
        label = "LABEL"
        if self._vfat_version >= LooseVersion("4.2"):
            extra = [BlockDev.ExtraArg.new("--mbr=n", "")]
        else:
            extra = None
        self._test_ext_generic_mkfs("vfat", BlockDev.fs_vfat_get_info, label, None, extra)

    def test_xfs_generic_mkfs(self):
        """ Test generic mkfs with XFS """
        label = "label"
        uuid = "8802574c-587b-43b9-a6be-9de77759d2c5"

        def _xfs_info(device):
            with mounted(device, self.mount_dir):
                info = BlockDev.fs_xfs_get_info(device)
            return info

        self._test_ext_generic_mkfs("xfs", _xfs_info, label, uuid)

    def test_btrfs_generic_mkfs(self):
        """ Test generic mkfs with Btrfs """
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")
        label = "label"
        uuid = "8802574c-587b-43b9-a6be-9de77759d2c5"

        def _btrfs_info(device):
            with mounted(device, self.mount_dir):
                info = BlockDev.fs_btrfs_get_info(self.mount_dir)
            return info

        self._test_ext_generic_mkfs("btrfs", _btrfs_info, label, uuid)

    def test_udf_generic_mkfs(self):
        """ Test generic mkfs with udf """
        if not self.udf_avail:
            self.skipTest("skipping UDF: not available")
        label = "LABEL"
        self._test_ext_generic_mkfs("udf", BlockDev.fs_udf_get_info, label, None)

    def test_can_mkfs(self):
        """ Test checking whether mkfs is supported """
        # lets pick a filesystem that supports everything and is always available
        # in the CI
        supported, flags, _util = BlockDev.fs_can_mkfs("ext2")
        self.assertTrue(supported)
        self.assertTrue(flags & BlockDev.FSMkfsOptionsFlags.DRY_RUN)
        self.assertTrue(flags & BlockDev.FSMkfsOptionsFlags.LABEL)
        self.assertTrue(flags & BlockDev.FSMkfsOptionsFlags.UUID)
        self.assertTrue(flags & BlockDev.FSMkfsOptionsFlags.NODISCARD)


class GenericCheck(GenericTestCase):
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
            with self.assertRaisesRegex(GLib.GError, "Too low version of fsck.f2fs. At least 1.11.0 required."):
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

    def test_btrfs_generic_check(self):
        """Test generic check function with an btrfs file system"""
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")
        self._test_generic_check(mkfs_function=BlockDev.fs_btrfs_mkfs)


class GenericRepair(GenericTestCase):
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

    def test_btrfs_generic_repair(self):
        """Test generic repair function with an btrfs file system"""
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")
        self._test_generic_repair(mkfs_function=BlockDev.fs_btrfs_mkfs)


class GenericSetLabel(GenericTestCase):
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

    def test_btrfs_generic_set_label(self):
        """Test generic set_label function with a btrfs file system"""
        if not self.btrfs_avail:
            self.skipTest("skipping btrfs: not available")
        self._test_generic_set_label(mkfs_function=BlockDev.fs_btrfs_mkfs)

    def test_udf_generic_set_label(self):
        """Test generic set_label function with a udf file system"""
        if not self.udf_avail:
            self.skipTest("skipping udf: not available")
        self._test_generic_set_label(mkfs_function=BlockDev.fs_udf_mkfs)


class GenericSetUUID(GenericTestCase):
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
        with self.assertRaisesRegex(GLib.GError, "Setting UUID of filesystem 'exfat' is not supported."):
            # exfat doesn't support setting UUID
            self._test_generic_set_uuid(mkfs_function=BlockDev.fs_exfat_mkfs)

    def test_btrfs_generic_set_uuid(self):
        """Test generic set_uuid function with a btrfs file system"""
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_btrfs_mkfs)

    def test_udf_generic_set_uuid(self):
        """Test generic set_uuid function with a udf file system"""
        if not self.udf_avail:
            self.skipTest("skipping UDF: not available")
        self._test_generic_set_uuid(mkfs_function=BlockDev.fs_udf_mkfs, test_uuid="5fae9ade7938dfc8")


class GenericResize(GenericTestCase):
    def _test_generic_resize(self, mkfs_function, size_delta=0, min_size=130*1024**2):
        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = mkfs_function(self.loop_dev, None)
        self.assertTrue(succ)
        size = BlockDev.fs_get_size(self.loop_dev)

        # shrink
        succ = BlockDev.fs_resize(self.loop_dev, min_size)
        self.assertTrue(succ)
        new_size = BlockDev.fs_get_size(self.loop_dev)
        self.assertAlmostEqual(new_size, min_size, delta=size_delta)

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
        def mkfs_vfat(device, options=None):
            if self._vfat_version >= LooseVersion("4.2"):
                if options:
                    return BlockDev.fs_vfat_mkfs(device, options + [BlockDev.ExtraArg.new("--mbr=n", "")])
                else:
                    return BlockDev.fs_vfat_mkfs(device, [BlockDev.ExtraArg.new("--mbr=n", "")])
            else:
                return BlockDev.fs_vfat_mkfs(device, options)

        self._test_generic_resize(mkfs_function=mkfs_vfat, size_delta=1024**2)

    def test_xfs_generic_resize(self):
        """Test generic resize function with an xfs file system"""

        lv = self._setup_lvm(vgname="libbd_fs_tests", lvname="generic_test")

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
        xfs_version = self._get_xfs_version()
        if xfs_version < LooseVersion("5.1.12"):
            with mounted(lv, self.mount_dir):
                with self.assertRaises(GLib.GError):
                    succ = BlockDev.fs_resize(lv, 40 * 1024**2)

        self._lvresize("libbd_fs_tests", "generic_test", "70M")
        # should grow
        with mounted(lv, self.mount_dir):
            succ = BlockDev.fs_resize(lv, 0)
        self.assertTrue(succ)
        with mounted(lv, self.mount_dir):
            fi = BlockDev.fs_xfs_get_info(lv)
        self.assertTrue(fi)
        self.assertEqual(fi.block_size * fi.block_count, 70 * 1024**2)

        self._lvresize("libbd_fs_tests", "generic_test", "90M")
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
            with self.assertRaisesRegex(GLib.GError, "Too low version of resize.f2fs. At least 1.12.0 required."):
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
        with self.assertRaisesRegex(GLib.GError, "Resizing filesystem 'exfat' is not supported."):
            BlockDev.fs_resize(self.loop_dev, 80 * 1024**2)

    def test_btrfs_generic_resize(self):
        """Test generic resize function with an btrfs file system"""
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")
        self._test_generic_resize(mkfs_function=BlockDev.fs_btrfs_mkfs, min_size=300*1024**2)

    def test_udf_generic_resize(self):
        """Test generic resize function with an udf file system"""
        if not self.udf_avail:
            self.skipTest("skipping UDF: not available")

        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)

        succ = BlockDev.fs_udf_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # no resize support for UDF
        with self.assertRaisesRegex(GLib.GError, "Resizing filesystem 'udf' is not supported."):
            BlockDev.fs_resize(self.loop_dev, 80 * 1024**2)


class GenericGetFreeSpace(GenericTestCase):
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
        def mkfs_vfat(device, options=None):
            if self._vfat_version >= LooseVersion("4.2"):
                if options:
                    return BlockDev.fs_vfat_mkfs(device, options + [BlockDev.ExtraArg.new("--mbr=n", "")])
                else:
                    return BlockDev.fs_vfat_mkfs(device, [BlockDev.ExtraArg.new("--mbr=n", "")])
            else:
                return BlockDev.fs_vfat_mkfs(device, options)

        self._test_get_free_space(mkfs_function=mkfs_vfat)

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

    def test_btrfs_get_free_space(self):
        """Test generic resize function with an btrfs file system"""
        if not self.btrfs_avail:
            self.skipTest("skipping Btrfs: not available")
        self._test_get_free_space(mkfs_function=BlockDev.fs_btrfs_mkfs)

    def test_udf_get_free_space(self):
        """Test generic resize function with an udf file system"""
        if not self.udf_avail:
            self.skipTest("skipping UDF: not available")

        # clean the device
        succ = BlockDev.fs_clean(self.loop_dev)
        self.assertTrue(succ)

        succ = BlockDev.fs_udf_mkfs(self.loop_dev)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "Getting free space on filesystem 'udf' is not supported."):
            BlockDev.fs_get_free_space(self.loop_dev)


class FSFreezeTest(GenericTestCase):

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

        self.addCleanup(utils.umount, self.loop_dev)
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

        self.addCleanup(utils.umount, self.loop_dev)
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "vfat", None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        # FAT doesn't support freezing
        with self.assertRaises(GLib.GError):
            BlockDev.fs_freeze(tmp)
