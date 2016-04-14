from __future__ import division

import unittest
import os
import six

import overrides_hack
from utils import create_sparse_tempfile, fake_utils, fake_path
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
    try:
        os.system("umount %s &>/dev/null" % what)
        os.rmdir(what)
    except OSError:
        # no such file or directory
        pass

class BtrfsTestCase(unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
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

    def _clean_up(self):
        umount(TEST_MNT)
        succ = BlockDev.loop_teardown(self.loop_dev)
        if not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)
        succ = BlockDev.loop_teardown(self.loop_dev2)
        if  not succ:
            os.unlink(self.dev_file2)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file2)

class BtrfsTestCreateQuerySimple(BtrfsTestCase):
    def test_create_and_query_volume(self):
        """Verify that btrfs volume creation and querying works"""

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_volume([], None, None, None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_volume(["/non/existing/device"], None, None, None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_volume([self.loop_dev], None, "RaID7", None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_volume([self.loop_dev], None, None, "RaID7", None)

        # one device, no label
        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None, None)
        self.assertTrue(succ)

        # already created
        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_volume([self.loop_dev], None, None, None, None)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

class BtrfsTestCreateQueryLabel(BtrfsTestCase):
    def test_create_and_query_volume_label(self):
        """Verify that btrfs volume creation with label works"""

        # one device, with label
        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)


class BtrfsTestCreateQueryTwoDevs(BtrfsTestCase):
    def test_create_and_query_volume_two_devs(self):
        """Verify that btrfs volume creation with two devices works"""

        # two devices, no specific data/metadata layout
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

class BtrfsTestCreateQueryTwoDevsRaids(BtrfsTestCase):
    def test_create_and_query_volume_two_devs(self):
        """Verify that btrfs volume creation with two devices and raid (meta)data works"""

        # two devices, raid1 data
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", "raid1", None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

        # two devices, raid1 metadata
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", None, "raid1", None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        wipefs(self.loop_dev)
        wipefs(self.loop_dev2)

        # two devices, raid1 data and metadata
        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs",
                                            "raid1", "raid1")
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

class BtrfsTestAddRemoveDevice(BtrfsTestCase):
    def test_add_remove_device(self):
        """Verify that it is possible to add/remove device to a btrfs volume"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

        mount(self.loop_dev, TEST_MNT)

        succ = BlockDev.btrfs_add_device(TEST_MNT, self.loop_dev2, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 2)

        succ = BlockDev.btrfs_remove_device(TEST_MNT, self.loop_dev2, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

class BtrfsTestCreateDeleteSubvolume(BtrfsTestCase):
    def test_create_delete_subvolume(self):
        """Verify that it is possible to create/delete subvolume"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 0)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1", None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)

        # already there
        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1", None)

        succ = BlockDev.btrfs_delete_subvolume(TEST_MNT, "subvol1", None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 0)

        # already removed
        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_delete_subvolume(TEST_MNT, "subvol1", None)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1", None)
        self.assertTrue(succ)

        # add it back
        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)

        # and create another subvolume in it
        succ = BlockDev.btrfs_create_subvolume(os.path.join(TEST_MNT, "subvol1"), "subvol1.1", None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 2)

        # make sure subvolumes are sorted properly (parents before children)
        seen = set()
        for subvol in subvols:
            seen.add(subvol)
            self.assertTrue(subvol.parent_id == BlockDev.BTRFS_MAIN_VOLUME_ID or any(subvol.parent_id == other.id for other in seen))

class BtrfsTestCreateSnapshot(BtrfsTestCase):
    def test_create_snapshot(self):
        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 0)

        # R/W snapshot
        succ = BlockDev.btrfs_create_snapshot(TEST_MNT, TEST_MNT + "/snap1", False, None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 1)

        # RO snapshot
        succ = BlockDev.btrfs_create_snapshot(TEST_MNT, TEST_MNT + "/snap2", True, None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 2)

