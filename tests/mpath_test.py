import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile
from gi.repository import BlockDev
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class MpathTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("mpath_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def tearDown(self):
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
