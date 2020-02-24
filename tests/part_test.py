import unittest
import os
from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, TestTags, tag_test, run_command
import overrides_hack

from gi.repository import BlockDev, GLib
from bytesize.bytesize import Size, ROUND_UP, B


class PartTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("part",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("part_test", 100 * 1024**2)
        self.dev_file2 = create_sparse_tempfile("part_test", 100 * 1024**2)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev2 = create_lio_device(self.dev_file2)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)

        try:
            delete_lio_device(self.loop_dev2)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file2)

class PartCreateTableCase(PartTestCase):
    @tag_test(TestTags.CORE)
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


class PartGetDiskSpecCase(PartTestCase):
    @tag_test(TestTags.CORE)
    def test_get_disk_spec(self):
        """Verify that it is possible to get information about disk"""

        with self.assertRaises(GLib.GError):
            BlockDev.part_get_disk_spec ("/non/existing/device")

        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev)
        self.assertEqual(ps.sector_size, 512)
        self.assertGreaterEqual(ps.size, 100 * 1024**2 - 512)
        self.assertEqual(ps.table_type, BlockDev.PartTableType.UNDEF)
        self.assertEqual(ps.flags, 0)

        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev)
        self.assertEqual(ps.sector_size, 512)
        self.assertGreaterEqual(ps.size, 100 * 1024**2 - 512)
        self.assertEqual(ps.table_type, BlockDev.PartTableType.MSDOS)
        self.assertEqual(ps.flags, 0)

        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev)
        self.assertEqual(ps.sector_size, 512)
        self.assertGreaterEqual(ps.size, 100 * 1024**2 - 512)
        self.assertEqual(ps.table_type, BlockDev.PartTableType.GPT)
        self.assertEqual(ps.flags, 0)

