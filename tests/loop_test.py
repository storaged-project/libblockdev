import os
import unittest

from utils import create_sparse_tempfile
from gi.repository import BlockDev
assert BlockDev.init(None, None)[0]

class LoopTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("loop_test", 1024**3)

    def tearDown(self):
        os.unlink(self.dev_file)

    def testLoop_setup_teardown(self):
        """Verify that loop_setup and loop_teardown work as expected"""

        succ, loop, err = BlockDev.loop_setup(self.dev_file)
        self.assertTrue(succ, err)
        self.assertTrue(loop, err)
        self.assertIs(err, None)

        succ, err = BlockDev.loop_teardown(loop)
        self.assertTrue(succ)
        self.assertIs(err, None)

    def testLoop_get_loop_name(self):
        """Verify that loop_get_loop_name works as expected"""

        succ, loop, err = BlockDev.loop_setup(self.dev_file)
        ret_loop, err = BlockDev.loop_get_loop_name(self.dev_file)
        self.assertIs(err, None)
        self.assertEqual(ret_loop, loop)
        succ, err = BlockDev.loop_teardown(loop)

    def testLoop_get_backing_file(self):
        """Verify that loop_get_backing_file works as expected"""

        succ, loop, err = BlockDev.loop_setup(self.dev_file)
        f_name, err = BlockDev.loop_get_backing_file(loop)
        self.assertIs(err, None)
        self.assertEqual(f_name, self.dev_file)
        succ, err = BlockDev.loop_teardown(loop)

