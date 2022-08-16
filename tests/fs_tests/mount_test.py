import os
import tempfile

from .fs_test import FSTestCase

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class MountTestCase(FSTestCase):

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

        self.addCleanup(utils.umount, self.loop_dev)

        # try mounting unknown filesystem type
        with self.assertRaisesRegex(GLib.GError, r"Filesystem type .* not configured in kernel."):
            BlockDev.fs_mount(self.loop_dev, tmp, "nonexisting", None)

        succ = BlockDev.fs_mount(self.loop_dev, tmp, "vfat", None)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_is_mountpoint(tmp)
        self.assertTrue(succ)

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

        backing_file = utils.create_sparse_tempfile("ro_mount", 50 * 1024**2)
        self.addCleanup(os.unlink, backing_file)
        self.assertTrue(BlockDev.fs_ext2_mkfs(backing_file, None))

        succ, dev = BlockDev.loop_setup(backing_file, 0, 0, True, False)
        self.assertTrue(succ)
        self.addCleanup(BlockDev.loop_teardown, dev)

        tmp_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_test")
        self.addCleanup(os.rmdir, tmp_dir)

        loop_dev = "/dev/" + dev
        # without any options, the mount should fall back to RO
        self.assertTrue(BlockDev.fs_mount(loop_dev, tmp_dir, None, None, None))
        self.addCleanup(utils.umount, dev)
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
        self.addCleanup(utils.umount, self.loop_dev)
        succ = BlockDev.fs_mount(device=self.loop_dev)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        succ = BlockDev.fs_unmount(self.loop_dev)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))

        # try to mount and unmount just using the mountpoint
        self.addCleanup(utils.umount, self.loop_dev)
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
        self.addCleanup(utils.umount, self.loop_dev)
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
        self.addCleanup(utils.umount, self.loop_dev)
        succ = BlockDev.fs_mount(device=self.loop_dev)
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))

        with self.assertRaises(GLib.GError):
            BlockDev.fs_unmount(self.loop_dev, run_as_uid=uid, run_as_gid=gid)
        self.assertTrue(os.path.ismount(tmp))

    def test_mount_nilfs(self):
        """ Test basic mounting and unmounting with NILFS2 filesystem"""
        # using NILFS2 because it uses a helper program (mount.nilfs2) and libmount
        # behaves differently because of that

        if not self.nilfs2_avail:
            self.skipTest("skipping NILFS mount test: not available")

        succ = BlockDev.fs_nilfs2_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        tmp = tempfile.mkdtemp(prefix="libblockdev.", suffix="mount_test")
        self.addCleanup(os.rmdir, tmp)

        self.addCleanup(utils.umount, self.loop_dev)

        succ = BlockDev.fs_mount(self.loop_dev, tmp, "nilfs2", None)
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
        succ = BlockDev.fs_mount(self.loop_dev, tmp, "nilfs2", "ro")
        self.assertTrue(succ)
        self.assertTrue(os.path.ismount(tmp))
        _ret, out, _err = utils.run_command("grep %s /proc/mounts" % tmp)
        self.assertTrue(out)
        self.assertIn("ro", out)

        succ = BlockDev.fs_unmount(self.loop_dev, False, False, None)
        self.assertTrue(succ)
        self.assertFalse(os.path.ismount(tmp))