class PartCreatePartCase(PartTestCase):
    @tag_test(TestTags.CORE)
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
        self.assertEqual(ps.path, self.loop_dev + "1")
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
        self.assertEqual(ps.path, self.loop_dev + "1")
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
        self.assertEqual(ps.path, self.loop_dev + "1")
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
    @tag_test(TestTags.CORE)
    def test_full_device_partition(self):
        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # create partition spanning whole device even disregarding the partition table (loop_dev size)
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 0, 100 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        succ = BlockDev.part_delete_part (self.loop_dev, ps.path)
        self.assertTrue(succ)

        # same, but create a maximal partition automatically
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 0, 0, BlockDev.PartAlign.OPTIMAL)
        succ = BlockDev.part_delete_part (self.loop_dev, ps.path)
        self.assertTrue(succ)

        # start at byte 1 and create partition spanning whole device explicitly
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 1, 100 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        succ = BlockDev.part_delete_part (self.loop_dev, ps.path)
        self.assertTrue(succ)

        # start at byte 1 and create a maximal partition automatically
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 1, 0, BlockDev.PartAlign.OPTIMAL)
        succ = BlockDev.part_delete_part (self.loop_dev, ps.path)
        self.assertTrue(succ)

    def test_create_part_all_primary(self):
        """Verify that partition creation works as expected with all primary parts"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        ps4 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "4")
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
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        ps4 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "4")
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

    @tag_test(TestTags.CORE)
    def test_create_part_with_extended_logical(self):
        """Verify that partition creation works as expected with primary, extended and logical parts"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps2.start + ps2.size + 1,
                                         30 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
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
        self.assertEqual(ps5.path, self.loop_dev + "5")
        self.assertEqual(ps5.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps5.start < ps3.start + ps3.size)
        self.assertTrue(abs(ps5.size - 10 * 1024**2) < 2 * 1024**2)
        self.assertEqual(ps5.flags, 0)  # no flags (combination of bit flags)

        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps5.start + ps5.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "6")
        self.assertEqual(ps6.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps6.start < ps3.start + ps3.size)
        self.assertEqual(ps6.size, 10 * 1024**2)
        self.assertEqual(ps6.flags, 0)  # no flags (combination of bit flags)

        ps7 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps6.start + ps6.size + 2 * 1024**2,
                                         5 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps7)
        self.assertEqual(ps7.path, self.loop_dev + "7")
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
        self.assertEqual(ps4.path, self.loop_dev + "4")
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
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
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

    @tag_test(TestTags.CORE)
    def test_create_part_next(self):
        """Verify that partition creation works as expected with the NEXT (auto) type"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
        self.assertEqual(ps3.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 10 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        # we should get a logical partition which has number 5
        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps3.start + ps3.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        ps4 = BlockDev.part_get_part_spec (self.loop_dev, self.loop_dev + "4")
        self.assertTrue(ps4)
        self.assertEqual(ps4.path, self.loop_dev + "4")
        self.assertEqual(ps4.type, BlockDev.PartType.EXTENDED)
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertGreater(ps4.size, 65 * 1024**2)

        self.assertTrue(ps5)
        self.assertEqual(ps5.path, self.loop_dev + "5")
        self.assertEqual(ps5.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p4 no more
        # than 2 MiB after its start
        self.assertLessEqual(ps5.start, ps4.start + 2*1024**2)
        self.assertEqual(ps5.size, 10 * 1024**2)
        self.assertEqual(ps5.flags, 0)  # no flags (combination of bit flags)

        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps5.start + ps5.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "6")
        self.assertEqual(ps6.type, BlockDev.PartType.LOGICAL)
        # logical partitions start 1 MiB after each other (no idea why)
        self.assertLessEqual(abs(ps6.start - (ps5.start + ps5.size + 1)), 1024**2 + 512)
        self.assertEqual(ps6.size, 10 * 1024**2)
        self.assertEqual(ps6.flags, 0)  # no flags (combination of bit flags)

        ps7 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps6.start + ps6.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps7)
        self.assertEqual(ps7.path, self.loop_dev + "7")
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

    @tag_test(TestTags.CORE)
    def test_create_part_next_gpt(self):
        """Verify that partition creation works as expected with the NEXT (auto) type on GPT"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps2.start + ps2.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
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
        self.assertEqual(ps4.path, self.loop_dev + "4")
        self.assertEqual(ps4.type, BlockDev.PartType.NORMAL)
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertEqual(ps4.size, 10 * 1024**2)

        # we should get just next primary partition (GPT)
        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps4.start + ps4.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps5)
        self.assertEqual(ps5.path, self.loop_dev + "5")
        self.assertEqual(ps5.type, BlockDev.PartType.NORMAL)
        self.assertTrue(abs(ps5.start - (ps4.start + ps4.size + 1)) < ps.start)
        self.assertEqual(ps5.size, 10 * 1024**2)

        # we should get just next primary partition (GPT)
        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NEXT, ps5.start + ps4.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "6")
        self.assertEqual(ps6.type, BlockDev.PartType.NORMAL)
        self.assertTrue(abs(ps6.start - (ps5.start + ps5.size + 1)) < ps.start)
        self.assertEqual(ps6.size, 10 * 1024**2)

class PartGetDiskPartsCase(PartTestCase):
    def test_get_disk_parts_empty(self):
        """Verify that getting info about partitions with no label works"""
        with self.assertRaises(GLib.GError):
            BlockDev.part_get_disk_parts (self.loop_dev)


def _round_up_mib(size):
    # convert size to nearest MiB (up)
    rounded = Size(size).round_to_nearest(Size(1024**2), rounding=ROUND_UP)
    return rounded.get_bytes()


