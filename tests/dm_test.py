import unittest
import os
import overrides_hack

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path
from gi.repository import BlockDev, GLib


class DevMapperTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("dm",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("dm_test", 1024**3)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            BlockDev.dm_remove("testMap")
        except:
            pass

        try:
            delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass

        os.unlink(self.dev_file)

class DevMapperGetSubsystemFromName(DevMapperTestCase):
    def test_get_susbsytem_from_name(self):
        """Verify that it is possible to get device subsystem from its name"""

        succ = BlockDev.dm_create_linear("testMap", self.loop_dev, 100, uuid='LVM-randomjunk')
        self.assertTrue(succ)

        subsystem = BlockDev.dm_get_subsystem_from_name("testMap")
        self.assertEqual(subsystem, "LVM")

        succ = BlockDev.dm_remove("testMap")
        self.assertTrue(succ)

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

class DMUnloadTest(DevMapperTestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    def test_check_low_version(self):
        """Verify that checking the minimum dmsetup version works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_utils("tests/dm_low_version/"):
            # too low version of dmsetup available, the DM plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("dm", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("dm", BlockDev.get_available_plugin_names())

    def test_check_no_dm(self):
        """Verify that checking dmsetup tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="dmsetup"):
            # no dmsetup available, the DM plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("dm", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("dm", BlockDev.get_available_plugin_names())
