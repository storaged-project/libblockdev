import unittest
import os

from utils import create_sparse_tempfile
from gi.repository import BlockDev
BlockDev.init(None)

class SwapTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("libblockdev_create_swap_test", 1024**3)
        succ, loop, err = BlockDev.loop_setup(self.dev_file)
        if err or not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def test_all(self):
        """Verify that swap_* functions work as expected"""

        succ, err = BlockDev.swap_mkswap(self.loop_dev, None)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.swap_swapon(self.loop_dev, -1)
        self.assertTrue(succ)
        self.assertIs(err, None)

        on, err = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertTrue(on)
        self.assertIs(err, None)

        succ, err = BlockDev.swap_swapoff(self.loop_dev)
        self.assertTrue(succ)
        self.assertIs(err, None)

        on, err = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertFalse(on)
        self.assertIs(err, None)

    def test_mkswap_with_label(self):
        """Verify that mkswap with label works as expected"""

        succ, err = BlockDev.swap_mkswap(self.loop_dev, "TestBlockDevSwap")
        self.assertTrue(succ)
        self.assertIs(err, None)

        os.path.exists ("/dev/disk/by-label/TestBlockDevSwap")

    def tearDown(self):
        succ, err = BlockDev.loop_teardown(self.loop_dev)
        if err or not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