class PartGetDiskFreeRegions(PartTestCase):
    @tag_test(TestTags.CORE)
    def test_get_disk_free_regions(self):
        """Verify that it is possible to get info about free regions on a disk"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 1, 10 * 1024**2, BlockDev.PartAlign.NONE)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 512)
        self.assertEqual(ps.size, 10 * 1024**2)

        fis = BlockDev.part_get_disk_free_regions (self.loop_dev)
        self.assertEqual(len(fis), 1)
        fi = fis[0]
        self.assertEqual(fi.start, _round_up_mib(ps.start + ps.size))
        self.assertGreaterEqual(fi.size, 89 * 1024**2)

        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 10 * 1024**2,
                                        10 * 1024**2, BlockDev.PartAlign.NONE)
        fis = BlockDev.part_get_disk_free_regions (self.loop_dev)
        self.assertEqual(len(fis), 2)  # first part, gap, second part, free
        fi = fis[0]
        self.assertEqual(fi.start, _round_up_mib(512 + 10 * 1024**2))
        self.assertGreaterEqual(fi.size, 9 * 1024**2)
        fi = fis[1]
        self.assertEqual(fi.start, _round_up_mib(512 + 30 * 1024**2))
        self.assertGreaterEqual(fi.size, 69 * 1024**2)

        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps.start + ps.size + 1,
                                        50 * 1024**2, BlockDev.PartAlign.NONE)
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps.start + 1024**2,
                                        10 * 1024**2, BlockDev.PartAlign.NONE)
        fis = BlockDev.part_get_disk_free_regions (self.loop_dev)
        self.assertEqual(len(fis), 3)  # first part, gap[0], second part, extended, logical, free extended[1], free[2]

        fi = fis[0]
        self.assertEqual(fi.start, _round_up_mib(512 + 10 * 1024**2))
        self.assertGreater(fi.size, 9 * 1024**2)
        fi = fis[1]
        self.assertGreaterEqual(fi.start, _round_up_mib(ps.start + ps.size))
        self.assertGreaterEqual(fi.size, 37 * 1024**2)
        fi = fis[2]
        self.assertGreaterEqual(fi.start, 80 * 1024**2)
        self.assertGreaterEqual(fi.size, 19 * 1024**2)

        # now something simple with GPT
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 1, 10 * 1024**2, BlockDev.PartAlign.NONE)

        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 512)
        self.assertEqual(ps.size, 10 * 1024**2)

        fis = BlockDev.part_get_disk_free_regions (self.loop_dev)
        self.assertEqual(len(fis), 1)
        fi = fis[0]
        self.assertEqual(fi.start, _round_up_mib(ps.start + ps.size))
        self.assertGreaterEqual(fi.size, 89 * 1024**2)

class PartGetBestFreeRegion(PartTestCase):
    def test_get_best_free_region(self):
        """Verify that it is possible to get info about the best free region on a disk"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        ps1 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 1, 10 * 1024**2, BlockDev.PartAlign.NONE)
        self.assertTrue(ps1)
        self.assertEqual(ps1.path, self.loop_dev + "1")
        self.assertEqual(ps1.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps1.start, 512)
        self.assertEqual(ps1.size, 10 * 1024**2)

        # create a 20MiB gap between the partitions
        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps1.start + ps1.size + 20 * 1024**2,
                                         10 * 1024**2, BlockDev.PartAlign.NONE)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps2.start, ps1.start + ps1.size + 20 * 1024**2)
        self.assertEqual(ps2.size, 10 * 1024**2)

        # normal partition should go in between the partitions because there's enough space for it
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.NORMAL, 10 * 1024**2)
        self.assertLess(ps.start, ps2.start)

        # extended partition should be as big as possible so it shouldn't go in between the partitions
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.EXTENDED, 10 * 1024**2)
        self.assertGreaterEqual(ps.start, ps2.start + ps2.size)

        # create a 10MiB gap between the partitions
        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps2.start + ps2.size + 10 * 1024**2,
                                         45 * 1024**2, BlockDev.PartAlign.NONE)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
        self.assertEqual(ps3.type, BlockDev.PartType.EXTENDED)
        self.assertEqual(ps3.start, ps2.start + ps2.size + 10 * 1024**2)
        self.assertEqual(ps3.size, 45 * 1024**2)

        # there should now be 5 MiB left after the third partition which is enough for a 3MiB partition
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.NORMAL, 3 * 1024**2)
        self.assertGreaterEqual(ps.start, ps3.start + ps3.size)

        # 7MiB partition should go in between the second and third partitions because there's enough space
        # for it there
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.NORMAL, 7 * 1024**2)
        self.assertGreaterEqual(ps.start, ps2.start + ps2.size)
        self.assertLess(ps.start, ps3.start)

        # 15MiB partition should go in between the first and second partitions because that's the only
        # space big enough for it
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.NORMAL, 15 * 1024**2)
        self.assertGreaterEqual(ps.start, ps1.start + ps1.size)
        self.assertLess(ps.start, ps2.start)

        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps3.start + 20 * 1024**2,
                                         15 * 1024**2, BlockDev.PartAlign.NONE)
        self.assertEqual(ps5.path, self.loop_dev + "5")
        self.assertEqual(ps5.type, BlockDev.PartType.LOGICAL)
        self.assertEqual(ps5.start, ps3.start + 20 * 1024**2)
        self.assertEqual(ps5.size, 15 * 1024**2)

        # 5MiB logical partition should go after the fifth partition because there's enough space for it
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.LOGICAL, 5 * 1024**2)
        self.assertGreaterEqual(ps.start, ps5.start + ps5.size)
        self.assertLess(ps.start, ps3.start + ps3.size)

        # 15MiB logical partition should go before the fifth partition because there's enough space for it
        ps = BlockDev.part_get_best_free_region (self.loop_dev, BlockDev.PartType.LOGICAL, 15 * 1024**2)
        self.assertGreaterEqual(ps.start, ps3.start)
        self.assertLess(ps.start, ps5.start)

