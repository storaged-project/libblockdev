import unittest
import os

from utils import create_sparse_tempfile
from gi.repository import BlockDev
assert BlockDev.init(None, None)

class SwapTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("swap_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def test_all(self):
        """Verify that swap_* functions work as expected"""

        succ = BlockDev.swap_mkswap(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.swap_swapon(self.loop_dev, -1)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertTrue(on)

        succ = BlockDev.swap_swapoff(self.loop_dev)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertFalse(on)

    def test_mkswap_with_label(self):
        """Verify that mkswap with label works as expected"""

        succ = BlockDev.swap_mkswap(self.loop_dev, "TestBlockDevSwap")
        self.assertTrue(succ)

        os.path.exists ("/dev/disk/by-label/TestBlockDevSwap")

    def tearDown(self):
        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)

