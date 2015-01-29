import unittest
import os

from utils import create_sparse_tempfile
from gi.repository import GLib, BlockDev
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

TEST_MNT = "/tmp/libblockdev_test_mnt"

def wipefs(device):
    os.system("wipefs -a %s > /dev/null" % device)

def mount(device, where):
    if not os.path.isdir(where):
        os.makedirs(where)
    os.system("mount %s %s" % (device, where))

def umount(what):
    os.system("umount %s" % what)
    os.rmdir(what)

class BtrfsTestCase (unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("lvm_test", 1024**3)
        self.dev_file2 = create_sparse_tempfile("lvm_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

    def tearDown(self):
        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)
        succ = BlockDev.loop_teardown(self.loop_dev2)
        if  not succ:
            os.unlink(self.dev_file2)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file2)

    def test_create_and_query_volume(self):
        """Verify that btrfs volume creation and querying works"""

        # one device, no label
        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

        wipefs(self.loop_dev)

        # one device, with label
        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

        wipefs(self.loop_dev)

        # two devices, no specific data/metadata layout
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

        # two devices, raid1 data
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", "raid1", None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

        # two devices, raid1 metadata
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", None, "raid1")
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

        # two devices, raid1 data and metadata
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", "raid1", "raid1")
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

    def test_add_remove_device(self):
        """Verify that it is possible to add/remove device to a btrfs volume"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

        mount(self.loop_dev, TEST_MNT)

        succ = BlockDev.btrfs_add_device(TEST_MNT, self.loop_dev2)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        succ = BlockDev.btrfs_remove_device(TEST_MNT, self.loop_dev2)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

        umount(TEST_MNT)
        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

    def test_create_delete_subvolume(self):
        """Verify that it is possible to create/delete subvolume"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 0)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1")
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)

        succ = BlockDev.btrfs_delete_subvolume(TEST_MNT, "subvol1")
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 0)

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_create_snapshot(self):
        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 0)

        # R/W snapshot
        succ = BlockDev.btrfs_create_snapshot(TEST_MNT, TEST_MNT + "/snap1", False)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 1)

        # RO snapshot
        succ = BlockDev.btrfs_create_snapshot(TEST_MNT, TEST_MNT + "/snap2", True)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 2)

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_get_default_subvolume_id(self):
        """Verify that getting default subvolume ID works as expected"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        # not mounted yet, should fail
        with self.assertRaisesRegexp(GLib.GError, r".*not mounted.*"):
            ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)

        mount(self.loop_dev, TEST_MNT)

        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEqual(ret, 5)

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_set_default_subvolume(self):
        """Verify that setting default subvolume works as expected"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEqual(ret, 5)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1")
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)

        new_id = next((subvol.id for subvol in subvols), None)
        self.assertIsNot(new_id, None)
        succ = BlockDev.btrfs_set_default_subvolume(TEST_MNT, new_id)
        self.assertTrue(succ)
        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEquals(ret, new_id)

        succ = BlockDev.btrfs_set_default_subvolume(TEST_MNT, 5)
        self.assertTrue(succ)
        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEquals(ret, 5)

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_list_devices(self):
        """Verify that it is possible to get info about devices"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)
        self.assertEqual(devs[0].id, 1)
        self.assertEqual(devs[1].id, 2)
        self.assertEqual(devs[0].path, self.loop_dev)
        self.assertEqual(devs[1].path, self.loop_dev2)
        self.assertTrue(devs[0].size >= 0)
        self.assertTrue(devs[1].size >= 0)
        self.assertTrue(devs[0].used >= 0)
        self.assertTrue(devs[1].used >= 0)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

    def test_list_snapshots(self):
        """Verify that it is possible to get info about snapshots"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 0)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1")
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)
        self.assertEqual(subvols[0].parent_id, 5)
        self.assertEqual(subvols[0].path, "subvol1")

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_filesystem_info(self):
        """Verify that it is possible to get filesystem info"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        info = BlockDev.btrfs_filesystem_info(TEST_MNT)
        self.assertEqual(info.label, "myShinyBtrfs")
        self.assertTrue(info.uuid)
        self.assertEqual(info.num_devices, 1)
        self.assertTrue(info.used >= 0)

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_mkfs(self):
        """Verify that it is possible to create a btrfs filesystem"""

        # mkfs is the same as create_volume
        self.test_create_and_query_volume()

    def test_resize(self):
        """Verify that is is possible to resize a btrfs filesystem"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        succ = BlockDev.btrfs_resize(TEST_MNT, 500 * 1024**2)
        self.assertTrue(succ)

        umount(TEST_MNT)
        wipefs(self.loop_dev)

    def test_check(self):
        """Verify that it's possible to check the btrfs filesystem"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None)
        self.assertTrue(succ)

        succ = BlockDev.btrfs_check(self.loop_dev)
        self.assertTrue(succ)

        wipefs(self.loop_dev)

    def test_repair(self):
        """Verify that it's possible to repair the btrfs filesystem"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None)
        self.assertTrue(succ)

        succ = BlockDev.btrfs_repair(self.loop_dev)
        self.assertTrue(succ)

        wipefs(self.loop_dev)

    def test_change_label(self):
        """Verify that it's possible to change btrfs filesystem's label"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        succ = BlockDev.btrfs_change_label(TEST_MNT, "newLabel")
        self.assertTrue(succ)

        info = BlockDev.btrfs_filesystem_info(TEST_MNT)
        self.assertEqual(info.label, "newLabel")

        umount(TEST_MNT)
        wipefs(self.loop_dev)
