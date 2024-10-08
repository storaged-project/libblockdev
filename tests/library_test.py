import os
import unittest
import re
import overrides_hack
from utils import fake_path, TestTags, tag_test, required_plugins

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev


@required_plugins(("crypto","dm", "loop", "mdraid", "part", "swap"))
class LibraryOpsTestCase(unittest.TestCase):
    log = ""

    # all plugins except for 'btrfs', 'fs' and 'mpath' -- these don't have all
    # the dependencies on CentOS/Debian and we don't need them for this test
    requested_plugins = BlockDev.plugin_specs_from_names(("crypto", "dm",
                                                          "loop", "mdraid",
                                                          "part", "swap"))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def my_log_func(self, level, msg):
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    @tag_test(TestTags.CORE)
    def test_logging_setup(self):
        """Verify that setting up logging works as expected"""

        self.assertTrue(BlockDev.reinit(self.requested_plugins, False, self.my_log_func))

        # we are checking for info log messages and default level is warning
        BlockDev.utils_set_log_level(BlockDev.UTILS_LOG_INFO)

        succ = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)

        # reinit with no logging function should change nothing about logging
        self.assertTrue(BlockDev.reinit(self.requested_plugins, False, None))

        succ, out = BlockDev.utils_exec_and_capture_output(["echo", "hi"])
        self.assertTrue(succ)
        self.assertEqual(out, "hi\n")

        match = re.search(r'Running \[(\d+)\] true', self.log)
        self.assertIsNot(match, None)
        task_id1 = match.group(1)
        match = re.search(r'Running \[(\d+)\] echo hi', self.log)
        self.assertIsNot(match, None)
        task_id2 = match.group(1)

        self.assertIn("...done [%s] (exit code: 0)" % task_id1, self.log)
        self.assertIn("stdout[%s]:" % task_id1, self.log)
        self.assertIn("stderr[%s]:" % task_id1, self.log)

        self.assertIn("stdout[%s]: hi" % task_id2, self.log)
        self.assertIn("stderr[%s]:" % task_id2, self.log)
        self.assertIn("...done [%s] (exit code: 0)" % task_id2, self.log)

    @tag_test(TestTags.CORE)
    def test_require_plugins(self):
        """Verify that loading only required plugins works as expected"""

        ps = BlockDev.PluginSpec(name=BlockDev.Plugin.SWAP, so_name="")
        self.assertTrue(BlockDev.reinit([ps], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), ["swap"])
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

    @tag_test(TestTags.CORE)
    def test_not_implemented(self):
        """Verify that unloaded/unimplemented functions report errors"""

        # should be loaded and working
        self.assertTrue(BlockDev.md_canonicalize_uuid("3386ff85:f5012621:4a435f06:1eb47236"))

        ps = BlockDev.PluginSpec(name=BlockDev.Plugin.SWAP, so_name="")
        self.assertTrue(BlockDev.reinit([ps], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), ["swap"])

        # no longer loaded
        with self.assertRaises(GLib.GError):
            BlockDev.md_canonicalize_uuid("3386ff85:f5012621:4a435f06:1eb47236")

        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

        # loaded again
        self.assertTrue(BlockDev.md_canonicalize_uuid("3386ff85:f5012621:4a435f06:1eb47236"))

    def test_ensure_init(self):
        """Verify that ensure_init just returns when already initialized"""

        # the library is already initialized, ensure_init() shonuld do nothing
        avail_plugs = BlockDev.get_available_plugin_names()
        self.assertTrue(BlockDev.ensure_init(self.requested_plugins, None))
        self.assertEqual(avail_plugs, BlockDev.get_available_plugin_names())

        # reinit with a subset of plugins
        plugins = BlockDev.plugin_specs_from_names(["swap", "part"])
        self.assertTrue(BlockDev.reinit(plugins, True, None))
        self.assertEqual(set(BlockDev.get_available_plugin_names()), set(["swap", "part"]))

        # ensure_init with the same subset -> nothing should change
        self.assertTrue(BlockDev.ensure_init(plugins, None))
        self.assertEqual(set(BlockDev.get_available_plugin_names()), set(["swap", "part"]))

        # ensure_init with more plugins -> extra plugins should be loaded
        plugins = BlockDev.plugin_specs_from_names(["swap", "part", "crypto"])
        self.assertTrue(BlockDev.ensure_init(plugins, None))
        self.assertEqual(set(BlockDev.get_available_plugin_names()), set(["swap", "part", "crypto"]))

        # reinit to unload all plugins
        self.assertTrue(BlockDev.reinit([], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), [])

        # ensure_init to load all plugins back
        self.assertTrue(BlockDev.ensure_init(self.requested_plugins, None))
        self.assertGreaterEqual(len(BlockDev.get_available_plugin_names()), 6)

    def test_non_en_init(self):
        """Verify that the library initializes with lang different from en_US"""

        orig_lang = os.environ.get("LANG")
        os.environ["LANG"] = "cs.CZ_UTF-8"
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        if orig_lang:
            os.environ["LANG"] = orig_lang
        else:
            del os.environ["LANG"]
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))