class PartGetPartByPos(PartTestCase):
    def test_get_part_by_pos(self):
        """Verify that getting partition by position works as expected"""

        ## prepare the disk with non-trivial setup first

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps)
        self.assertEqual(ps.path, self.loop_dev + "1")
        self.assertEqual(ps.type, BlockDev.PartType.NORMAL)
        self.assertEqual(ps.start, 2048 * 512)
        self.assertEqual(ps.size, 10 * 1024**2)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps.start + ps.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps2)
        self.assertEqual(ps2.path, self.loop_dev + "2")
        self.assertEqual(ps2.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps2.start - (ps.start + ps.size + 1)) < ps.start)
        self.assertEqual(ps2.size, 10 * 1024**2)
        self.assertEqual(ps2.flags, 0)  # no flags (combination of bit flags)

        ps3 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.EXTENDED, ps2.start + ps2.size + 1,
                                         35 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps3)
        self.assertEqual(ps3.path, self.loop_dev + "3")
        self.assertEqual(ps3.type, BlockDev.PartType.EXTENDED)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps3.start - (ps2.start + ps2.size + 1)) < ps.start)
        self.assertEqual(ps3.size, 35 * 1024**2)
        self.assertEqual(ps3.flags, 0)  # no flags (combination of bit flags)

        # the logical partition has number 5 even though the extended partition
        # has number 3
        ps5 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps3.start + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps5)
        self.assertEqual(ps5.path, self.loop_dev + "5")
        self.assertEqual(ps5.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps5.start < ps3.start + ps3.size)
        self.assertTrue(abs(ps5.size - 10 * 1024**2) < 2 * 1024**2)
        self.assertEqual(ps5.flags, 0)  # no flags (combination of bit flags)

        ps6 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps5.start + ps5.size + 1,
                                         10 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps6)
        self.assertEqual(ps6.path, self.loop_dev + "6")
        self.assertEqual(ps6.type, BlockDev.PartType.LOGICAL)
        # the start has to be somewhere in the extended partition p3 which
        # should need at most 2 MiB extra space
        self.assertTrue(ps3.start < ps6.start < ps3.start + ps3.size)
        self.assertEqual(ps6.size, 10 * 1024**2)
        self.assertEqual(ps6.flags, 0)  # no flags (combination of bit flags)

        ps7 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.LOGICAL, ps6.start + ps6.size + 2 * 1024**2,
                                         5 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(ps7)
        self.assertEqual(ps7.path, self.loop_dev + "7")
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
        self.assertEqual(ps4.path, self.loop_dev + "4")
        self.assertEqual(ps4.type, BlockDev.PartType.NORMAL)
        # the start has to be at most as far from the end of the previous part
        # as is the start of the first part from the start of the disk
        self.assertTrue(abs(ps4.start - (ps3.start + ps3.size + 1)) < ps.start)
        self.assertEqual(ps4.size, 10 * 1024**2)
        self.assertEqual(ps4.flags, 0)  # no flags (combination of bit flags)


        ## now try to get the partitions
        # XXX: Any way to get the extended partition (ps3)? Let's just skip it now.
        for part in (ps, ps2, ps5, ps6, ps7, ps4):
            ret = BlockDev.part_get_part_by_pos(self.loop_dev, part.start + 1 * 1024**2)
            self.assertIsNotNone(ret)
            self.assertEqual(ret.path, part.path)
            self.assertEqual(ret.start, part.start)
            self.assertEqual(ret.size, part.size)
            self.assertEqual(ret.type, part.type)
            self.assertEqual(ret.flags, part.flags)

        # free space in the extended partition
        ret = BlockDev.part_get_part_by_pos(self.loop_dev, ps3.start + 33 * 1024**2)
        self.assertIsNotNone(ret)
        self.assertIsNone(ret.path)
        self.assertTrue(ret.type & BlockDev.PartType.FREESPACE)
        self.assertTrue(ret.type & BlockDev.PartType.LOGICAL)
        # there are two 10MiB and one 5MiB logical partitions
        self.assertGreater(ret.start, ps3.start + 25 * 1024**2)
        # the size of the extended partition is 35 MiB
        self.assertLess(ret.size, 10 * 1024**2)

        # free space at the end of the disk
        ret = BlockDev.part_get_part_by_pos(self.loop_dev, 90 * 1024**2)
        self.assertIsNotNone(ret)
        self.assertIsNone(ret.path)
        self.assertTrue(ret.type & BlockDev.PartType.FREESPACE)
        self.assertEqual(ret.start, ps4.start + ps4.size)
        self.assertLessEqual(ret.size, (100 * 1024**2) - (ps4.start + ps4.size))

        # metadata at the start of the extendeded partition
        ret = BlockDev.part_get_part_by_pos(self.loop_dev, ps3.start)
        self.assertIsNotNone(ret)
        self.assertIsNone(ret.path)
        self.assertTrue(ret.type & BlockDev.PartType.LOGICAL)
        self.assertTrue(ret.type & BlockDev.PartType.METADATA)
        self.assertEqual(ret.start, ps3.start)
        self.assertEqual(ret.size, ps5.start - ps3.start)

        # metadata after a logical partition
        for ps in (ps5, ps6, ps7):
            ret = BlockDev.part_get_part_by_pos(self.loop_dev, ps.start + ps.size)
            self.assertIsNotNone(ret)
            self.assertIsNone(ret.path)
            self.assertTrue(ret.type & BlockDev.PartType.LOGICAL)
            self.assertTrue(ret.type & BlockDev.PartType.METADATA)
            self.assertEqual(ret.start, ps.start + ps.size)
            self.assertEqual(ret.size, 1024**2)

