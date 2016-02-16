import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, fake_utils, fake_path
from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class MpathTestCase(unittest.TestCase):
    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("mpath_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def _clean_up(self):
        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)

    def test_is_mpath_member(self):
        """Verify that is_mpath_member works as expected"""

        # just test that some non-mpath is not reported as a multipath member
        # device and no error is reported
        self.assertFalse(BlockDev.mpath_is_mpath_member("/dev/loop0"))

class MpathUnloadTest(unittest.TestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, None, True, None)

    def test_check_low_version(self):
        """Verify that checking the minimum dmsetup version works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/mpath_low_version/"):
            # too low version of the multipath tool available, the mpath plugin
            # should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())

    def test_check_no_multipath(self):
        """Verify that checking multipath tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path():
            # no multipath available, the mpath plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())

    def test_check_no_mpathconf(self):
        """Verify that checking mpathconf tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path("tests/mpath_no_mpathconf", keep_utils=["cat"]):
            # no mpathconf available, the mpath plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())
