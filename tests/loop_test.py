import os
import unittest

from utils import create_sparse_tempfile
from gi.repository import BlockDev
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class LoopTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("loop_test", 1024**3)

    def tearDown(self):
        os.unlink(self.dev_file)

    def testLoop_setup_teardown(self):
        """Verify that loop_setup and loop_teardown work as expected"""

        succ, loop = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ)
        self.assertTrue(loop)

        succ = BlockDev.loop_teardown(loop)
        self.assertTrue(succ)

    def testLoop_get_loop_name(self):
        """Verify that loop_get_loop_name works as expected"""

        succ, loop = BlockDev.loop_setup(self.dev_file)
        ret_loop = BlockDev.loop_get_loop_name(self.dev_file)
        self.assertEqual(ret_loop, loop)
        BlockDev.loop_teardown(loop)

    def testLoop_get_backing_file(self):
        """Verify that loop_get_backing_file works as expected"""

        succ, loop = BlockDev.loop_setup(self.dev_file)
        f_name = BlockDev.loop_get_backing_file(loop)
        self.assertEqual(f_name, self.dev_file)
        BlockDev.loop_teardown(loop)

