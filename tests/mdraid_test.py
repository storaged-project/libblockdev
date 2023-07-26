import unittest
import os
import re
import time
from contextlib import contextmanager
import overrides_hack

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, TestTags, tag_test, run_command

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev


@contextmanager
def wait_for_action(action_name):
    try:
        yield
    finally:
        time.sleep(2)
        action = True
        while action:
            with open("/proc/mdstat", "r") as f:
                action = action_name in f.read()
            if action:
                print("Sleeping")
                time.sleep(1)

class MDTest(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("mdraid",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

class MDNoDevTestCase(MDTest):

    requested_plugins = BlockDev.plugin_specs_from_names(("mdraid",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.MDRAID), "libbd_mdraid.so.3")

    @tag_test(TestTags.NOSTORAGE)
    def test_get_superblock_size(self):
        """Verify that superblock size si calculated properly"""

        # 2 MiB for versions <= 1.0
        self.assertEqual(BlockDev.md_get_superblock_size(2 * 1024**3, "0.9"), 2 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(2 * 1024**3, "1.0"), 2 * 1024**2)

        # no version, "default" or > 1.0
        self.assertEqual(BlockDev.md_get_superblock_size(256 * 1024**3, None), 128 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(128 * 1024**3, None), 128 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(64 * 1024**3, "default"), 64 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(63 * 1024**3, "default"), 32 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(10 * 1024**3, "1.1"), 8 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(1 * 1024**3, "1.1"), 1 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(1023 * 1024**2, "1.2"), 1 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(512 * 1024**2, "1.2"), 1 * 1024**2)

        # unsupported version -> default superblock size
        self.assertEqual(BlockDev.md_get_superblock_size(257 * 1024**2, version="unknown version"),
                         2 * 1024**2)

    @tag_test(TestTags.NOSTORAGE)
    def test_canonicalize_uuid(self):
        """Verify that UUID canonicalization works as expected"""

        self.assertEqual(BlockDev.md_canonicalize_uuid("3386ff85:f5012621:4a435f06:1eb47236"),
                         "3386ff85-f501-2621-4a43-5f061eb47236")

        with self.assertRaisesRegex(GLib.GError, r'malformed or invalid'):
            BlockDev.md_canonicalize_uuid("malformed-uuid-example")

    @tag_test(TestTags.NOSTORAGE)
    def test_get_md_uuid(self):
        """Verify that getting UUID in MD RAID format works as expected"""

        self.assertEqual(BlockDev.md_get_md_uuid("3386ff85-f501-2621-4a43-5f061eb47236"),
                         "3386ff85:f5012621:4a435f06:1eb47236")

        with self.assertRaisesRegex(GLib.GError, r'malformed or invalid'):
            BlockDev.md_get_md_uuid("malformed-uuid-example")

class MDTestCase(MDTest):

    _sparse_size = 10 * 1024**2

    def setUp(self):
        if os.uname()[-1] == "i686":
            self.skipTest("Skipping hanging MD RAID tests on i686")

        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("md_test", self._sparse_size)
        self.dev_file2 = create_sparse_tempfile("md_test", self._sparse_size)
        self.dev_file3 = create_sparse_tempfile("md_test", self._sparse_size)

        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev2 = create_lio_device(self.dev_file2)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev3 = create_lio_device(self.dev_file3)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            BlockDev.md_deactivate("bd_test_md")
        except:
            pass
        try:
            BlockDev.md_deactivate(BlockDev.md_node_from_name("bd_test_md"))
        except:
            pass
        try:
            BlockDev.md_destroy(self.loop_dev)
        except:
            pass
        try:
            BlockDev.md_destroy(self.loop_dev2)
        except:
            pass
        try:
            BlockDev.md_destroy(self.loop_dev3)
        except:
            pass
        try:
            BlockDev.md_deactivate("bd_test_md")
        except:
            pass
        try:
            BlockDev.md_deactivate(BlockDev.md_node_from_name("bd_test_md"))
        except:
            pass

        try:
            delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)

        try:
            delete_lio_device(self.loop_dev2)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file2)

        try:
            delete_lio_device(self.loop_dev3)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file3)


