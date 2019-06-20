import unittest
import os
import re
import time
from contextlib import contextmanager
from distutils.version import LooseVersion
from distutils.spawn import find_executable
from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, wipe_all, fake_path, read_file, TestTags, tag_test
from bytesize import bytesize
import overrides_hack

from gi.repository import BlockDev, GLib


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

def _wait_for_bcache_setup(bcache_dev):
    i = 0
    cache_dir = "/sys/block/%s/bcache/cache" % bcache_dev
    while not os.access(cache_dir, os.R_OK):
        time.sleep(1)
        i += 1
        if i >= 30:
            print("WARNING: Giving up waiting for bcache setup!!!")
            break

class KbdZRAMTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("kbd", "swap"))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self._loaded_zram_module = False

    def _clean_up(self):
        # make sure we unload the module if we loaded it
        if self._loaded_zram_module:
            os.system("rmmod zram")

class KbdZRAMDevicesTestCase(KbdZRAMTestCase):
    @unittest.skipUnless(_can_load_zram(), "cannot load the 'zram' module")
    @tag_test(TestTags.SLOW)
    def test_create_destroy_devices(self):
        # the easiest case
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 2]))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())
            time.sleep(1)

        # no nstreams specified
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], None))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())
            time.sleep(1)

        # with module pre-loaded, but unsed
        self.assertEqual(os.system("modprobe zram num_devices=2"), 0)
        time.sleep(1)
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 1]))
            time.sleep(1)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())
            time.sleep(1)

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
            time.sleep(1)

    @unittest.skipUnless(_can_load_zram(), "cannot load the 'zram' module")
    @tag_test(TestTags.SLOW)
    def test_zram_add_remove_device(self):
        """Verify that it is possible to add and remove a zram device"""

        # the easiest case
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            succ, device = BlockDev.kbd_zram_add_device (10 * 1024**2, 4)
            self.assertTrue(succ)
            self.assertTrue(device.startswith("/dev/zram"))
            time.sleep(5)
            self.assertTrue(BlockDev.kbd_zram_remove_device(device))

        # no nstreams specified
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            succ, device = BlockDev.kbd_zram_add_device (10 * 1024**2, 0)
            self.assertTrue(succ)
            self.assertTrue(device.startswith("/dev/zram"))
            time.sleep(5)
            self.assertTrue(BlockDev.kbd_zram_remove_device(device))

        # create two devices
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            succ, device = BlockDev.kbd_zram_add_device (10 * 1024**2, 4)
            self.assertTrue(succ)
            self.assertTrue(device.startswith("/dev/zram"))

            succ, device2 = BlockDev.kbd_zram_add_device (10 * 1024**2, 4)
            self.assertTrue(succ)
            self.assertTrue(device2.startswith("/dev/zram"))

            time.sleep(5)
            self.assertTrue(BlockDev.kbd_zram_remove_device(device))
            self.assertTrue(BlockDev.kbd_zram_remove_device(device2))

        # mixture of multiple devices and a single device
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(2, [10 * 1024**2, 10 * 1024**2], [1, 2]))
            time.sleep(5)
            succ, device = BlockDev.kbd_zram_add_device (10 * 1024**2, 4)
            self.assertTrue(succ)
            self.assertTrue(device.startswith("/dev/zram"))
            time.sleep(5)
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())
            time.sleep(5)


