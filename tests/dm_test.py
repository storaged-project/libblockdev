import unittest
import os

from utils import create_sparse_tempfile
from gi.repository import BlockDev
BlockDev.init(None)

class DevMapperTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("lvm_test", 1024**3)
        succ, loop, err = BlockDev.loop_setup(self.dev_file)
        if err or not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def tearDown(self):
        succ, err = BlockDev.loop_teardown(self.loop_dev)
        if err or not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)

    def test_create_remove_linear(self):
        """Verify that it is possible to create new linear mapping and remove it"""

        succ, err = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, None)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.dm_remove("testMap")
        self.assertTrue(succ)
        self.assertIs(err, None)

    def test_map_exists(self):
        """Verify that testing if map exists works as expected"""

        succ, err = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, None)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.dm_map_exists("testMap", True, True)
        self.assertTrue(succ)
        self.assertIs(err, None)

        # suspend the map
        os.system("dmsetup suspend testMap")

        # not ignoring suspended maps, should be found
        succ, err = BlockDev.dm_map_exists("testMap", True, False)
        self.assertTrue(succ)
        self.assertIs(err, None)

        # ignoring suspended maps, should not be found
        succ, err = BlockDev.dm_map_exists("testMap", True, True)
        self.assertFalse(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.dm_remove("testMap")
        self.assertTrue(succ)
        self.assertIs(err, None)

        # removed, should not exist even without any restrictions
        succ, err = BlockDev.dm_map_exists("testMap", False, False)
        self.assertFalse(succ)
        self.assertIs(err, None)

    def test_name_node_bijection(self):
        """Verify that the map's node and map name points to each other"""

        succ, err = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, None)
        self.assertTrue(succ)
        self.assertIs(err, None)

        self.assertEqual(BlockDev.dm_name_from_node(BlockDev.dm_node_from_name("testMap")[0])[0],
                         "testMap")

        succ, err = BlockDev.dm_remove("testMap")
        self.assertTrue(succ)
        self.assertIs(err, None)
