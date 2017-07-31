import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, skip_on
from gi.repository import BlockDev, GLib

REQUESTED_PLUGINS = BlockDev.plugin_specs_from_names(("mpath",))

if not BlockDev.is_initialized():
    BlockDev.init(REQUESTED_PLUGINS, None)
else:
    BlockDev.reinit(REQUESTED_PLUGINS, True, None)

class MpathTestCase(unittest.TestCase):
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

class MpathUnloadTest(unittest.TestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, REQUESTED_PLUGINS, True, None)

    def test_check_low_version(self):
        """Verify that checking the minimum dmsetup version works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/mpath_low_version/"):
            # too low version of the multipath tool available, the mpath plugin
            # should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(REQUESTED_PLUGINS, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())

    def test_check_no_multipath(self):
        """Verify that checking multipath tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="multipath"):
            # no multipath available, the mpath plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(REQUESTED_PLUGINS, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())

    @skip_on("debian", reason="mpathconf is not available on Debian")
    def test_check_no_mpathconf(self):
        """Verify that checking mpathconf tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="mpathconf"):
            # no mpathconf available, the mpath plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(REQUESTED_PLUGINS, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())
