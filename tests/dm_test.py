import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, fake_utils, fake_path
from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

class DevMapperTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("lvm_test", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

    def tearDown(self):
        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        try:
            BlockDev.dm_remove("testMap")
        except:
            pass

        os.unlink(self.dev_file)

class DevMapperCreateRemoveLinear(DevMapperTestCase):
    def test_create_remove_linear(self):
        """Verify that it is possible to create new linear mapping and remove it"""

        succ = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, None)
        self.assertTrue(succ)

        succ = BlockDev.dm_remove("testMap")
        self.assertTrue(succ)

class DevMapperMapExists(DevMapperTestCase):
    def test_map_exists(self):
        """Verify that testing if map exists works as expected"""

        succ = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, None)
        self.assertTrue(succ)

        succ = BlockDev.dm_map_exists("testMap", True, True)
        self.assertTrue(succ)

        # suspend the map
        os.system("dmsetup suspend testMap")

        # not ignoring suspended maps, should be found
        succ = BlockDev.dm_map_exists("testMap", True, False)
        self.assertTrue(succ)

        # ignoring suspended maps, should not be found
        succ = BlockDev.dm_map_exists("testMap", True, True)
        self.assertFalse(succ)

        succ = BlockDev.dm_remove("testMap")
        self.assertTrue(succ)

        # removed, should not exist even without any restrictions
        succ = BlockDev.dm_map_exists("testMap", False, False)
        self.assertFalse(succ)

class DevMapperNameNodeBijection(DevMapperTestCase):
    def test_name_node_bijection(self):
        """Verify that the map's node and map name points to each other"""

        succ = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, None)
        self.assertTrue(succ)

        self.assertEqual(BlockDev.dm_name_from_node(BlockDev.dm_node_from_name("testMap")),
                         "testMap")

        self.assertTrue(succ)

class DMUnloadTest(unittest.TestCase):
    def tearDown(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.assertTrue(BlockDev.reinit(None, True, None))

    def test_check_low_version(self):
        """Verify that checking the minimum dmsetup version works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/dm_low_version/"):
            # too low version of dmsetup available, the DM plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("dm", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("dm", BlockDev.get_available_plugin_names())

    def test_check_no_dm(self):
        """Verify that checking dmsetup tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path():
            # no dmsetup available, the DM plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(None, True, None)

            self.assertNotIn("dm", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(None, True, None))
        self.assertIn("dm", BlockDev.get_available_plugin_names())