class PartCreateResizePartCase(PartTestCase):
    def test_create_resize_part_two(self):
        """Verify that it is possible to create and resize two paritions"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        ps1_half = 20 * 1024**2
        ps1_start = 2 * 1024**2

        # create a maximal second partition
        ps2 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2* ps1_half, 0, BlockDev.PartAlign.NONE)
        self.assertGreaterEqual(ps2.start, 2* ps1_half)

        # create one maximal partition in the beginning
        ps1 = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, ps1_start, 0, BlockDev.PartAlign.NONE)
        self.assertGreaterEqual(ps1.size, ps1_half)
        self.assertGreaterEqual(ps1.start, ps1_start)
        self.assertLess(ps1.size, ps1_half * 2)  # can't have full size from beginning to ps2 because of start offset

        # resizing should give the same result
        ps1_size = ps1.size
        succ = BlockDev.part_resize_part (self.loop_dev, ps1.path, 0, BlockDev.PartAlign.NONE)
        self.assertTrue(succ)
        ps1 = BlockDev.part_get_part_spec(self.loop_dev, ps1.path)
        self.assertEqual(ps1.start, ps1_start)  # offset must not be moved
        self.assertEqual(ps1.size, ps1_size)

        succ = BlockDev.part_resize_part (self.loop_dev, ps1.path, ps1_half, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(succ)
        ps1 = BlockDev.part_get_part_spec(self.loop_dev, ps1.path)
        self.assertEqual(ps1.start, ps1_start)  # offset must not be moved
        self.assertGreaterEqual(ps1.size, ps1_half)  # at least requested size
        self.assertLess(ps1.size, ps1_half + 2 * 1024**2)  # and only slightly bigger

        ps2_size = ps2.size
        ps2_start = ps2.start
        succ = BlockDev.part_resize_part (self.loop_dev, ps2.path, 0, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(succ)
        ps2 = BlockDev.part_get_part_spec(self.loop_dev, ps2.path)
        self.assertEqual(ps2.start, ps2_start)  # offset must not be moved
        self.assertGreaterEqual(ps2.size, ps2_size - 2 * 1024**2)  # almost as big as before

    def test_create_resize_part_single(self):
        """Verify that it is possible to create and resize a parition"""

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # create a maximal partition
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2 * 1024**2, 0, BlockDev.PartAlign.OPTIMAL)
        initial_start = ps.start
        initial_size = ps.size

        new_size = 20 * 1000**2  # resize to MB (not MiB) for a non-multiple of the blocksize
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, new_size, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)  # offset must not be moved
        self.assertGreaterEqual(ps.size, new_size)  # at least the requested size
        self.assertLess(ps.size, new_size + 1 * 1024**2)  # but also not too big (assuming 1 MiB alignment)

        # resize to maximum
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, 0, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)
        self.assertEqual(initial_size, ps.size)  # should grow to the same size again

        # resize to maximum explicitly
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, initial_size, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)
        self.assertGreaterEqual(ps.size, initial_size) # at least the requested size

        # resize back to 20 MB (not MiB) with no alignment
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, new_size, BlockDev.PartAlign.NONE)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)  # offset must not be moved
        self.assertGreaterEqual(ps.size, new_size)  # at least the requested size
        self.assertLess(ps.size, new_size + 4 * 1024)  # but also not too big (assuming max. 4 KiB blocks)

        # resize to maximum with no alignment
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, 0, BlockDev.PartAlign.NONE)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)
        self.assertGreaterEqual(ps.size, initial_size - 1 * 1024**2)  # libparted sometimes creates smaller partitions for no alignment
        new_size = ps.size

        # resize to maximum with no alignment explicitly
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, new_size, BlockDev.PartAlign.NONE)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)
        self.assertGreaterEqual(ps.size, new_size) # at least the requested size

        # resize to previous maximum with no alignment explicitly
        succ = BlockDev.part_resize_part (self.loop_dev, ps.path, initial_size, BlockDev.PartAlign.NONE)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec(self.loop_dev, ps.path)
        self.assertEqual(initial_start, ps.start)
        self.assertGreaterEqual(ps.size, initial_size) # at least the requested size

class PartCreateDeletePartCase(PartTestCase):
    @tag_test(TestTags.CORE)
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

        # GPT_HIDDEN is not supported on the MSDOS table
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

        # remove a flag that is not set
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY, False)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.flags, 0)

        # set read-only flag (twice to be sure that second set doesn't change it)
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)

        # set hidden and remove read-only flag
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_HIDDEN, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_READ_ONLY, False)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)

        # set no-automount flag
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.GPT_NO_AUTOMOUNT, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.GPT_READ_ONLY)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_HIDDEN)
        self.assertTrue(ps.flags & BlockDev.PartFlag.GPT_NO_AUTOMOUNT)

    def test_set_part_flag_hidden(self):
        """Verify that it is possible to set the hidden partition flag"""

        # hidden (and lba) flag is special, because the way it is set depends on
        # the filesystem (different part ID for FAT12/16/32 and NTFS) so better
        # start with a clean test environment

        # we first need a partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 100 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 100 * 1024**2, BlockDev.PartAlign.OPTIMAL)
        # we should get proper data back
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)  # no flags (combination of bit flags)

        # no filesystem -> hidden shouldn't be set
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.HIDDEN, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.flags, 0)

        # now format the partition to FAT12
        ret, out, err = run_command("mkfs.fat -F 12 %s" % ps.path)
        if ret != 0:
            self.fail("Failed to format partition to fat12 for flag test case:\n%s\n%s" % (out, err))
        self.addCleanup(run_command, "wipefs -a %s" % ps.path)

        # hidden is supported on FAT12
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.HIDDEN, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.HIDDEN)

        # remove the flag
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.HIDDEN, False)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.HIDDEN)

        # now format the partition to FAT32
        ret, out, err = run_command("mkfs.fat -F 32 %s" % ps.path)
        if ret != 0:
            self.fail("Failed to format partition to fat32 for flag test case:\n%s\n%s" % (out, err))

        # hidden is supported on FAT32
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.HIDDEN, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.HIDDEN)

        # lba is also supported on FAT32
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.LBA, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.LBA)
        self.assertTrue(ps.flags & BlockDev.PartFlag.HIDDEN)

        # unset the lba flag
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.LBA, False)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertFalse(ps.flags & BlockDev.PartFlag.LBA)
        self.assertTrue(ps.flags & BlockDev.PartFlag.HIDDEN)


class PartSetDiskFlagCase(PartTestCase):
    def test_set_disk_flag(self):
        """Verify that it is possible to set disk flag(s)"""

        with self.assertRaises(GLib.GError):
            BlockDev.part_set_disk_flag ("/non/existing/device", BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT, True)

        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)
        self.assertEqual(ps.table_type, BlockDev.PartTableType.UNDEF)
        # no label/table
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_disk_flag (self.loop_dev, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT, True)

        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.table_type, BlockDev.PartTableType.MSDOS)
        self.assertEqual(ps.flags, 0)
        # not supported on the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_disk_flag (self.loop_dev, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT, True)


        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.table_type, BlockDev.PartTableType.GPT)
        self.assertEqual(ps.flags, 0)

        succ = BlockDev.part_set_disk_flag (self.loop_dev, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT, True)
        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.flags, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT)

        # try to set the flag again, just to make sure it doesn't change
        succ = BlockDev.part_set_disk_flag (self.loop_dev, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT, True)
        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.flags, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT)

        succ = BlockDev.part_set_disk_flag (self.loop_dev, BlockDev.PartDiskFlag.PART_DISK_FLAG_GPT_PMBR_BOOT, False)
        ps = BlockDev.part_get_disk_spec (self.loop_dev)
        self.assertTrue(ps)
        self.assertEqual(ps.flags, 0)

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

        # HPSERVICE flag not supported on the MSDOS table
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_flags (self.loop_dev, ps.path, BlockDev.PartFlag.HPSERVICE)

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

class PartSetIdCase(PartTestCase):
    def test_set_part_id(self):
        """Verify that it is possible to set partition id (msdos partition type)"""

        # we first need an MBR partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.MSDOS, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # we should get proper data back
        self.assertTrue(ps)

        succ = BlockDev.part_set_part_id (self.loop_dev, ps.path, "0x8e")
        self.assertTrue(succ)
        part_id = BlockDev.part_get_part_id (self.loop_dev, ps.path)
        self.assertEqual(part_id, "0x8e")

        # we can't change part id to extended partition id
        with self.assertRaises(GLib.GError):
            BlockDev.part_set_part_id (self.loop_dev, ps.path, "0x85")


class PartSetGptFlagsCase(PartTestCase):
    def test_set_part_type(self):
        """Verify that it is possible to set and get partition flags on GPT"""

        esp_guid = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"

        # we first need a GPT partition table
        succ = BlockDev.part_create_table (self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)

        # for now, let's just create a typical primary partition starting at the
        # sector 2048, 10 MiB big with optimal alignment
        ps = BlockDev.part_create_part (self.loop_dev, BlockDev.PartTypeReq.NORMAL, 2048*512, 10 * 1024**2, BlockDev.PartAlign.OPTIMAL)

        # set GUID (part type) to check that changing flags doesn't change it
        succ = BlockDev.part_set_part_type (self.loop_dev, ps.path, esp_guid)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertEqual(ps.type_guid, esp_guid)

        # set LEGACY_BOOT flag and test it
        succ = BlockDev.part_set_part_flag (self.loop_dev, ps.path, BlockDev.PartFlag.LEGACY_BOOT, True)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.LEGACY_BOOT)
        self.assertEqual(ps.type_guid, esp_guid)

        # same but set_part_flags
        succ = BlockDev.part_set_part_flags (self.loop_dev, ps.path, BlockDev.PartFlag.LEGACY_BOOT)
        self.assertTrue(succ)
        ps = BlockDev.part_get_part_spec (self.loop_dev, ps.path)
        self.assertTrue(ps.flags & BlockDev.PartFlag.LEGACY_BOOT)
        self.assertEqual(ps.type_guid, esp_guid)

class PartNoDevCase(PartTestCase):

    def setUp(self):
        # no devices needed for this test case
        pass

    def test_part_type_str(self):
        types = {BlockDev.PartType.NORMAL: 'primary', BlockDev.PartType.LOGICAL: 'logical',
                 BlockDev.PartType.EXTENDED: 'extended', BlockDev.PartType.FREESPACE: 'free',
                 BlockDev.PartType.METADATA: 'metadata', BlockDev.PartType.PROTECTED: 'primary'}

        for key, value in types.items():
            self.assertEqual(BlockDev.part_get_type_str(key), value)
