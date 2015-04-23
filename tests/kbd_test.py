import unittest
import os
import time
from contextlib import contextmanager
import overrides_hack

from gi.repository import BlockDev, GLib
if not BlockDev.is_initialized():
    BlockDev.init(None, None)

def _can_load_zram():
    """Test if we can load the zram module"""

    if os.system("lsmod|grep zram >/dev/null") != 0:
        # not loaded
        return True
    elif os.system("rmmod zram") == 0:
        # successfully unloaded
        return True
    else:
        # loaded and failed to unload
        return False

@contextmanager
def _track_module_load(test_case, mod_name, loaded_attr):
    setattr(test_case, loaded_attr, os.system("lsmod|grep %s > /dev/null" % mod_name) == 0)
    try:
        yield
    finally:
        setattr(test_case, loaded_attr, os.system("lsmod|grep %s > /dev/null" % mod_name) == 0)

class KbdZRAMTestCase(unittest.TestCase):
    def setUp(self):
        self._loaded_zram_module = False

    def tearDown(self):
        # make sure we unload the module if we loaded it
        if self._loaded_zram_module:
            os.system("rmmod zram")

    @unittest.skipUnless(_can_load_zram(), "cannot load the 'zram' module")
    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_create_destroy_devices(self):
        # the easiest case
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 2]))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())

        # no nstreams specified
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], None))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())

        # with module pre-loaded, but unsed
        self.assertEqual(os.system("modprobe zram num_devices=2"), 0)
        time.sleep(1)
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 1]))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())

        # with module pre-loaded, and devices used (as active swaps)
        self.assertEqual(os.system("modprobe zram num_devices=2"), 0)
        self.assertEqual(os.system("echo 10M > /sys/class/block/zram0/disksize"), 0)
        self.assertEqual(os.system("echo 10M > /sys/class/block/zram1/disksize"), 0)
        time.sleep(1)
        for zram_dev in ("/dev/zram0", "/dev/zram1"):
            self.assertTrue(BlockDev.swap_mkswap(zram_dev, None))
            self.assertTrue(BlockDev.swap_swapon(zram_dev, -1))
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            with self.assertRaises(GLib.GError):
                self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 1]))
            for zram_dev in ("/dev/zram0", "/dev/zram1"):
                self.assertTrue(BlockDev.swap_swapoff(zram_dev))
            self.assertEqual(os.system("rmmod zram"), 0)

            # should work just fine now
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 1]))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())


