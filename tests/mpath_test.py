import unittest
import os
import overrides_hack
import shutil

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, get_version, TestTags, tag_test
from gi.repository import BlockDev, GLib

class MpathTest(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("mpath",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)


class MpathTestCase(MpathTest):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("mpath_test", 1024**3)
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

    def test_is_mpath_member(self):
        """Verify that is_mpath_member works as expected"""

        # just test that some non-mpath is not reported as a multipath member
        # device and no error is reported
        self.assertFalse(BlockDev.mpath_is_mpath_member("/dev/loop0"))

class MpathNoDevTestCase(MpathTest):
    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.MPATH), "libbd_mpath.so.3")

    @tag_test(TestTags.NOSTORAGE)
    def test_get_mpath_members(self):
        """Verify that get_mpath_members works as expected"""
        ret = BlockDev.mpath_get_mpath_members()
        self.assertIsNotNone(ret)

    @tag_test(TestTags.NOSTORAGE)
    def test_set_friendly_names(self):
        """Verify that set_friendly_names works as expected"""
        if not shutil.which('mpathconf'):
            self.skipTest("skipping The 'mpathconf' utility is not available")
        else:
            succ = BlockDev.mpath_set_friendly_names(True)
            self.assertTrue(succ)


class MpathDepsTest(MpathTest):
    @tag_test(TestTags.NOSTORAGE)
    def test_missing_dependencies(self):
        """Verify that checking for technology support works as expected"""

        with fake_utils("tests/fake_utils/mpath_low_version/"):
            with self.assertRaisesRegex(GLib.GError, "Too low version of multipath"):
                BlockDev.mpath_is_tech_avail(BlockDev.MpathTech.BASE, BlockDev.MpathTechMode.QUERY)

        with fake_path(all_but="multipath"):
            with self.assertRaisesRegex(GLib.GError, "The 'multipath' utility is not available"):
                BlockDev.mpath_is_tech_avail(BlockDev.MpathTech.BASE, BlockDev.MpathTechMode.QUERY)

        with fake_path(all_but="mpathconf"):
            # "base" should be available without mpathconf
            avail = BlockDev.mpath_is_tech_avail(BlockDev.MpathTech.BASE, BlockDev.MpathTechMode.QUERY)
            self.assertTrue(avail)

            with self.assertRaisesRegex(GLib.GError, "The 'mpathconf' utility is not available"):
                BlockDev.mpath_is_tech_avail(BlockDev.MpathTech.FRIENDLY_NAMES, BlockDev.MpathTechMode.MODIFY)
