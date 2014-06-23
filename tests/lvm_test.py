import unittest
import os
import math

from utils import create_sparse_tempfile
from gi.repository import BlockDev
BlockDev.init(None)

class LvmNoDevTestCase(unittest.TestCase):
    def test_is_supported_pe_size(self):
        """Verify that lvm_is_supported_pe_size works as expected"""

        self.assertTrue(BlockDev.lvm_is_supported_pe_size(4 * 1024))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(4 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(6 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(12 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(15 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_supported_pe_size(4 * 1024**3))

        self.assertFalse(BlockDev.lvm_is_supported_pe_size(512))
        self.assertFalse(BlockDev.lvm_is_supported_pe_size(4097))
        self.assertFalse(BlockDev.lvm_is_supported_pe_size(65535))
        self.assertFalse(BlockDev.lvm_is_supported_pe_size(32 * 1024**3))

    def test_get_supported_pe_sizes(self):
        """Verify that supported PE sizes are really supported"""

        for size in BlockDev.lvm_get_supported_pe_sizes():
            self.assertTrue(BlockDev.lvm_is_supported_pe_size(size))

    def test_get_max_lv_size(self):
        """Verify that max LV size is correctly determined"""

        if os.uname()[-1] == "i686":
            # 32-bit arch
            expected = 16 * 1024**4
        else:
            # 64-bit arch
            expected = 8 * 1024**6

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), expected)

    def test_round_size_to_pe(self):
        """Verify that round_size_to_pe works as expected"""

        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 4 * 1024**2, True), 12 * 1024**2)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 4 * 1024**2, False), 8 * 1024**2)

        # default PE size is 4 MiB
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 0, True), 12 * 1024**2)
        self.assertEqual(BlockDev.lvm_round_size_to_pe(11 * 1024**2, 0, False), 8 * 1024**2)

    def test_get_lv_physical_size(self):
        """Verify that get_lv_physical_size works as expected"""

        self.assertEqual(BlockDev.lvm_get_lv_physical_size(25 * 1024**3, 4 * 1024**2),
                         25 * 1024**3 + 4 * 1024**2)

        # default PE size is 4 MiB
        self.assertEqual(BlockDev.lvm_get_lv_physical_size(25 * 1024**3, 0),
                         25 * 1024**3 + 4 * 1024**2)

        self.assertEqual(BlockDev.lvm_get_lv_physical_size(11 * 1024**2, 4 * 1024**2),
                         16 * 1024**2)

    def test_get_thpool_padding(self):
        """Verify that get_thpool_padding works as expected"""

        expected_padding = BlockDev.lvm_round_size_to_pe(int(math.ceil(11 * 1024**2 * 0.2)),
                                                         4 * 1024**2, True)
        self.assertEqual(BlockDev.lvm_get_thpool_padding(11 * 1024**2, 4 * 1024**2, False),
                         expected_padding)

        expected_padding = BlockDev.lvm_round_size_to_pe(int(math.ceil(11 * 1024**2 * (1.0/6.0))),
                                                         4 * 1024**2, True)
        self.assertEqual(BlockDev.lvm_get_thpool_padding(11 * 1024**2, 4 * 1024**2, True),
                         expected_padding)

    def test_is_valid_thpool_md_size(self):
        """Verify that is_valid_thpool_md_size works as expected"""

        self.assertTrue(BlockDev.lvm_is_valid_thpool_md_size(2 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_valid_thpool_md_size(3 * 1024**2))
        self.assertTrue(BlockDev.lvm_is_valid_thpool_md_size(16 * 1024**3))

        self.assertFalse(BlockDev.lvm_is_valid_thpool_md_size(1 * 1024**2))
        self.assertFalse(BlockDev.lvm_is_valid_thpool_md_size(17 * 1024**3))

    def test_is_valid_thpool_chunk_size(self):
        """Verify that is_valid_thpool_chunk_size works as expected"""

        # 64 KiB is OK with or without discard
        self.assertTrue(BlockDev.lvm_is_valid_thpool_chunk_size(64 * 1024, True))
        self.assertTrue(BlockDev.lvm_is_valid_thpool_chunk_size(64 * 1024, False))

        # 192 KiB is OK without discard, but NOK with discard
        self.assertTrue(BlockDev.lvm_is_valid_thpool_chunk_size(192 * 1024, False))
        self.assertFalse(BlockDev.lvm_is_valid_thpool_chunk_size(192 * 1024, True))

        # 191 KiB is NOK in both cases
        self.assertFalse(BlockDev.lvm_is_valid_thpool_chunk_size(191 * 1024, False))
        self.assertFalse(BlockDev.lvm_is_valid_thpool_chunk_size(191 * 1024, True))


class LvmTestCase(unittest.TestCase):
    # TODO: test pvmove (must create two PVs, a VG, a VG and some data in it
    #       first)
    def setUp(self):
        self.dev_file = create_sparse_tempfile("lvm_test", 1024**3)
        self.dev_file2 = create_sparse_tempfile("lvm_test", 1024**3)
        succ, loop, err = BlockDev.loop_setup(self.dev_file)
        if err or not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop, err = BlockDev.loop_setup(self.dev_file2)
        if err or not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

    def tearDown(self):
        succ, err = BlockDev.loop_teardown(self.loop_dev)
        if err or not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)
        succ, err = BlockDev.loop_teardown(self.loop_dev2)
        if err or not succ:
            os.unlink(self.dev_file2)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file2)

    def test_pvcreate_and_pvremove(self):
        """Verify that it's possible to create and destroy a PV"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_pvresize(self):
        """Verify that it's possible to resize a PV"""

        succ, err = BlockDev.lvm_pvresize(self.loop_dev, 1 * 1024**3)
        self.assertFalse(succ)
        self.assertTrue(err)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvresize(self.loop_dev, 1 * 1024**3)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_pvscan(self):
        """Verify that pvscan runs without issues with cache or without"""

        succ, err = BlockDev.lvm_pvscan(None, False)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvscan(self.loop_dev, True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvscan(None, True)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_pvinfo(self):
        """Verify that it's possible to gather info about a PV"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_pvinfo(self.loop_dev)
        self.assertTrue(info)
        self.assertIs(err, None)
        self.assertEqual(info.pv_name, self.loop_dev)
        self.assertTrue(info.pv_uuid)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_pvs(self):
        """Verify that it's possible to gather info about PVs"""

        pvs, err = BlockDev.lvm_pvs()
        orig_len = len(pvs)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        pvs, err = BlockDev.lvm_pvs()
        self.assertTrue(len(pvs) > orig_len)
        self.assertTrue(any(info.pv_name == self.loop_dev for info in pvs))

        info, err = BlockDev.lvm_pvinfo(self.loop_dev)
        self.assertTrue(info)
        self.assertIs(err, None)

        self.assertTrue(any(info.pv_uuid == all_info.pv_uuid for all_info in pvs))

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_vgcreate_pvremove(self):
        """Verify that it is possible to create and destroy a VG"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_vgactivate_vgdeactivate(self):
        """Verify that it is possible to (de)activate a VG"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgactivate("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgdeactivate("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgactivate("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgdeactivate("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_vgextend_vgreduce(self):
        """Verify that it is possible to extend/reduce a VG"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgextend("testVG", self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgreduce("testVG", self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgextend("testVG", self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgreduce("testVG", self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_vginfo(self):
        """Verify that it is possible to gather info about a VG"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_vginfo("testVG")
        self.assertTrue(info)
        self.assertIs(err, None)
        self.assertEqual(info.name, "testVG")
        self.assertTrue(info.uuid)
        self.assertEqual(info.pv_count, 2)
        self.assertTrue(info.size < 2 * 1024**3)
        self.assertEqual(info.free, info.size)
        self.assertEqual(info.extent_size, 4 * 1024**2)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_vgs(self):
        """Verify that it's possible to gather info about VGs"""

        vgs, err = BlockDev.lvm_vgs()
        orig_len = len(vgs)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        vgs, err = BlockDev.lvm_vgs()
        self.assertTrue(len(vgs) > orig_len)
        self.assertTrue(any(info.name == "testVG" for info in vgs))

        info, err = BlockDev.lvm_vginfo("testVG")
        self.assertTrue(info)
        self.assertIs(err, None)

        self.assertTrue(any(info.uuid == all_info.uuid for all_info in vgs))

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_lvcreate_lvremove(self):
        """Verify that it's possible to create/destroy an LV"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, [self.loop_dev])
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        # not enough space (only one PV)
        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 1048 * 1024**2, [self.loop_dev])
        self.assertFalse(succ)
        self.assertIn("Insufficient free space", err)

        # enough space (two PVs)
        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 1048 * 1024**2, [self.loop_dev, self.loop_dev2])
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_lvactivate_lvdeactivate(self):
        """Verify it's possible to (de)actiavate an LV"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, [self.loop_dev])
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvactivate("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvdeactivate("testVG", "testLV")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvactivate("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvdeactivate("testVG", "testLV")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_snapshotcreate_lvorigin_snapshotmerge(self):
        """Verify that LV snapshot support works"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, [self.loop_dev])
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvsnapshotcreate("testVG", "testLV", "testLV_bak", 256 * 1024**2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        origin_name, err = BlockDev.lvm_lvorigin("testVG", "testLV_bak")
        self.assertIs(err, None)
        self.assertEqual(origin_name, "testLV")

        succ, err = BlockDev.lvm_lvsnapshotmerge("testVG", "testLV_bak")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_lvinfo(self):
        """Verify that it is possible to gather info about an LV"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, [self.loop_dev])
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertTrue(info)
        self.assertIs(err, None)
        self.assertEqual(info.lv_name, "testLV")
        self.assertEqual(info.vg_name, "testVG")
        self.assertTrue(info.uuid)
        self.assertEqual(info.size, 512 * 1024**2)

        succ, err = BlockDev.lvm_lvremove("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_lvs(self):
        """Verify that it's possible to gather info about LVs"""

        lvs, err = BlockDev.lvm_lvs(None)
        orig_len = len(lvs)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvcreate("testVG", "testLV", 512 * 1024**2, [self.loop_dev])
        self.assertTrue(succ)
        self.assertIs(err, None)

        lvs, err = BlockDev.lvm_lvs(None)
        self.assertTrue(len(lvs) > orig_len)
        self.assertTrue(any(info.lv_name == "testLV" and info.vg_name == "testVG" for info in lvs))

        info, err = BlockDev.lvm_lvinfo("testVG", "testLV")
        self.assertTrue(info)
        self.assertIs(err, None)

        self.assertTrue(any(info.uuid == all_info.uuid for all_info in lvs))

        lvs, err = BlockDev.lvm_lvs("testVG")
        self.assertEqual(len(lvs), 1)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_thpoolcreate(self):
        """Verify that it is possible to create a thin pool"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024)
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertIs(err, None)
        self.assertIn("t", info.attr)

        succ, err = BlockDev.lvm_lvremove("testVG", "testPool", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_thlvcreate_thpoolname(self):
        """Verify that it is possible to create a thin LV and get its pool name"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_thlvcreate("testVG", "testPool", "testThLV", 1024**3)
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertIs(err, None)
        self.assertIn("t", info.attr)

        info, err = BlockDev.lvm_lvinfo("testVG", "testThLV")
        self.assertIs(err, None)
        self.assertIn("V", info.attr)

        pool, err = BlockDev.lvm_thlvpoolname("testVG", "testThLV")
        self.assertIs(err, None)
        self.assertEqual(pool, "testPool")

        succ, err = BlockDev.lvm_lvremove("testVG", "testThLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testPool", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_thsnapshotcreate(self):
        """Verify that it is possible to create a thin LV snapshot"""

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvcreate(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgcreate("testVG", [self.loop_dev, self.loop_dev2], 0)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_thpoolcreate("testVG", "testPool", 512 * 1024**2, 4 * 1024**2, 512 * 1024)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_thlvcreate("testVG", "testPool", "testThLV", 1024**3)
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_lvinfo("testVG", "testPool")
        self.assertIs(err, None)
        self.assertIn("t", info.attr)

        info, err = BlockDev.lvm_lvinfo("testVG", "testThLV")
        self.assertIs(err, None)
        self.assertIn("V", info.attr)

        succ, err = BlockDev.lvm_thsnapshotcreate("testVG", "testThLV", "testThLV_bak", "testPool")
        self.assertTrue(succ)
        self.assertIs(err, None)

        info, err = BlockDev.lvm_lvinfo("testVG", "testThLV_bak")
        self.assertIs(err, None)
        self.assertIn("V", info.attr)

        succ, err = BlockDev.lvm_lvremove("testVG", "testThLV_bak", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testThLV", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_lvremove("testVG", "testPool", True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_vgremove("testVG")
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev2)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.lvm_pvremove(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)