class MDTestCreateDeactivateDestroy(MDTestCase):
    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_create_deactivate_destroy(self):
        """Verify that it is possible to create, deactivate and destroy an MD RAID"""

        with self.assertRaises(GLib.GError):
            BlockDev.md_create("bd_test_md2", "raid1",
                               ["/non/existing/device", self.loop_dev2],
                               1, None, None)

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        # newly created array should be 'clean'
        state = BlockDev.md_get_status("bd_test_md")
        self.assertEqual(state, "clean")

        succ = BlockDev.md_deactivate("bd_test_md")
        self.assertTrue(succ)

        succ = BlockDev.md_destroy(self.loop_dev)
        self.assertTrue(succ)
        succ = BlockDev.md_destroy(self.loop_dev2)
        self.assertTrue(succ)
        succ = BlockDev.md_destroy(self.loop_dev3)
        self.assertTrue(succ)

class MDTestCreateWithChunkSize(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_create_with_chunk_size(self):
        """Verify that it is possible to create and MD RAID with specific chunk size """

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid0",
                                      [self.loop_dev, self.loop_dev2],
                                      0, None, None, 512 * 1024)
            self.assertTrue(succ)

        ex_data = BlockDev.md_examine(self.loop_dev)
        self.assertEqual(ex_data.chunk_size, 512 * 1024)

        succ = BlockDev.md_deactivate("bd_test_md")
        self.assertTrue(succ)

        succ = BlockDev.md_destroy(self.loop_dev)
        self.assertTrue(succ)
        succ = BlockDev.md_destroy(self.loop_dev2)
        self.assertTrue(succ)
        succ = BlockDev.md_destroy(self.loop_dev3)
        self.assertTrue(succ)

class MDTestActivateDeactivate(MDTestCase):
    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_activate_deactivate(self):
        """Verify that it is possible to activate and deactivate an MD RAID"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.md_deactivate("non_existing_md")

        with wait_for_action("resync"):
            succ = BlockDev.md_deactivate("bd_test_md")
            self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.md_activate("bd_test_md",
                                 ["/non/existing/device", self.loop_dev2, self.loop_dev3], None)

        with wait_for_action("resync"):
            succ = BlockDev.md_activate("bd_test_md",
                                        [self.loop_dev, self.loop_dev2, self.loop_dev3], None)
            self.assertTrue(succ)

        # try to activate again, should not fail, just no-op
        succ = BlockDev.md_activate("bd_test_md",
                                    [self.loop_dev, self.loop_dev2, self.loop_dev3], None)
        self.assertTrue(succ)

        # try to deactivate using the node instead of name
        with wait_for_action("resync"):
            succ = BlockDev.md_deactivate(BlockDev.md_node_from_name("bd_test_md"))
            self.assertTrue(succ)

        # try to activate using full path, not just the name
        # (it should work too and blivet does this)
        with wait_for_action("resync"):
            succ = BlockDev.md_activate("/dev/md/bd_test_md",
                                        [self.loop_dev, self.loop_dev2, self.loop_dev3], None)
            self.assertTrue(succ)

class MDTestActivateWithUUID(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_activate_with_uuid(self):
        """Verify that it is possible to activate an MD RAID with UUID"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_deactivate("bd_test_md")
            self.assertTrue(succ)

        md_info = BlockDev.md_examine(self.loop_dev)
        self.assertTrue(md_info)
        self.assertTrue(md_info.uuid)

        with wait_for_action("resync"):
            succ = BlockDev.md_activate("bd_test_md", [self.loop_dev, self.loop_dev2, self.loop_dev3], md_info.uuid)

