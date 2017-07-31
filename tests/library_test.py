import os
import unittest
import re
import overrides_hack
from utils import fake_path

from gi.repository import GLib, BlockDev

# all plugins expcept mpath -- it doesn't have all the dependencies on Debian
# and we don't need it for this test
REQUESTED_PLUGINS = BlockDev.plugin_specs_from_names(("btrfs", "crypto", "dm",
                                                      "fs", "kbd", "loop", "lvm",
                                                      "mdraid", "part", "swap"))

if not BlockDev.is_initialized():
    BlockDev.init(REQUESTED_PLUGINS, None)
else:
    BlockDev.reinit(REQUESTED_PLUGINS, True, None)

class LibraryOpsTestCase(unittest.TestCase):
    log = ""

    def setUp(self):
        self.addCleanup(self._clean_up)

    def _clean_up(self):
        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?BD_LVM_MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &> /dev/null")

        # try to get everything back to normal by (re)loading all plugins
        BlockDev.reinit(REQUESTED_PLUGINS, True, None)

    # recompiles the LVM plugin
    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_reload(self):
        """Verify that reloading plugins works as expected"""

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?BD_LVM_MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &> /dev/null")

        # library should successfully reinitialize without reloading plugins
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, False, None))

        # LVM plugin not reloaded, max LV size should be the same
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # library should successfully reinitialize reloading plugins
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # LVM plugin reloaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?BD_LVM_MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &> /dev/null")

        # library should successfully reinitialize reloading original plugins
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

    # recompiles the LVM plugin
    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_force_plugin(self):
        """Verify that forcing plugin to be used works as expected"""

        # library should be successfully initialized
        self.assertTrue(BlockDev.is_initialized())

        # init() called twice, should give a warning and return False
        self.assertFalse(BlockDev.init(REQUESTED_PLUGINS, None))

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?BD_LVM_MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &>/dev/null")

        # proclaim the new build a different plugin
        os.system("cp src/plugins/.libs/libbd_lvm.so src/plugins/.libs/libbd_lvm2.so")

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?BD_LVM_MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &>/dev/null")

        # force the new plugin to be used
        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.LVM
        ps.so_name = "libbd_lvm2.so"
        self.assertTrue(BlockDev.reinit([ps], True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # clean after ourselves
        os.system ("rm -f src/plugins/.libs/libbd_lvm2.so")

        # force the old plugin to be used
        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.LVM
        ps.so_name = "libbd_lvm.so"
        self.assertTrue(BlockDev.reinit([ps], True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

    # recompiles the LVM plugin
    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_plugin_priority(self):
        """Verify that preferring plugin to be used works as expected"""

        # library should be successfully initialized
        self.assertTrue(BlockDev.is_initialized())

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?BD_LVM_MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &>/dev/null")

        # proclaim the new build a different plugin
        os.system("cp src/plugins/.libs/libbd_lvm.so src/plugins/.libs/libbd_lvm2.so.2")

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?BD_LVM_MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &>/dev/null")

        # now reinit the library with the config preferring the new build
        orig_conf_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR")
        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = "tests/plugin_prio_conf.d"
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm2.so.2")
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # reinit with the original config
        if orig_conf_dir:
            os.environ["LIBBLOCKDEV_CONFIG_DIR"] = orig_conf_dir
        else:
            del os.environ["LIBBLOCKDEV_CONFIG_DIR"]
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # now reinit the library with the another config preferring the new
        # build
        orig_conf_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR")
        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = "tests/plugin_multi_conf.d"
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm2.so.2")
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # reinit with the original config
        if orig_conf_dir:
            os.environ["LIBBLOCKDEV_CONFIG_DIR"] = orig_conf_dir
        else:
            del os.environ["LIBBLOCKDEV_CONFIG_DIR"]
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # clean after ourselves
        os.system ("rm -f src/plugins/.libs/libbd_lvm2.so")

    # recompiles the LVM plugin
    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_plugin_fallback(self):
        """Verify that fallback when loading plugins works as expected"""

        # library should be successfully initialized
        self.assertTrue(BlockDev.is_initialized())

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?gboolean bd_lvm_check_deps \(\) \{?gboolean bd_lvm_check_deps () { return FALSE;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &>/dev/null")

        # proclaim the new build a different plugin
        os.system("cp src/plugins/.libs/libbd_lvm.so src/plugins/.libs/libbd_lvm2.so.2")

        # change the sources back and recompile
        os.system("sed -ri 's?gboolean bd_lvm_check_deps \(\) \{ return FALSE;//test-change?gboolean bd_lvm_check_deps () {?' src/plugins/lvm.c > /dev/null")
        os.system("make -C src/plugins/ libbd_lvm.la &>/dev/null")

        # now reinit the library with the config preferring the new build
        orig_conf_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR")
        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = "tests/plugin_prio_conf.d"
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # the original plugin should be loaded because the new one should fail
        # to load (due to check() returning FALSE)
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm.so.2")
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # reinit with the original config
        if orig_conf_dir:
            os.environ["LIBBLOCKDEV_CONFIG_DIR"] = orig_conf_dir
        else:
            del os.environ["LIBBLOCKDEV_CONFIG_DIR"]
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # now reinit the library with the another config preferring the new
        # build
        orig_conf_dir = os.environ.get("LIBBLOCKDEV_CONFIG_DIR")
        os.environ["LIBBLOCKDEV_CONFIG_DIR"] = "tests/plugin_multi_conf.d"
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # the original plugin should be loaded because the new one should fail
        # to load (due to check() returning FALSE)
        self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm.so.2")
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # reinit with the original config
        if orig_conf_dir:
            os.environ["LIBBLOCKDEV_CONFIG_DIR"] = orig_conf_dir
        else:
            del os.environ["LIBBLOCKDEV_CONFIG_DIR"]
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # clean after ourselves
        os.system ("rm -f src/plugins/.libs/libbd_lvm2.so")

    def my_log_func(self, level, msg):
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    def test_logging_setup(self):
        """Verify that setting up logging works as expected"""

        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, False, self.my_log_func))

        succ = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)

        # reinit with no logging function should change nothing about logging
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, False, None))

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

    def test_require_plugins(self):
        """Verify that loading only required plugins works as expected"""

        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.BTRFS
        ps.so_name = ""
        self.assertTrue(BlockDev.reinit([ps], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), ["btrfs"])
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

    def test_not_implemented(self):
        """Verify that unloaded/unimplemented functions report errors"""

        # should be loaded and working
        self.assertTrue(BlockDev.lvm_get_max_lv_size() > 0)

        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.BTRFS
        ps.so_name = ""
        self.assertTrue(BlockDev.reinit([ps], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), ["btrfs"])

        # no longer loaded
        with self.assertRaises(GLib.GError):
            BlockDev.lvm_get_max_lv_size()

        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # loaded again
        self.assertTrue(BlockDev.lvm_get_max_lv_size() > 0)

    def test_ensure_init(self):
        """Verify that ensure_init just returns when already initialized"""

        # the library is already initialized, ensure_init() shonuld do nothing
        avail_plugs = BlockDev.get_available_plugin_names()
        self.assertTrue(BlockDev.ensure_init(REQUESTED_PLUGINS, None))
        self.assertEqual(avail_plugs, BlockDev.get_available_plugin_names())

        # reinit with a subset of plugins
        plugins = BlockDev.plugin_specs_from_names(["btrfs", "lvm"])
        self.assertTrue(BlockDev.reinit(plugins, True, None))
        self.assertEqual(set(BlockDev.get_available_plugin_names()), set(["btrfs", "lvm"]))

        # ensure_init with the same subset -> nothing should change
        self.assertTrue(BlockDev.ensure_init(plugins, None))
        self.assertEqual(set(BlockDev.get_available_plugin_names()), set(["btrfs", "lvm"]))

        # ensure_init with more plugins -> extra plugins should be loaded
        plugins = BlockDev.plugin_specs_from_names(["btrfs", "lvm", "crypto"])
        self.assertTrue(BlockDev.ensure_init(plugins, None))
        self.assertEqual(set(BlockDev.get_available_plugin_names()), set(["btrfs", "lvm", "crypto"]))

        # reinit to unload all plugins
        self.assertTrue(BlockDev.reinit([], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), [])

        # ensure_init to load all plugins back
        self.assertTrue(BlockDev.ensure_init(REQUESTED_PLUGINS, None))
        self.assertGreaterEqual(len(BlockDev.get_available_plugin_names()), 9)

    def test_try_reinit(self):
        """Verify that try_reinit() works as expected"""

        # try reinitializing with only some utilities being available and thus
        # only some plugins able to load
        with fake_path("tests/lib_missing_utils", keep_utils=["swapon", "swapoff", "mkswap", "lvm", "btrfs", "thin_metadata_size"]):
            succ, loaded = BlockDev.try_reinit(REQUESTED_PLUGINS, True, None)
            self.assertFalse(succ)
            for plug_name in ("swap", "lvm", "btrfs"):
                self.assertIn(plug_name, loaded)

        # reset back to all plugins
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

        # now the same with a subset of plugins requested
        plugins = BlockDev.plugin_specs_from_names(["btrfs", "lvm", "swap"])
        with fake_path("tests/lib_missing_utils", keep_utils=["swapon", "swapoff", "mkswap", "lvm", "btrfs", "thin_metadata_size"]):
            succ, loaded = BlockDev.try_reinit(plugins, True, None)
            self.assertTrue(succ)
            self.assertEqual(set(loaded), set(["swap", "lvm", "btrfs"]))

    def test_non_en_init(self):
        """Verify that the library initializes with lang different from en_US"""

        orig_lang = os.environ.get("LANG")
        os.environ["LANG"] = "cs.CZ_UTF-8"
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))
        if orig_lang:
            os.environ["LANG"] = orig_lang
        else:
            del os.environ["LANG"]
        self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))

    def test_dep_checks_disabled(self):
        """Verify that disabling runtime dep checks works"""

        with fake_path(all_but="mkswap"):
            # should fail because of 'mkswap' missing
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(REQUESTED_PLUGINS, True, None)

        os.environ["LIBBLOCKDEV_SKIP_DEP_CHECKS"] = ""
        self.addCleanup(os.environ.pop, "LIBBLOCKDEV_SKIP_DEP_CHECKS")

        with fake_path(all_but="mkswap"):
            # should load just fine, skipping the runtime dep checks
            self.assertTrue(BlockDev.reinit(REQUESTED_PLUGINS, True, None))