class KbdZRAMStatsTestCase(KbdZRAMTestCase):
    @unittest.skipUnless(_can_load_zram(), "cannot load the 'zram' module")
    def test_zram_get_stats(self):
        """Verify that it is possible to get stats for a zram device"""

        # location of some sysfs files we use is different since linux 4.11
        kernel_version = os.uname()[2]
        if LooseVersion(kernel_version) >= LooseVersion("4.11"):
            self._zram_get_stats_new()
        else:
            self._zram_get_stats_old()

    def _zram_get_stats_new(self):
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(1, [10 * 1024**2], [2]))
            time.sleep(1)

        # XXX: this needs to get more complex/serious
        stats = BlockDev.kbd_zram_get_stats("zram0")
        self.assertTrue(stats)

        # /dev/zram0 should work too
        stats = BlockDev.kbd_zram_get_stats("/dev/zram0")
        self.assertTrue(stats)

        self.assertEqual(stats.disksize, 10 * 1024**2)
        # XXX: 'max_comp_streams' is currently broken on rawhide
        # https://bugzilla.redhat.com/show_bug.cgi?id=1352567
        # self.assertEqual(stats.max_comp_streams, 2)
        self.assertTrue(stats.comp_algorithm)

        # read 'num_reads' and 'num_writes' from '/sys/block/zram0/stat'
        sys_stats = read_file("/sys/block/zram0/stat").strip().split()
        self.assertGreaterEqual(len(sys_stats), 11)  # 15 stats since 4.19
        num_reads = int(sys_stats[0])
        num_writes = int(sys_stats[4])
        self.assertEqual(stats.num_reads, num_reads)
        self.assertEqual(stats.num_writes, num_writes)

        # read 'orig_data_size', 'compr_data_size', 'mem_used_total' and
        # 'zero_pages' from '/sys/block/zram0/mm_stat'
        sys_stats = read_file("/sys/block/zram0/mm_stat").strip().split()
        self.assertGreaterEqual(len(sys_stats), 7)  # since 4.18 we have 8 stats
        orig_data_size = int(sys_stats[0])
        compr_data_size = int(sys_stats[1])
        mem_used_total = int(sys_stats[2])
        zero_pages = int(sys_stats[5])
        self.assertEqual(stats.orig_data_size, orig_data_size)
        self.assertEqual(stats.compr_data_size, compr_data_size)
        self.assertEqual(stats.mem_used_total, mem_used_total)
        self.assertEqual(stats.zero_pages, zero_pages)

        # read 'invalid_io' and 'num_writes' from '/sys/block/zram0/io_stat'
        sys_stats = read_file("/sys/block/zram0/io_stat").strip().split()
        self.assertEqual(len(sys_stats), 4)
        invalid_io = int(sys_stats[2])
        self.assertEqual(stats.invalid_io, invalid_io)

        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())

    def _zram_get_stats_old(self):
        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_create_devices(1, [10 * 1024**2], [2]))
            time.sleep(1)

        stats = BlockDev.kbd_zram_get_stats("zram0")
        self.assertTrue(stats)

        # /dev/zram0 should work too
        stats = BlockDev.kbd_zram_get_stats("/dev/zram0")
        self.assertTrue(stats)

        self.assertEqual(stats.disksize, 10 * 1024**2)
        self.assertEqual(stats.max_comp_streams, 2)
        self.assertTrue(stats.comp_algorithm)

        num_reads = int(read_file("/sys/block/zram0/num_reads").strip())
        self.assertEqual(stats.num_reads, num_reads)
        num_writes = int(read_file("/sys/block/zram0/num_writes").strip())
        self.assertEqual(stats.num_writes, num_writes)

        orig_data_size = int(read_file("/sys/block/zram0/orig_data_size").strip())
        self.assertEqual(stats.orig_data_size, orig_data_size)
        compr_data_size = int(read_file("/sys/block/zram0/compr_data_size").strip())
        self.assertEqual(stats.compr_data_size, compr_data_size)
        mem_used_total = int(read_file("/sys/block/zram0/mem_used_total").strip())
        self.assertEqual(stats.mem_used_total, mem_used_total)
        zero_pages = int(read_file("/sys/block/zram0/zero_pages").strip())
        self.assertEqual(stats.zero_pages, zero_pages)

        invalid_io = int(read_file("/sys/block/zram0/invalid_io").strip())
        self.assertEqual(stats.invalid_io, invalid_io)

        with _track_module_load(self, "zram", "_loaded_zram_module"):
            self.assertTrue(BlockDev.kbd_zram_destroy_devices())