class BtrfsTestGetDefaultSubvolumeID(BtrfsTestCase):
    def test_get_default_subvolume_id(self):
        """Verify that getting default subvolume ID works as expected"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        # not mounted yet, should fail
        with six.assertRaisesRegex(self, GLib.GError, r".*(can't|cannot) access.*"):
            ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)

        mount(self.loop_dev, TEST_MNT)

        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEqual(ret, 5)

class BtrfsTestSetDefaultSubvolumeID(BtrfsTestCase):
    def test_set_default_subvolume(self):
        """Verify that setting default subvolume works as expected"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEqual(ret, 5)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1", None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)

        new_id = next((subvol.id for subvol in subvols), None)
        self.assertIsNot(new_id, None)
        succ = BlockDev.btrfs_set_default_subvolume(TEST_MNT, new_id, None)
        self.assertTrue(succ)
        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEqual(ret, new_id)

        succ = BlockDev.btrfs_set_default_subvolume(TEST_MNT, 5, None)
        self.assertTrue(succ)
        ret = BlockDev.btrfs_get_default_subvolume_id(TEST_MNT)
        self.assertEqual(ret, 5)

class BtrfsTestListDevices(BtrfsTestCase):
    def test_list_devices(self):
        """Verify that it is possible to get info about devices"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2], "myShinyBtrfs", None, None, None)
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

class BtrfsTestListSubvolumes(BtrfsTestCase):
    def test_list_subvolumes(self):
        """Verify that it is possible to get info about subvolumes"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, True)
        self.assertEqual(len(subvols), 0)

        succ = BlockDev.btrfs_create_subvolume(TEST_MNT, "subvol1", None)
        self.assertTrue(succ)

        subvols = BlockDev.btrfs_list_subvolumes(TEST_MNT, False)
        self.assertEqual(len(subvols), 1)
        self.assertEqual(subvols[0].parent_id, 5)
        self.assertEqual(subvols[0].path, "subvol1")

