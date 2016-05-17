import unittest
import os
from utils import create_sparse_tempfile
import overrides_hack

from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class PartTestCase(unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("part_test", 100 * 1024**2)
        self.dev_file2 = create_sparse_tempfile("part_test", 100 * 1024**2)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

    def _clean_up(self):
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

class PartCreateTableCase(PartTestCase):
    def test_create_table(self):
        """Verify that it is possible to create a new partition table"""

        # doesn't matter if we want to ignore any preexisting partition tables
        # or not on a nonexisting device
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_table ("/non/existing", BlockDev.PartTableType.MSDOS, False)

        with self.assertRaises(GLib.GError):
            BlockDev.part_create_table ("/non/existing", BlockDev.PartTableType.MSDOS, True)

        # doesn't matter if we want to ignore any preexisting partition tables
        # or not on a clean device
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, False)
        self.assertTrue(succ)

        succ = BlockDev.part_create_table (self.loop_dev2, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # should fail because of a preexisting partition table (and not ignoring it)
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, False)

        # should succeed if we want to ignore any preexisting partition tables
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

class PartCreatePartCase(PartTestCase):
    def test_create_part_simple(self):
        """Verify that it is possible to create a parition"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.path, ps2.path)
        self.assertEqual(ps.type, ps2.type);
        self.assertEqual(ps.start, ps2.start)
        self.assertEqual(ps.size, ps2.size)
        self.assertEqual(ps.flags, ps2.flags)

        pss = BlockDev.part_get_disk_parts (self.loop_dev)
        self.assertEqual(len(pss), 1)
        ps3 = pss[0]
        self.assertEqual(ps.path, ps3.path)
        self.assertEqual(ps.type, ps3.type)
        self.assertEqual(ps.start, ps3.start)
        self.assertEqual(ps.size, ps3.size)
        self.assertEqual(ps.flags, ps3.flags)

    def test_create_part_minimal_start_optimal(self):
        """Verify that it is possible to create a parition with minimal start and optimal alignment"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 1, 2 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertLessEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 2 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.path, ps2.path)
        self.assertEqual(ps.type, ps2.type);
        self.assertEqual(ps.start, ps2.start)
        self.assertEqual(ps.size, ps2.size)
        self.assertEqual(ps.flags, ps2.flags)

        pss = BlockDev.part_get_disk_parts (self.loop_dev)
        self.assertEqual(len(pss), 1)
        ps3 = pss[0]
        self.assertEqual(ps.path, ps3.path)
        self.assertEqual(ps.type, ps3.type)
        self.assertEqual(ps.start, ps3.start)
        self.assertEqual(ps.size, ps3.size)
        self.assertEqual(ps.flags, ps3.flags)

    def test_create_part_minimal_start(self):
        """Verify that it is possible to create a parition with minimal start"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 1, 2 * 1024**2, BlockDev.PartAlign.NONE)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 512)
        self.assertEqual(ps.size, 2 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.path, ps2.path)
        self.assertEqual(ps.type, ps2.type);
        self.assertEqual(ps.start, ps2.start)
        self.assertEqual(ps.size, ps2.size)
        self.assertEqual(ps.flags, ps2.flags)

        pss = BlockDev.part_get_disk_parts (self.loop_dev)
        self.assertEqual(len(pss), 1)
        ps3 = pss[0]
        self.assertEqual(ps.path, ps3.path)
        self.assertEqual(ps.type, ps3.type)
        self.assertEqual(ps.start, ps3.start)
        self.assertEqual(ps.size, ps3.size)
        self.assertEqual(ps.flags, ps3.flags)

class PartCreatePartFullCase(PartTestCase):
    def test_create_part_all_primary(self):
        """Verify that partition creation works as expected with all primary parts"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "p2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "p3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        ps4 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "p4")
        self.assertEqual(ps4.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertEqual(ps4.size, 10 * 1024**2)
        self.assertEqual(ps4.flags, 0)  # no flags (combination of bit flags)

        # no more primary partitions allowed in the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps4.start + ps4.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps4.start + ps4.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

    def test_create_part_with_extended(self):
        """Verify that partition creation works as expected with primary and extended parts"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "p2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "p3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        ps4 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "p4")
        self.assertEqual(ps4.type, BlockDev.PartType.EXTENDED)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertEqual(ps4.size, 10 * 1024**2)
        self.assertEqual(ps4.flags, 0)  # no flags (combination of bit flags)

        # no more primary partitions allowed in the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps4.start + ps4.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps4.start + ps4.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

    def test_create_part_with_extended_logical(self):
        """Verify that partition creation works as expected with primary, extended and logical parts"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "p2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps2.start + ps2.size + 1,
                                         30 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "p3")
        self.assertEqual(ps3.type, BlockDev.PartType.EXTENDED)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 30 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        # the logical partition has number 5 even though the extended partition
        # has number 3
        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps3.start + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps5)
        self.assertEqual(ps5.path, self.loop_dev + "p5")
        self.assertEqual(ps5.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps5.start < ps3.start + ps3.size)
        self.assertTrue(abs(ps5.size - 10 * 1024**2) < 2 * 1024**2)
        self.assertEqual(ps5.flags, 0)  # no flags (combination of bit flags)

        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps5.start + ps5.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "p6")
        self.assertEqual(ps6.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps6.start < ps3.start + ps3.size)
        self.assertEqual(ps6.size, 10 * 1024**2)
        self.assertEqual(ps6.flags, 0)  # no flags (combination of bit flags)

        ps7 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps6.start + ps6.size + 2 * 1024**2,
                                         5 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps7)
        self.assertEqual(ps7.path, self.loop_dev + "p7")
        self.assertEqual(ps7.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps7.start < ps3.start + ps3.size)
        self.assertLess(abs(ps7.start - (ps6.start + ps6.size + 2 * 1024**2)), 512)
        self.assertEqual(ps7.size, 5 * 1024**2)
        self.assertEqual(ps7.flags, 0)  # no flags (combination of bit flags)

        # here we go with the partition number 4
        ps4 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "p4")
        self.assertEqual(ps4.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertEqual(ps4.size, 10 * 1024**2)
        self.assertEqual(ps4.flags, 0)  # no flags (combination of bit flags)

        # no more primary partitions allowed in the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps3.start + ps3.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

    def test_create_part_with_extended_logical_gpt(self):
        """Verify that partition creation works as expected with primary, extended and logical parts on GPT"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "p2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "p3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        # no extended partitions allowed in the GPT table
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps3.start + ps3.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        # no logical partitions allowed in the GPT table
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

    def test_create_part_next(self):
        """Verify that partition creation works as expected with the NEXT (auto) type"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "p2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "p3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        # we should get a logical partition which has number 5
        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps3.start + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        ps4 = BlockDev.part_get_part_spec (self.loop_dev, self.loop_dev + "p4")
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "p4")
        self.assertEqual(ps4.type, BlockDev.PartType.EXTENDED)
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertGreater(ps4.size, 65 * 1024**2)

        self.assertTrue(ps5)
        self.assertEqual(ps5.path, self.loop_dev + "p5")
        self.assertEqual(ps5.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p4 no more
        # than 2 MiB after its start
        self.assertLessEqual(ps5.start, ps4.start + 2*1024**2)
        self.assertEqual(ps5.size, 10 * 1024**2)
        self.assertEqual(ps5.flags, 0)  # no flags (combination of bit flags)

        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps5.start + ps5.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "p6")
        self.assertEqual(ps6.type, BlockDev.PartType.LOGICAL)
        # logical partitions start 1 MiB after each other (no idea why)
        self.assertLessEqual(abs(ps6.start - (ps5.start + ps5.size + 1)), 1024**2 + 512)
        self.assertEqual(ps6.size, 10 * 1024**2)
        self.assertEqual(ps6.flags, 0)  # no flags (combination of bit flags)

        ps7 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps6.start + ps6.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps7)
        self.assertEqual(ps7.path, self.loop_dev + "p7")
        self.assertEqual(ps7.type, BlockDev.PartType.LOGICAL)
        # logical partitions start 1 MiB after each other (no idea why)
        self.assertLessEqual(abs(ps7.start - (ps6.start + ps6.size + 1)), 1024**2 + 512)
        self.assertEqual(ps7.size, 10 * 1024**2)
        self.assertEqual(ps7.flags, 0)  # no flags (combination of bit flags)

        # no more primary nor extended partitions allowed in the MSDOS table and
        # there should be no space
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps4.start + ps4.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        with self.assertRaises(GLib.GError):
            BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps4.start + ps4.size + 1,
                                       10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

    def test_create_part_next_gpt(self):
        """Verify that partition creation works as expected with the NEXT (auto) type on GPT"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "p1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "p2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "p3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        # we should get just next primary partition (GPT)
        ps4 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "p4")
        self.assertEqual(ps4.type, BlockDev.PartType.NORMAL)
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertEqual(ps4.size, 10 * 1024**2)

        # we should get just next primary partition (GPT)
        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps4.start + ps4.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps5)
        self.assertEqual(ps5.path, self.loop_dev + "p5")
        self.assertEqual(ps5.type, BlockDev.PartType.NORMAL)
        self.assertTrue(abs(ps5.start - (ps4.start + ps4.size + 1)) < ps.start)
        self.assertEqual(ps5.size, 10 * 1024**2)

        # we should get just next primary partition (GPT)
        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps5.start + ps4.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "p6")
        self.assertEqual(ps6.type, BlockDev.PartType.NORMAL)
        self.assertTrue(abs(ps6.start - (ps5.start + ps5.size + 1)) < ps.start)
        self.assertEqual(ps6.size, 10 * 1024**2)

class PartGetDiskPartsCase(PartTestCase):
    def test_get_disk_parts_empty(self):
        """Verify that getting info about partitions with no label works"""
        with self.assertRaises(GLib.GError):
            BlockDev.part_get_disk_parts (self.loop_dev)

class PartCreateDeletePartCase(PartTestCase):
    def test_create_delete_part_simple(self):
        """Verify that it is possible to create and delete a parition"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        pss = BlockDev.part_get_disk_parts (self.loop_dev)
        self.assertEqual(len(pss), 1)

        succ = BlockDev.part_delete_part (self.loop_dev, ps.path)
        self.assertTrue(succ)
        pss = BlockDev.part_get_disk_parts (self.loop_dev)
        self.assertEqual(len(pss), 0)

