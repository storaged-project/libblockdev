import os
import unittest
import re

from gi.repository import BlockDev
if not BlockDev.is_initialized():
    assert BlockDev.init(None, None)

class LibraryOpsTestCase(unittest.TestCase):
    log = ""

    def test_reload(self):
        """Verify that reloading plugins works as expected"""

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?BD_LVM_MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make libbd_lvm.so > /dev/null")

        # library should successfully reinitialize without reloading plugins
        self.assertTrue(BlockDev.reinit(None, False, None))

        # LVM plugin not reloaded, max LV size should be the same
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # library should successfully reinitialize reloading plugins
        self.assertTrue(BlockDev.reinit(None, True, None))

        # LVM plugin reloaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?BD_LVM_MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make libbd_lvm.so > /dev/null")

        # library should successfully reinitialize reloading original plugins
        self.assertTrue(BlockDev.reinit(None, True, None))

    def test_force_plugin(self):
        """Verify that forcing plugin to be used works as expected"""

        # library should be successfully initialized
        self.assertTrue(BlockDev.is_initialized())

        # init() called twice, should give a warning and return False
        self.assertFalse(BlockDev.init(None, None))

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?BD_LVM_MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make libbd_lvm.so > /dev/null")

        # proclaim the new build a different plugin
        os.system("cp build/libbd_lvm.so build/libbd_lvm2.so")

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?BD_LVM_MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make libbd_lvm.so > /dev/null")

        # force the new plugin to be used
        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.LVM
        ps.so_name = "libbd_lvm2.so"
        self.assertTrue(BlockDev.reinit([ps], True, None))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # clean after ourselves
        os.system ("rm -f build/libbd_lvm2.so")

        # force the old plugin to be used
        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.LVM
        ps.so_name = "libbd_lvm.so"
        self.assertTrue(BlockDev.reinit([ps], True, None))

    def my_log_func(self, level, msg):
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    def test_logging_setup(self):
        """Verify that setting up logging works as expected"""

        self.assertTrue(BlockDev.reinit(None, False, self.my_log_func))

        succ = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)

        # reinit with no logging function should change nothing about logging
        self.assertTrue(BlockDev.reinit(None, False, None))

        succ, out = BlockDev.utils_exec_and_capture_output(["echo", "hi"])
        self.assertTrue(succ)
        self.assertEqual(out, "hi\n")

        match = re.search(r'Running \[(\d+)\] true', self.log)
        self.assertIsNot(match, None)
        task_id1 = match.group(1)
        match = re.search(r'Running \[(\d+)\] echo hi', self.log)
        self.assertIsNot(match, None)
        task_id2 = match.group(1)

        self.assertIn("...done [%s]" % task_id1, self.log)
        self.assertIn("stdout[%s]:" % task_id1, self.log)
        self.assertIn("stderr[%s]:" % task_id1, self.log)

        self.assertIn("stdout[%s]: hi" % task_id2, self.log)
        self.assertIn("stderr[%s]:" % task_id2, self.log)
        self.assertIn("...done [%s]" % task_id2, self.log)

    def test_require_plugins(self):
        """Verify that loading only required plugins works as expected"""

        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.BTRFS
        ps.so_name = ""
        self.assertTrue(BlockDev.reinit([ps], True, None))
        self.assertEqual(BlockDev.get_available_plugin_names(), ["btrfs"])
        self.assertTrue(BlockDev.reinit(None, True, None))

    def test_try_init(self):
        """Verify that try_init just returns when already initialized"""

        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.BTRFS
        ps.so_name = "nonexisting.so"
        avail_plugs = BlockDev.get_available_plugin_names()

        # try init should exit early enough to not hit the issue with
        # non-existing so file since the library is already initialized here, it
        # shouldn't report any error and it shouldn't affect the loaded plugins
        self.assertTrue(BlockDev.try_init([ps], None))
        self.assertEqual(avail_plugs, BlockDev.get_available_plugin_names())
