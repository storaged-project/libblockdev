from __future__ import print_function

import os
import yaml
import unittest
import overrides_hack
import six

from utils import run_command, read_file, fake_path, create_sparse_tempfile, create_lio_device, delete_lio_device, TestTags, tag_test
from gi.repository import BlockDev, GLib
from bytesize import bytesize
from distutils.spawn import find_executable


class VDOTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("vdo","part",))
    loop_size = 8 * 1024**3

    @classmethod
    def setUpClass(cls):

        if not BlockDev.utils_have_kernel_module("kvdo"):
            raise unittest.SkipTest("VDO kernel module not available, skipping.")

        try:
            BlockDev.utils_load_kernel_module("kvdo")
        except GLib.GError as e:
            if "File exists" not in e.message:
                raise unittest.SkipTest("cannot load VDO kernel module, skipping.")

        if not find_executable("vdo"):
            raise unittest.SkipTest("vdo executable not foundin $PATH, skipping.")

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("vdo_test", self.loop_size)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)


class VDOTest(VDOTestCase):

    vdo_name = "bd-test-vdo"

    def _remove_vdo(self, name):
        run_command("vdo remove --force -n %s" % name)

    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_create_remove(self):
        """Verify that it is possible to create and remove a VDO volume"""

        with self.assertRaises(GLib.GError):
            BlockDev.vdo_create(self.vdo_name, '/non/existing')

        with self.assertRaises(GLib.GError):
            BlockDev.vdo_create(self.vdo_name, self.loop_dev, write_policy=BlockDev.VDOWritePolicy.UNKNOWN)

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev, 3 * self.loop_size, 0,
                                  True, True, BlockDev.VDOWritePolicy.AUTO)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        self.assertTrue(os.path.exists("/dev/mapper/%s" % self.vdo_name))

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertEqual(info.name, self.vdo_name)
        self.assertEqual(info.device, self.loop_dev)
        self.assertTrue(info.deduplication)
        self.assertTrue(info.compression)
        self.assertEqual(info.logical_size, 3 * self.loop_size)
        self.assertEqual(info.write_policy, BlockDev.VDOWritePolicy.AUTO)

        ret = BlockDev.vdo_remove(self.vdo_name, True)
        self.assertTrue(ret)

        self.assertFalse(os.path.exists("/dev/mapper/%s" % self.vdo_name))

    @tag_test(TestTags.SLOW)
    def test_enable_disable_compression(self):
        """Verify that it is possible to enable/disable compression on an existing VDO volume"""

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev, 3 * self.loop_size, 0,
                                  True, True, BlockDev.VDOWritePolicy.AUTO)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertTrue(info.compression)

        # now disable the compression
        ret = BlockDev.vdo_disable_compression(self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertFalse(info.compression)

        # and enable it again
        ret = BlockDev.vdo_enable_compression(self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertTrue(info.compression)

    @tag_test(TestTags.SLOW)
    def test_enable_disable_deduplication(self):
        """Verify that it is possible to enable/disable deduplication on an existing VDO volume"""

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev, 3 * self.loop_size, 0,
                                  True, True, BlockDev.VDOWritePolicy.AUTO)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertTrue(info.deduplication)

        # now disable the deduplication
        ret = BlockDev.vdo_disable_deduplication(self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertFalse(info.deduplication)

        # and enable it again
        ret = BlockDev.vdo_enable_deduplication(self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertTrue(info.deduplication)

    @tag_test(TestTags.SLOW)
    def test_activate_deactivate(self):
        """Verify that it is possible to activate/deactivate an existing VDO volume"""

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev, 3 * self.loop_size, 0,
                                  True, True, BlockDev.VDOWritePolicy.AUTO)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertTrue(info.active)

        # now disable the volume
        ret = BlockDev.vdo_deactivate(self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertFalse(info.active)

        # and stop it too
        ret = BlockDev.vdo_stop(self.vdo_name)
        self.assertTrue(ret)

        self.assertFalse(os.path.exists("/dev/mapper/%s" % self.vdo_name))

        # enable it again
        ret = BlockDev.vdo_activate(self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertTrue(info.active)

        # and start it
        ret = BlockDev.vdo_start(self.vdo_name)
        self.assertTrue(ret)

        self.assertTrue(os.path.exists("/dev/mapper/%s" % self.vdo_name))

    @tag_test(TestTags.SLOW)
    def test_change_write_policy(self):

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev, 3 * self.loop_size, 0,
                                  True, True, BlockDev.VDOWritePolicy.AUTO)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        with self.assertRaises(GLib.GError):
            BlockDev.vdo_change_write_policy("definitely-not-a-vdo", BlockDev.VDOWritePolicy.SYNC)

        with self.assertRaises(GLib.GError):
            BlockDev.vdo_change_write_policy(self.vdo_name, BlockDev.VDOWritePolicy.UNKNOWN)

        ret = BlockDev.vdo_change_write_policy(self.vdo_name, BlockDev.VDOWritePolicy.SYNC)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertEqual(info.write_policy, BlockDev.VDOWritePolicy.SYNC)

    def _get_vdo_info(self, name):
        ret, out, _err = run_command("vdo status -n %s" % name)
        if ret != 0 or not out:
            return None

        info = yaml.load(out, Loader=yaml.SafeLoader)
        if "VDOs" not in info.keys() or name not in info["VDOs"].keys():
            print("Failed to parse output of 'vdo status'")
            return None

        return info["VDOs"][name]

    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_get_info(self):
        """Verify that it is possible to get information about an existing VDO volume"""

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        bd_info = BlockDev.vdo_info(self.vdo_name)
        self.assertIsNotNone(bd_info)

        sys_info = self._get_vdo_info(self.vdo_name)
        self.assertIsNotNone(sys_info)

        self.assertEqual(bd_info.deduplication, sys_info["Deduplication"] == "enabled")
        self.assertEqual(bd_info.deduplication, sys_info["Compression"] == "enabled")

        self.assertEqual(BlockDev.vdo_get_write_policy_str(bd_info.write_policy),
                         sys_info["Configured write policy"])

        # index memory is printed in gigabytes without the unit
        self.assertEqual(bd_info.index_memory, sys_info["Index memory setting"] * 1000**3)

        # logical and physical size are printed with units
        self.assertEqual(bd_info.physical_size, bytesize.Size(sys_info["Physical size"]))
        self.assertEqual(bd_info.logical_size, bytesize.Size(sys_info["Logical size"]))

    @tag_test(TestTags.SLOW)
    def test_grow_logical(self):
        """Verify that it is possible to grow logical size of an existing VDO volume"""

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertIsNotNone(info)

        new_size = info.logical_size * 2

        ret = BlockDev.vdo_grow_logical(self.vdo_name, new_size)
        self.assertTrue(ret)

        info = BlockDev.vdo_info(self.vdo_name)
        self.assertIsNotNone(info)

        self.assertEqual(info.logical_size, new_size)

    @tag_test(TestTags.SLOW, TestTags.UNSTABLE)
    def test_grow_physical(self):
        """Verify that it is possible to grow physical size of an existing VDO volume"""

        # create a partition that we can grow later
        succ = BlockDev.part_create_table(self.loop_dev, BlockDev.PartTableType.GPT, True)
        self.assertTrue(succ)
        part_spec = BlockDev.part_create_part(self.loop_dev,BlockDev.PartTypeReq.NORMAL, 2048, 5.1 * 1024**3, BlockDev.PartAlign.OPTIMAL)
        self.assertIsNotNone(part_spec)

        vdo_part_dev = self.loop_dev + '1'
        self.assertTrue(os.path.exists(vdo_part_dev))

        ret = BlockDev.vdo_create(self.vdo_name, vdo_part_dev)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        info_before = BlockDev.vdo_info(self.vdo_name)
        self.assertIsNotNone(info_before)

        # grow the partition
        succ = BlockDev.part_resize_part(self.loop_dev, vdo_part_dev, 0, BlockDev.PartAlign.OPTIMAL)
        self.assertTrue(succ)
        info_after = BlockDev.vdo_info(self.vdo_name)
        self.assertIsNotNone(info_after)
        self.assertEqual(info_before.logical_size, info_after.logical_size)
        self.assertEqual(info_before.physical_size, info_after.physical_size)

        # perform the real grow and get new sizes
        succ = BlockDev.vdo_grow_physical(self.vdo_name)
        self.assertTrue(succ)
        info_after = BlockDev.vdo_info(self.vdo_name)
        self.assertIsNotNone(info_after)
        self.assertEqual(info_before.logical_size, info_after.logical_size)
        self.assertGreater(info_after.physical_size, info_before.physical_size)

    @tag_test(TestTags.SLOW)
    def test_statistics(self):
        """Verify that it is possible to retrieve statistics of an existing VDO volume"""

        ret = BlockDev.vdo_create(self.vdo_name, self.loop_dev)
        self.addCleanup(self._remove_vdo, self.vdo_name)
        self.assertTrue(ret)

        with six.assertRaisesRegex(self, GLib.GError, "No such file or directory"):
            stats = BlockDev.vdo_get_stats("nonexistingxxx")

        stats = BlockDev.vdo_get_stats(self.vdo_name)
        self.assertIsNotNone(stats)
        # assuming block_size is always greater than zero
        self.assertGreater(stats.block_size, 0)

        stats = BlockDev.vdo_get_stats_full(self.vdo_name)
        self.assertIsNotNone(stats)
        self.assertGreater(len(stats), 0)
        self.assertIn("block_size", stats)


class VDOUnloadTest(VDOTestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_check_no_vdo(self):
        """Verify that checking vdo tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="vdo"):
            # no vdo tool available, the VDO plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("vdo", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("vdo", BlockDev.get_available_plugin_names())