class KbdBcacheNodevTestCase(unittest.TestCase):
    # no setUp/tearDown methods needed
    requested_plugins = BlockDev.plugin_specs_from_names(("kbd", "swap"))

    @classmethod
    def setUpClass(cls):
        if not find_executable("make-bcache"):
            raise unittest.SkipTest("make-bcache executable not found in $PATH, skipping.")

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_bcache_mode_str_bijection(self):
        """Verify that it's possible to transform between cache modes and their string representations"""

        mode_mapping = ((BlockDev.KBDBcacheMode.WRITETHROUGH, "writethrough"),
                        (BlockDev.KBDBcacheMode.WRITEBACK, "writeback"),
                        (BlockDev.KBDBcacheMode.WRITEAROUND, "writearound"),
                        (BlockDev.KBDBcacheMode.NONE, "none"),
                        (BlockDev.KBDBcacheMode.UNKNOWN, "unknown"),
        )

        for (mode, mode_str) in mode_mapping:
            self.assertEqual(mode, BlockDev.kbd_bcache_get_mode_from_str(mode_str))
            self.assertEqual(mode_str, BlockDev.kbd_bcache_get_mode_str(mode))
            self.assertEqual(mode_str, BlockDev.kbd_bcache_get_mode_str(BlockDev.kbd_bcache_get_mode_from_str(mode_str)))
            self.assertEqual(mode, BlockDev.kbd_bcache_get_mode_from_str(BlockDev.kbd_bcache_get_mode_str(mode)))

class KbdBcacheTestCase(unittest.TestCase):
    requested_plugins = BlockDev.plugin_specs_from_names(("kbd", "swap"))

    @classmethod
    def setUpClass(cls):
        if not find_executable("make-bcache"):
            raise unittest.SkipTest("make-bcache executable not found in $PATH, skipping.")

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("lvm_test", 10 * 1024**3)
        self.dev_file2 = create_sparse_tempfile("lvm_test", 10 * 1024**3)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev2 = create_lio_device(self.dev_file2)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

        self.bcache_dev = None

    def _clean_up(self):
        if self.bcache_dev:
            try:
                BlockDev.kbd_bcache_destroy(self.bcache_dev)
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

class KbdTestBcacheCreate(KbdBcacheTestCase):
    @tag_test(TestTags.UNSTABLE)
    def test_bcache_create_destroy(self):
        """Verify that it's possible to create and destroy a bcache device"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(5)

        wipe_all(self.loop_dev, self.loop_dev2)

    @tag_test(TestTags.UNSTABLE)
    def test_bcache_create_destroy_full_path(self):
        """Verify that it's possible to create and destroy a bcache device with full device path"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        succ = BlockDev.kbd_bcache_destroy("/dev/" + self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(5)

        wipe_all(self.loop_dev, self.loop_dev2)

class KbdTestBcacheAttachDetach(KbdBcacheTestCase):
    @tag_test(TestTags.UNSTABLE)
    def test_bcache_attach_detach(self):
        """Verify that it's possible to detach/attach a cache from/to a bcache device"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        succ, c_set_uuid = BlockDev.kbd_bcache_detach(self.bcache_dev)
        self.assertTrue(succ)
        self.assertTrue(c_set_uuid)

        succ = BlockDev.kbd_bcache_attach(c_set_uuid, self.bcache_dev)
        self.assertTrue(succ)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(1)

        wipe_all(self.loop_dev, self.loop_dev2)

    @tag_test(TestTags.UNSTABLE)
    def test_bcache_attach_detach_full_path(self):
        """Verify that it's possible to detach/attach a cache from/to a bcache device with full device path"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        succ, c_set_uuid = BlockDev.kbd_bcache_detach("/dev/" + self.bcache_dev)
        self.assertTrue(succ)
        self.assertTrue(c_set_uuid)

        succ = BlockDev.kbd_bcache_attach(c_set_uuid, "/dev/" + self.bcache_dev)
        self.assertTrue(succ)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(1)

        wipe_all(self.loop_dev, self.loop_dev2)

    @tag_test(TestTags.UNSTABLE)
    def test_bcache_detach_destroy(self):
        """Verify that it's possible to destroy a bcache device with no cache attached"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        succ, c_set_uuid = BlockDev.kbd_bcache_detach(self.bcache_dev)
        self.assertTrue(succ)
        self.assertTrue(c_set_uuid)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(1)

        wipe_all(self.loop_dev, self.loop_dev2)

class KbdTestBcacheGetSetMode(KbdBcacheTestCase):
    @tag_test(TestTags.UNSTABLE)
    def test_bcache_get_set_mode(self):
        """Verify that it is possible to get and set Bcache mode"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        mode = BlockDev.kbd_bcache_get_mode(self.bcache_dev)
        self.assertNotEqual(mode, BlockDev.KBDBcacheMode.UNKNOWN)

        for mode_str in ("writethrough", "writeback", "writearound", "none"):
            mode = BlockDev.kbd_bcache_get_mode_from_str(mode_str)
            succ = BlockDev.kbd_bcache_set_mode(self.bcache_dev, mode)
            self.assertTrue(succ)
            new_mode = BlockDev.kbd_bcache_get_mode(self.bcache_dev)
            self.assertEqual(mode, new_mode)
            self.assertEqual(mode_str, BlockDev.kbd_bcache_get_mode_str(new_mode))

        mode_str = "unknown"
        mode = BlockDev.kbd_bcache_get_mode_from_str(mode_str)
        with self.assertRaises(GLib.GError):
            # cannot set mode to "uknown"
            BlockDev.kbd_bcache_set_mode(self.bcache_dev, mode)

        mode_str = "bla"
        with self.assertRaises(GLib.GError):
            mode = BlockDev.kbd_bcache_get_mode_from_str(mode_str)

        # set back to some caching mode
        mode_str = "writethrough"
        mode = BlockDev.kbd_bcache_get_mode_from_str(mode_str)
        succ = BlockDev.kbd_bcache_set_mode(self.bcache_dev, mode)
        self.assertTrue(succ)

        _wait_for_bcache_setup(dev)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(1)

        wipe_all(self.loop_dev, self.loop_dev2)

