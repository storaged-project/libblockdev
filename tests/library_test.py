import os
import unittest

from gi.repository import BlockDev

class LibraryOpsTestCase(unittest.TestCase):
    def test_reload(self):
        """Verify that reloading plugins works as expected"""

        # library should successfully initialize
        self.assertTrue(BlockDev.init(None))

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make src/plugins/libbd_lvm.so > /dev/null")

        # library should successfully reinitialize without reloading plugins
        self.assertTrue(BlockDev.reinit(None, False))

        # LVM plugin not reloaded, max LV size should be the same
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), orig_max_size)

        # library should successfully reinitialize reloading plugins
        self.assertTrue(BlockDev.reinit(None, True))

        # LVM plugin reloaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make src/plugins/libbd_lvm.so > /dev/null")

        # library should successfully reinitialize reloading original plugins
        self.assertTrue(BlockDev.reinit(None, True))

    def test_force_plugin(self):
        """Verify that forcing plugin to be used works as expected"""

        # library should successfully initialize
        self.assertTrue(BlockDev.init(None))

        # max LV size should be something sane (not 1024 bytes)
        orig_max_size = BlockDev.lvm_get_max_lv_size()
        self.assertNotEqual(orig_max_size, 1024)

        # change the sources and recompile
        os.system("sed -ri 's?MAX_LV_SIZE;?1024;//test-change?' src/plugins/lvm.c > /dev/null")
        os.system("make src/plugins/libbd_lvm.so > /dev/null")

        # proclaim the new build a different plugin
        os.system("cp src/plugins/libbd_lvm.so src/plugins/libbd_lvm2.so")

        # change the sources back and recompile
        os.system("sed -ri 's?1024;//test-change?MAX_LV_SIZE;?' src/plugins/lvm.c > /dev/null")
        os.system("make src/plugins/libbd_lvm.so > /dev/null")

        # force the new plugin to be used
        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.LVM
        ps.so_name = "libbd_lvm2.so"
        self.assertTrue(BlockDev.reinit([ps], True))

        # new LVM plugin loaded, max LV size should be 1024 bytes
        self.assertEqual(BlockDev.lvm_get_max_lv_size(), 1024)

        # clean after ourselves
        os.system ("rm -f src/plugins/libbd_lvm2.so")

        # force the old plugin to be used
        ps = BlockDev.PluginSpec()
        ps.name = BlockDev.Plugin.LVM
        ps.so_name = "libbd_lvm.so"
        self.assertTrue(BlockDev.reinit([ps], True))

