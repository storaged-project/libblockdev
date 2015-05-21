import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, fake_utils, fake_path
from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class SwapTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("swap_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def tearDown(self):
        try:
            BlockDev.swap_swapoff(self.loop_dev)
        except:
            pass

        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)

    def test_all(self):
        """Verify that swap_* functions work as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.swap_mkswap("/non/existing/device", None)

        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapon("/non/existing/device", -1)

        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapoff("/non/existing/device")

        self.assertFalse(BlockDev.swap_swapstatus("/non/existing/device"))

        # not a swap device (yet)
        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapon(self.loop_dev, -1)

        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapoff(self.loop_dev)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertFalse(on)

        # the common/expected sequence of calls
        succ = BlockDev.swap_mkswap(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.swap_swapon(self.loop_dev, -1)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertTrue(on)

        succ = BlockDev.swap_swapoff(self.loop_dev)
        self.assertTrue(succ)

        # already off
        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapoff(self.loop_dev)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertFalse(on)

    def test_mkswap_with_label(self):
        """Verify that mkswap with label works as expected"""

        succ = BlockDev.swap_mkswap(self.loop_dev, "TestBlockDevSwap")
        self.assertTrue(succ)

        os.path.exists ("/dev/disk/by-label/TestBlockDevSwap")

class SwapUnloadTest(unittest.TestCase):
    def tearDown(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.assertTrue(BlockDev.reinit(None, True, None))

    def test_check_low_version(self):
        """Verify that checking the minimum swap utils versions works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/swap_low_version/"):
            # too low version of mkswap available, the swap plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("swap", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("swap", BlockDev.get_available_plugin_names())

    def test_check_no_mkswap(self):
        """Verify that checking mkswap tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path():
            # no mkswap available, the swap plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("swap", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("swap", BlockDev.get_available_plugin_names())

    def test_check_no_swapon(self):
        """Verify that checking swapon tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path("tests/swap_no_swapon", keep_utils=["cat"]):
            # no mkswap available, the swap plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("swap", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("swap", BlockDev.get_available_plugin_names())

    def test_check_no_swapoff(self):
        """Verify that checking swapoff tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path("tests/swap_no_swapoff", keep_utils=["cat"]):
            # no mkswap available, the swap plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("swap", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("swap", BlockDev.get_available_plugin_names())