class MDTestActivateByUUID(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_activate_by_uuid(self):
        """Verify that it is possible to activate an MD RAID by UUID"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_deactivate("bd_test_md")
            self.assertTrue(succ)

        md_info = BlockDev.md_examine(self.loop_dev)
        self.assertTrue(md_info)
        self.assertTrue(md_info.uuid)

        # should work with member devices specified
        with wait_for_action("resync"):
            succ = BlockDev.md_activate(None, [self.loop_dev, self.loop_dev2, self.loop_dev3], md_info.uuid)

        with wait_for_action("resync"):
            succ = BlockDev.md_deactivate("bd_test_md")
            self.assertTrue(succ)

        # as well as without them
        with wait_for_action("resync"):
            succ = BlockDev.md_activate(None, None, md_info.uuid)


class MDTestNominateDenominate(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_nominate_denominate(self):
        """Verify that it is possible to nominate and denominate an MD RAID device"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_denominate(self.loop_dev)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_nominate(self.loop_dev)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_denominate(self.loop_dev)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_nominate(self.loop_dev)
            self.assertTrue(succ)

        with wait_for_action("resync"):
            succ = BlockDev.md_deactivate(BlockDev.md_node_from_name("bd_test_md"))
            self.assertTrue(succ)


class MDTestAddRemove(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_add_remove(self):
        """Verify that it is possible to add a device to and remove from an MD RAID"""

        # the MD array doesn't exist yet
        with self.assertRaises(GLib.GError):
            BlockDev.md_add("bd_test_md", self.loop_dev3, 0, None)

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2],
                                      0, None, None)
            self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.md_add("bd_test_md", "/non/existing/device", 0, None)

        # add the device as a spare
        succ = BlockDev.md_add("bd_test_md", self.loop_dev3, 0, None)
        self.assertTrue(succ)

        md_info = BlockDev.md_detail("bd_test_md")
        self.assertEqual(md_info.raid_devices, 2)
        self.assertEqual(md_info.spare_devices, 1)

        with self.assertRaises(GLib.GError):
            BlockDev.md_add("bd_test_md", self.loop_dev3, 0, None)

        # now remove the spare device (should be possible without --fail)
        with wait_for_action("resync"):
            succ = BlockDev.md_remove("bd_test_md", self.loop_dev3, False, None)
            self.assertTrue(succ)

        md_info = BlockDev.md_detail("bd_test_md")
        self.assertEqual(md_info.raid_devices, 2)
        self.assertEqual(md_info.spare_devices, 0)

        # remove one of the original devices (with --fail enabled)
        with wait_for_action("resync"):
            succ = BlockDev.md_remove("bd_test_md", self.loop_dev2, True, None)
            self.assertTrue(succ)

        md_info = BlockDev.md_detail("bd_test_md")
        self.assertEqual(md_info.raid_devices, 2)
        self.assertEqual(md_info.active_devices, 1)
        self.assertEqual(md_info.spare_devices, 0)

        # now try to add it back -- it should be re-added automatically as
        # a RAID device, not a spare device
        with wait_for_action("recovery"):
            succ = BlockDev.md_add("bd_test_md", self.loop_dev2, 0, None)
            self.assertTrue(succ)

        md_info = BlockDev.md_detail("bd_test_md")
        self.assertEqual(md_info.raid_devices, 2)
        self.assertEqual(md_info.active_devices, 2)
        self.assertEqual(md_info.spare_devices, 0)

class MDTestExamineDetail(MDTestCase):
    # sleeps to let MD RAID sync things
    @tag_test(TestTags.SLOW)
    def test_examine_detail(self):
        """Verify that it is possible to get info about an MD RAID"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        ex_data = BlockDev.md_examine(self.loop_dev)
        # test that we got something
        self.assertTrue(ex_data)

        # verify some known data
        self.assertEqual(ex_data.device, "/dev/md/bd_test_md")
        self.assertEqual(ex_data.level, "raid1")
        self.assertEqual(ex_data.num_devices, 2)
        self.assertTrue(ex_data.name.endswith("bd_test_md"))
        self.assertEqual(len(ex_data.metadata), 3)
        self.assertLess(ex_data.size, (10 * 1024**2))
        self.assertTrue(re.match(r'[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}', ex_data.uuid))

        de_data = BlockDev.md_detail("bd_test_md")
        # test that we got something
        self.assertTrue(de_data)

        # verify some known data
        self.assertEqual(de_data.device, "/dev/md/bd_test_md")
        self.assertTrue(de_data.name.endswith("bd_test_md"))
        self.assertEqual(len(de_data.metadata), 3)
        self.assertEqual(de_data.level, "raid1")
        self.assertEqual(de_data.raid_devices, 2)
        self.assertEqual(de_data.total_devices, 3)
        self.assertEqual(de_data.spare_devices, 1)
        self.assertLess(de_data.array_size, (10 * 1024**2))
        self.assertLess(de_data.use_dev_size, (10 * 1024**2))
        if "JENKINS_HOME" not in os.environ:
            # XXX: for some reason the RAID is in "active sync" when tests run in
            # Jenkins
            self.assertTrue(de_data.clean)
        self.assertTrue(re.match(r'[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}', de_data.uuid))

        self.assertEqual(ex_data.uuid, de_data.uuid)

        # try to get detail data with some different raid specification
        node = BlockDev.md_node_from_name("bd_test_md")

        de_data = BlockDev.md_detail("/dev/md/bd_test_md")
        self.assertTrue(de_data)
        de_data = BlockDev.md_detail(node)
        self.assertTrue(de_data)
        de_data = BlockDev.md_detail("/dev/%s" % node)
        self.assertTrue(de_data)

class MDTestNameNodeBijection(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_name_node_bijection(self):
        """Verify that MD RAID node and name match each other"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        node = BlockDev.md_node_from_name("bd_test_md")
        self.assertEqual(BlockDev.md_name_from_node(node), "bd_test_md")
        self.assertEqual(BlockDev.md_name_from_node("/dev/" + node), "bd_test_md")

        with self.assertRaises(GLib.GError):
            node = BlockDev.md_node_from_name("made_up_md")

        with self.assertRaisesRegex(GLib.GError, r'No name'):
            BlockDev.md_name_from_node("no_such_node")

        succ = BlockDev.md_deactivate("bd_test_md");
        self.assertTrue(succ)

        succ = BlockDev.md_destroy(self.loop_dev)
        self.assertTrue(succ)
        succ = BlockDev.md_destroy(self.loop_dev2)
        self.assertTrue(succ)
        succ = BlockDev.md_destroy(self.loop_dev3)
        self.assertTrue(succ)

