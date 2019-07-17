import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, get_version, TestTags, tag_test
from gi.repository import BlockDev, GLib

class MpathTest(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("mpath",))

    @classmethod
    def setUpClass(cls):
        distro, _version = get_version()
        if distro == "debian":
            os.environ["LIBBLOCKDEV_SKIP_DEP_CHECKS"] = ""

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    @classmethod
    def tearDownClass(cls):
        if "LIBBLOCKDEV_SKIP_DEP_CHECKS" in os.environ.keys():
            os.environ.pop("LIBBLOCKDEV_SKIP_DEP_CHECKS")

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

class MpathUnloadTest(MpathTest):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_check_low_version(self):
        """Verify that checking the minimum dmsetup version works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/mpath_low_version/"):
            # too low version of the multipath tool available, the mpath plugin
            # should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())

    @tag_test(TestTags.NOSTORAGE)
    def test_check_no_multipath(self):
        """Verify that checking multipath tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="multipath"):
            # no multipath available, the mpath plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())

    @tag_test(TestTags.NOSTORAGE)
    def test_check_no_mpathconf(self):
        """Verify that checking mpathconf tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="mpathconf"):
            # no mpathconf available, the mpath plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("mpath", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("mpath", BlockDev.get_available_plugin_names())