class KbdTestBcacheStatusTest(KbdBcacheTestCase):

    def _get_size(self, bcache_name):
        cache_dir = '/sys/block/%s/bcache/cache' % bcache_name

        # sum sizes from all caches
        caches = ['%s/%s' % (cache_dir, d) for d in os.listdir(cache_dir) if re.match('cache[0-9]*$', d)]
        return sum(int(read_file(os.path.realpath(c) + '/../size')) for c in caches)

    @tag_test(TestTags.UNSTABLE)
    def test_bcache_status(self):
        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        # should work with both "bcacheX" and "/dev/bcacheX"
        status = BlockDev.kbd_bcache_status(self.bcache_dev)
        self.assertTrue(status)
        status = BlockDev.kbd_bcache_status("/dev/" + self.bcache_dev)
        self.assertTrue(status)

        # check some basic values
        self.assertTrue(status.state)
        sys_state = read_file("/sys/block/%s/bcache/state" % self.bcache_dev).strip()
        self.assertEqual(status.state, sys_state)
        sys_block = read_file("/sys/block/%s/bcache/cache/block_size" % self.bcache_dev).strip()
        self.assertEqual(status.block_size, int(bytesize.Size(sys_block)))
        sys_size = self._get_size(self.bcache_dev)
        self.assertGreater(status.cache_size, sys_size)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(1)

        wipe_all(self.loop_dev, self.loop_dev2)

class KbdTestBcacheBackingCacheDevTest(KbdBcacheTestCase):
    @tag_test(TestTags.UNSTABLE)
    def test_bcache_backing_cache_dev(self):
        """Verify that is is possible to get the backing and cache devices for a Bcache"""

        succ, dev = BlockDev.kbd_bcache_create(self.loop_dev, self.loop_dev2, None)
        self.assertTrue(succ)
        self.assertTrue(dev)
        self.bcache_dev = dev

        _wait_for_bcache_setup(dev)

        self.assertEqual("/dev/" + BlockDev.kbd_bcache_get_backing_device(self.bcache_dev), self.loop_dev)
        self.assertEqual("/dev/" + BlockDev.kbd_bcache_get_cache_device(self.bcache_dev), self.loop_dev2)

        succ = BlockDev.kbd_bcache_destroy(self.bcache_dev)
        self.assertTrue(succ)
        self.bcache_dev = None
        time.sleep(1)

        wipe_all(self.loop_dev, self.loop_dev2)

class KbdUnloadTest(KbdBcacheTestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    @tag_test(TestTags.NOSTORAGE)
    def test_check_no_bcache_progs(self):
        """Verify that checking the availability of make-bcache works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="make-bcache"):
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("kbd", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("kbd", BlockDev.get_available_plugin_names())