class BtrfsTestFilesystemInfo(BtrfsTestCase):
    def test_filesystem_info(self):
        """Verify that it is possible to get filesystem info"""

        label = "My 'Shiny' Btrfs"
        succ = BlockDev.btrfs_create_volume([self.loop_dev], label, None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        info = BlockDev.btrfs_filesystem_info(TEST_MNT)
        self.assertTrue(info)
        self.assertEqual(info.label, label)
        self.assertTrue(info.uuid)
        self.assertEqual(info.num_devices, 1)
        self.assertTrue(info.used >= 0)

class BtrfsTestFilesystemInfoNoLabel(BtrfsTestCase):
    def test_filesystem_info(self):
        """Verify that it is possible to get filesystem info for a volume with no label"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        info = BlockDev.btrfs_filesystem_info(TEST_MNT)
        self.assertEqual(info.label, str())
        self.assertTrue(info.uuid)
        self.assertEqual(info.num_devices, 1)
        self.assertTrue(info.used >= 0)

class BtrfsTestMkfs(BtrfsTestCase):
    def test_mkfs(self):
        """Verify that it is possible to create a btrfs filesystem"""

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_mkfs([], None, None, None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_mkfs(["/non/existing/device"], None, None, None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_mkfs([self.loop_dev], None, "RaID7", None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_mkfs([self.loop_dev], None, None, "RaID7", None)

        # one device, no label
        succ = BlockDev.btrfs_mkfs([self.loop_dev], None, None, None, None)
        self.assertTrue(succ)

        # already created
        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_mkfs([self.loop_dev], None, None, None, None)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

class BtrfsTestMkfsLabel(BtrfsTestCase):
    def test_mkfs_label(self):
        """Verify that it is possible to create a btrfs filesystem with a label"""

        succ = BlockDev.btrfs_mkfs([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        devs = BlockDev.btrfs_list_devices(self.loop_dev)
        self.assertEqual(len(devs), 1)

class BtrfsTestResize(BtrfsTestCase):
    def test_resize(self):
        """Verify that is is possible to resize a btrfs filesystem"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        succ = BlockDev.btrfs_resize(TEST_MNT, 500 * 1024**2, None)
        self.assertTrue(succ)

class BtrfsTestCheck(BtrfsTestCase):
    def test_check(self):
        """Verify that it's possible to check the btrfs filesystem"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None, None)
        self.assertTrue(succ)

        succ = BlockDev.btrfs_check(self.loop_dev, None)
        self.assertTrue(succ)

class BtrfsTestRepair(BtrfsTestCase):
    def test_repair(self):
        """Verify that it's possible to repair the btrfs filesystem"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], None, None, None, None)
        self.assertTrue(succ)

        succ = BlockDev.btrfs_repair(self.loop_dev, None)
        self.assertTrue(succ)

class BtrfsTestChangeLabel(BtrfsTestCase):
    def test_change_label(self):
        """Verify that it's possible to change btrfs filesystem's label"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev], "myShinyBtrfs", None, None, None)
        self.assertTrue(succ)

        mount(self.loop_dev, TEST_MNT)

        succ = BlockDev.btrfs_change_label(TEST_MNT, "newLabel")
        self.assertTrue(succ)

        info = BlockDev.btrfs_filesystem_info(TEST_MNT)
        self.assertEqual(info.label, "newLabel")

class BtrfsTooSmallTestCase (unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("lvm_test", BlockDev.BTRFS_MIN_MEMBER_SIZE)
        self.dev_file2 = create_sparse_tempfile("lvm_test", BlockDev.BTRFS_MIN_MEMBER_SIZE//2)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

    def _clean_up(self):
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

    def test_create_too_small(self):
        """Verify that an attempt to create BTRFS on a too small device fails"""

        # even one small devices is enough for the fail
        with self.assertRaises(GLib.GError):
            BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2],
                                         None, None, None)

class BtrfsJustBigEnoughTestCase (unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("lvm_test", BlockDev.BTRFS_MIN_MEMBER_SIZE)
        self.dev_file2 = create_sparse_tempfile("lvm_test", BlockDev.BTRFS_MIN_MEMBER_SIZE)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

    def _clean_up(self):
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

    def test_create_just_enough(self):
        """Verify that creating BTRFS on a just big enough devices works"""

        succ = BlockDev.btrfs_create_volume([self.loop_dev, self.loop_dev2],
                                            None, None, None)
        self.assertTrue(succ)


class FakeBtrfsUtilsTestCase(unittest.TestCase):
    # no setUp nor tearDown needed, we are gonna use fake utils

    def test_list_subvols_weird_docker_data(self):
        """Verify that list_subvolumes works as expected on weird data from one Docker use case"""

        with fake_utils("tests/btrfs_subvols_docker"):
            subvols = BlockDev.btrfs_list_subvolumes("fake_dev", False)

        # make sure subvolumes are sorted properly (parents before children)
        seen = set()
        for subvol in subvols:
            seen.add(subvol)
            self.assertTrue(subvol.parent_id == BlockDev.BTRFS_MAIN_VOLUME_ID or any(subvol.parent_id == other.id for other in seen))

        # check that one of the weird subvols is in the list of subvolumes
        self.assertTrue(any(subvol for subvol in subvols if subvol.path == "docker/btrfs/subvolumes/f2062b736fbabbe4da752632ac4deae87fcb916add6d7d8f5cecee4cbdc41fd9"))

class BTRFSUnloadTest(unittest.TestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, None, True, None)

    def test_check_low_version(self):
        """Verify that checking the minimum BTRFS version works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/btrfs_low_version/"):
            # too low version of BTRFS available, the BTRFS plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("btrfs", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("btrfs", BlockDev.get_available_plugin_names())

    def test_check_new_version_format(self):
        """Verify that checking the minimum BTRFS version works as expected with the new format"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        # check that new version format is correctly parsed
        with fake_utils("tests/btrfs_new_version_format/"):
            BlockDev.reinit(None, True, None)

        self.assertIn("btrfs", BlockDev.get_available_plugin_names())

        BlockDev.reinit(None, True, None)
        self.assertIn("btrfs", BlockDev.get_available_plugin_names())

    def test_check_no_btrfs(self):
        """Verify that checking btrfs tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path():
            # no btrfs tool available, the BTRFS plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("btrfs", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("btrfs", BlockDev.get_available_plugin_names())