class PluginsTestCase(unittest.TestCase):
    # only LVM plugin for this test
    requested_plugins = BlockDev.plugin_specs_from_names(("lvm",))

    orig_config_dir = ""

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

        try:
            cls.devices_avail = BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.DEVICES, 0)
        except:
            cls.devices_avail = False

    def setUp(self):
        self.orig_config_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR", "")
        self.addCleanup(self._clean_up)

    def _clean_up(self):
        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")

        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = self.orig_config_dir

        # try to get everything back to normal by (re)loading all plugins
        BlockDev.reinit(self.requested_plugins, True, None)

    # recompiles the LVM plugin
    @tag_test(TestTags.SLOW, TestTags.CORE, TestTags.SOURCEONLY)
    def test_reload(self):
        """Verify that reloading plugins works as expected"""

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        ret = os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")
        self.assertEqual(ret, 0, "Failed to recompile libblockdev for reload test")

        # library should successfully reinitialize without reloading plugins
        self.assertTrue(BlockDev.reinit(self.requested_plugins, False, None))

        # LVM plugin not reloaded, max LV size should be the same
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # library should successfully reinitialize reloading plugins
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

        # LVM plugin reloaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")

        # library should successfully reinitialize reloading original plugins
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

    # recompiles the LVM plugin
    @tag_test(TestTags.SLOW, TestTags.SOURCEONLY)
    def test_force_plugin(self):
        """Verify that forcing plugin to be used works as expected"""

        # library should be successfully initialized
        self.assertTrue(BlockDev.is_initialized())

        # init() called twice, should give a warning and return False
        self.assertFalse(BlockDev.init(self.requested_plugins, None))

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        ret = os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")
        self.assertEqual(ret, 0, "Failed to recompile libblockdev for force plugin test")

        # proclaim the new build a different plugin
        os.system("cp src/plugins/.libs/libbd_lvm.so src/plugins/.libs/libbd_lvm2.so")

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        ret = os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")
        self.assertEqual(ret, 0, "Failed to recompile libblockdev for force plugin test")

        # force the new plugin to be used
        ps = BlockDev.PluginSpec(name=BlockDev.Plugin.LVM, so_name="libbd_lvm2.so")
        self.assertTrue(BlockDev.reinit([ps], True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # clean after ourselves
        os.system ("rm -f src/plugins/.libs/libbd_lvm2.so")

        # force the old plugin to be used
        ps = BlockDev.PluginSpec(name=BlockDev.Plugin.LVM, so_name="libbd_lvm.so")
        self.assertTrue(BlockDev.reinit([ps], True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

    # recompiles the LVM plugin
    @tag_test(TestTags.SLOW, TestTags.SOURCEONLY)
    def test_plugin_priority(self):
        """Verify that preferring plugin to be used works as expected"""

        # library should be successfully initialized
        self.assertTrue(BlockDev.is_initialized())

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        ret = os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")
        self.assertEqual(ret, 0, "Failed to recompile libblockdev for plugin priority test")

        # proclaim the new build a different plugin
        os.system("cp src/plugins/.libs/libbd_lvm.so src/plugins/.libs/libbd_lvm2.so.3")

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        ret = os.system("make -C src/plugins/ libbd_lvm.la >/dev/null 2>&1")
        self.assertEqual(ret, 0, "Failed to recompile libblockdev for plugin priority test")

        # now reinit the library with the config preferring the new build
        orig_conf_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR")
        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = "tests/test_configs/plugin_prio_conf.d"
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm2.so.3")
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # reinit with the original config
        if orig_conf_dir:
            os.environ["LIBBLOCKDEV_CONFIG_DIR"] = orig_conf_dir
        else:
            del os.environ["LIBBLOCKDEV_CONFIG_DIR"]
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # now reinit the library with the another config preferring the new
        # build
        orig_conf_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR")
        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = "tests/test_configs/plugin_multi_conf.d"
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm2.so.3")
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # reinit with the original config
        if orig_conf_dir:
            os.environ["LIBBLOCKDEV_CONFIG_DIR"] = orig_conf_dir
        else:
            del os.environ["LIBBLOCKDEV_CONFIG_DIR"]
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # clean after ourselves
        os.system ("rm -f src/plugins/.libs/libbd_lvm2.so")