class PartSetFlagCase(PartTestCase):
    def test_set_part_flag(self):
        """Verify that it is possible to set a partition flag"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.BOOT, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.BOOT)

        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.BOOT, False)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.BOOT)

        # add another partition and do some more tests on that one
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.BOOT, True)
        self.assertTrue(succ)
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.LVM, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.BOOT)
        self.assertTrue(ps.flags & BlockDev.PartFlag.LVM)

        # SWAP label not supported on the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.SWAP, True)
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.SWAP, False)
        # so isn't GPT_HIDDEN
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_HIDDEN, True)
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_HIDDEN, False)

        # also try some GPT-only flags
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_HIDDEN, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY, False)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)

class PartSetFlagsCase(PartTestCase):
    def test_set_part_flags(self):
        """Verify that it is possible to set multiple partition flags at once"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        succ = BlockDev.part_set_part_flags (self.loop_dev, ps.path, BlockDev.PartFlag.BOOT)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.BOOT)

        # 0 -> unset all
        succ = BlockDev.part_set_part_flags (self.loop_dev, ps.path, 0)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.BOOT)

        # add another partition and do some more tests on that one
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        succ = BlockDev.part_set_part_flags (self.loop_dev, ps.path, BlockDev.PartFlag.BOOT | BlockDev.PartFlag.LVM)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.BOOT)
        self.assertTrue(ps.flags & BlockDev.PartFlag.LVM)

        # SWAP label not supported on the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_flags (self.loop_dev, ps.path, BlockDev.PartFlag.SWAP)

        # also try some GPT-only flags
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        succ = BlockDev.part_set_part_flags (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY | BlockDev.PartFlag.GPT_HIDDEN)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)

        succ = BlockDev.part_set_part_flags (self.loop_dev, ps.path, 0) # no flags
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        self.assertFalse(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)