class MDTestSetBitmapLocation(MDTestCase):
    @tag_test(TestTags.SLOW, TestTags.UNSTABLE)
    def test_set_bitmap_location(self):
        """Verify we can change bitmap location for an existing MD array"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, "none")
            self.assertTrue(succ)

        loc = BlockDev.md_get_bitmap_location("bd_test_md")
        self.assertEqual(loc, "none")

        BlockDev.md_deactivate("bd_test_md")

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, "internal")
            self.assertTrue(succ)

        loc = BlockDev.md_get_bitmap_location("bd_test_md")
        self.assertEqual(loc, "+8")

        succ = BlockDev.md_set_bitmap_location("bd_test_md", "none")
        self.assertTrue(succ)

        loc = BlockDev.md_get_bitmap_location("bd_test_md")
        self.assertEqual(loc, "none")

        succ = BlockDev.md_set_bitmap_location("bd_test_md", "internal")
        self.assertTrue(succ)

        loc = BlockDev.md_get_bitmap_location("bd_test_md")
        self.assertEqual(loc, "+8")

        # test some different name specifications
        # (need to switch between internal and none because setting the same
        # location multiple times results in an error)
        succ = BlockDev.md_set_bitmap_location("/dev/md/bd_test_md", "none")
        self.assertTrue(succ)

        node = BlockDev.md_node_from_name("bd_test_md")
        self.assertIsNotNone(node)

        succ = BlockDev.md_set_bitmap_location(node, "internal")
        self.assertTrue(succ)

        succ = BlockDev.md_set_bitmap_location("/dev/%s" % node, "none")
        self.assertTrue(succ)

        # get_bitmap_location should accept name, node or path
        loc = BlockDev.md_get_bitmap_location(node)
        self.assertEqual(loc, "none")

        loc = BlockDev.md_get_bitmap_location("/dev/%s" % node)
        self.assertEqual(loc, "none")

        loc = BlockDev.md_get_bitmap_location("/dev/md/bd_test_md")
        self.assertEqual(loc, "none")


class MDTestRequestSyncAction(MDTestCase):
    @tag_test(TestTags.SLOW)
    def test_request_sync_action(self):
        """Verify we can request sync action on an existing MD array"""

        with wait_for_action("resync"):
            succ = BlockDev.md_create("bd_test_md", "raid1",
                                      [self.loop_dev, self.loop_dev2, self.loop_dev3],
                                      1, None, None)
            self.assertTrue(succ)

        with wait_for_action("check"):
            succ = BlockDev.md_request_sync_action("bd_test_md", "check")

        node = BlockDev.md_node_from_name("bd_test_md")
        with open("/sys/block/%s/md/last_sync_action" % node) as f:
            action = f.read().strip()
        self.assertEqual(action, "check")


class MDTestDDFRAID(MDTestCase):

    _sparse_size = 50 * 1024**2

    def _clean_up(self):
        try:
            BlockDev.md_deactivate("bd_test_ddf")
        except:
            pass
        try:
            BlockDev.md_deactivate(BlockDev.md_node_from_name("bd_test_ddf"))
        except:
            pass

        super(MDTestDDFRAID, self)._clean_up()

    def test_examine_ddf_container(self):
        succ = BlockDev.md_create("bd_test_md", "container",
                                  [self.loop_dev, self.loop_dev2],
                                  0, "ddf", None)
        self.assertTrue(succ)

        # we cannot create the array with libblockdev because we cannot pass the --raid-devices option
        ret, _out, err = run_command("mdadm --create /dev/md/bd_test_ddf --run --level=raid0 --raid-devices=2 /dev/md/bd_test_md")
        self.assertEqual(ret, 0, msg="Failed to create RAID for DDF test: %s" % err)

        edata = BlockDev.md_examine(self.loop_dev)
        self.assertIsNotNone(edata)
        self.assertIsNotNone(edata.uuid)
        self.assertEqual(edata.level, "container")
        self.assertEqual(edata.metadata, "ddf")

        # ddf container detail
        ddata = BlockDev.md_detail("bd_test_md")
        self.assertIsNotNone(ddata)
        self.assertIsNotNone(ddata.uuid)
        self.assertEqual(ddata.uuid, edata.uuid)
        self.assertEqual(ddata.level, "container")
        self.assertEqual(ddata.metadata, "ddf")

        # array detail
        ddata = BlockDev.md_detail("bd_test_ddf")
        self.assertIsNotNone(ddata)
        self.assertEqual(ddata.level, "raid0")
        self.assertEqual(ddata.container, "/dev/md/bd_test_md")


class FakeMDADMutilTest(MDTest):

    def setUp(self):
        super().setUp()

        # we need to force the check now to make sure that libblockdev doesn't use
        # the fake tools below to try to get mdadm version
        BlockDev.md_is_tech_avail(BlockDev.MDTech.MDRAID, 0)

    # no setUp nor tearDown needed, we are gonna use fake utils
    @tag_test(TestTags.NOSTORAGE)
    def test_fw_raid_uppercase_examine(self):
        """Verify that md_examine works with output using "RAID" instead of "Raid" and other quirks """

        with fake_utils("tests/fake_utils/mdadm_fw_RAID_examine"):
            ex_data = BlockDev.md_examine("fake_dev")

        self.assertEqual(ex_data.level, "container")
        self.assertEqual(ex_data.num_devices, 1)
        self.assertEqual(ex_data.uuid, "b42756a2-37e4-3e47-674b-d1dd6e822145")
        self.assertEqual(ex_data.device, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_no_metadata_examine(self):
        """Verify that md_examine works as expected with no metadata spec"""

        # shouldn't raise any exception
        with fake_utils("tests/fake_utils/mdadm_no_metadata_examine"):
            ex_data = BlockDev.md_examine("fake_dev")

        self.assertIs(ex_data.metadata, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_fw_raid_migrating(self):
        """Verify that md_examine works when array is migrating ("foo <-- bar" values in output) """

        with fake_utils("tests/fake_utils/mdadm_fw_RAID_examine_migrate"):
            ex_data = BlockDev.md_examine("fake_dev")

        self.assertEqual(ex_data.chunk_size, 128 * 1024)

    @tag_test(TestTags.NOSTORAGE)
    def test_mdadm_name_extra_info(self):
        """Verify that md_examine and md_detail work with extra MD RAID name info"""

        with fake_utils("tests/fake_utils/mdadm_extra_name_stuff"):
            ex_data = BlockDev.md_examine("fake_dev")
            detail_data = BlockDev.md_detail("fake_dev")

        self.assertEqual(ex_data.name, "localhost:fedora")
        self.assertEqual(detail_data.name, "localhost:fedora")


class MDDepsTest(MDTest):
    @tag_test(TestTags.NOSTORAGE)
    def test_missing_dependencies(self):
        """Verify that checking for technology support works as expected"""

        with fake_utils("tests/fake_utils/mdraid_low_version/"):
            # too low version of mdsetup available
            with self.assertRaisesRegex(GLib.GError, "Too low version of mdadm"):
                BlockDev.md_is_tech_avail(BlockDev.MDTech.MDRAID, 0)

        with fake_path(all_but="mdadm"):
            # no mdadm available
            with self.assertRaisesRegex(GLib.GError, "The 'mdadm' utility is not available"):
                BlockDev.md_is_tech_avail(BlockDev.MDTech.MDRAID, 0)