class PartSetNameCase(PartTestCase):
    def test_set_part_name(self):
        """Verify that it is possible to set partition name"""

        # we first need a GPT partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertIn(ps.name, ("", None))  # no name

        succ = BlockDev.part_set_part_name (self.loop_dev, ps.path, "TEST")
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.name, "TEST")

        succ = BlockDev.part_set_part_name (self.loop_dev, ps.path, "")
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.name, "")

        # let's now test an MSDOS partition table (doesn't support names)
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertIn(ps.name, ("", None))  # no name

        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_name (self.loop_dev, ps.path, "")

        # we should still get proper data back though
        self.assertTrue(ps)
        self.assertIn(ps.name, ("", None))  # no name

class PartSetTypeCase(PartTestCase):
    def test_set_part_type(self):
        """Verify that it is possible to set and get partition type"""

        # we first need a GPT partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertTrue(ps.type_guid)  # should have some type

        succ = BlockDev.part_set_part_type (self.loop_dev, ps.path, "E6D6D379-F507-44C2-A23C-238F2A3DF928")
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.type_guid, "E6D6D379-F507-44C2-A23C-238F2A3DF928")

        succ = BlockDev.part_set_part_type (self.loop_dev, ps.path, "0FC63DAF-8483-4772-8E79-3D69D8477DE4")
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.type_guid, "0FC63DAF-8483-4772-8E79-3D69D8477DE4")

        # let's now test an MSDOS partition table (doesn't support type GUIDs)
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertIn(ps.type_guid, ("", None))  # no type GUID

        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_type (self.loop_dev, ps.path, "0FC63DAF-8483-4772-8E79-3D69D8477DE4")

        # we should still get proper data back though
        self.assertTrue(ps)
        self.assertIn(ps.type_guid, ("", None))  # no type GUID
