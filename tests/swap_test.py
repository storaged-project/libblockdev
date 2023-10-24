import unittest
import os
import resource
import overrides_hack

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, fake_utils, fake_path, run_command, run, TestTags, tag_test

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev

class SwapTest(unittest.TestCase):
    requested_plugins = BlockDev.plugin_specs_from_names(("swap",))
    dev_size = 1024**3

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

class SwapPluginVersionTestCase(SwapTest):
    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.SWAP), "libbd_swap.so.3")

class SwapTestCase(SwapTest):
    test_uuid = "4d7086c4-a4d3-432f-819e-73da03870df9"

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("swap_test", self.dev_size)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            BlockDev.swap_swapoff(self.loop_dev)
        except:
            pass

        try:
            delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)

    @tag_test(TestTags.CORE)
    def test_all(self):
        """Verify that swap_* functions work as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.swap_mkswap("/non/existing/device", None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapon("/non/existing/device", -1)

        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapoff("/non/existing/device")

        self.assertFalse(BlockDev.swap_swapstatus("/non/existing/device"))

        # not a swap device (yet)
        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapon(self.loop_dev, -1)

        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapoff(self.loop_dev)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertFalse(on)

        # the common/expected sequence of calls
        succ = BlockDev.swap_mkswap(self.loop_dev, None, None)
        self.assertTrue(succ)

        with self.assertRaisesRegex(GLib.GError, "at most 16 characters long."):
            BlockDev.swap_check_label(17 * "a")

        succ = BlockDev.swap_set_label(self.loop_dev, "BlockDevSwap")
        self.assertTrue(succ)

        _ret, out, _err = run_command("blkid -ovalue -sLABEL -p %s" % self.loop_dev)
        self.assertEqual(out, "BlockDevSwap")

        with self.assertRaisesRegex(GLib.GError, "not a valid RFC-4122 UUID"):
            BlockDev.swap_check_uuid("aaaaaaa")

        succ = BlockDev.swap_set_uuid(self.loop_dev, uuid=self.test_uuid)
        self.assertTrue(succ)

        _ret, out, _err = run_command("blkid -ovalue -sUUID -p %s" % self.loop_dev)
        self.assertEqual(out, self.test_uuid)

        succ = BlockDev.swap_swapon(self.loop_dev, -1)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertTrue(on)

        succ = BlockDev.swap_swapoff(self.loop_dev)
        self.assertTrue(succ)

        # already off
        with self.assertRaises(GLib.GError):
            BlockDev.swap_swapoff(self.loop_dev)

        on = BlockDev.swap_swapstatus(self.loop_dev)
        self.assertFalse(on)

    def test_mkswap_with_label(self):
        """Verify that mkswap with label works as expected"""

        succ = BlockDev.swap_mkswap(self.loop_dev, "BlockDevSwap", None)
        self.assertTrue(succ)

        _ret, out, _err = run_command("blkid -ovalue -sLABEL -p %s" % self.loop_dev)
        self.assertEqual(out, "BlockDevSwap")

    def test_mkswap_with_uuid(self):
        """Verify that mkswap with uuid works as expected"""

        succ = BlockDev.swap_mkswap(self.loop_dev, uuid=self.test_uuid)
        self.assertTrue(succ)

        _ret, out, _err = run_command("blkid -ovalue -sUUID -p %s" % self.loop_dev)
        self.assertEqual(out, self.test_uuid)

    def test_swapon_pagesize(self):
        """Verify that activating swap with different pagesize fails"""

        # pick some wrong page size: 8k on 64k and 64k everywhere else
        pagesize = resource.getpagesize()
        if pagesize == 65536:
            wrong_pagesize = 8192
        else:
            wrong_pagesize = 65536

        # create swap with "wrong" pagesize
        ret, out, err = run_command("mkswap --pagesize %s %s" % (wrong_pagesize, self.loop_dev))
        if ret != 0:
            self.fail("Failed to prepare swap for pagesize test: %s %s" % (out, err))

        # activation should fail because swap has different pagesize
        with self.assertRaises(BlockDev.SwapPagesizeError):
            BlockDev.swap.swapon(self.loop_dev)

    def test_swapon_wrong_size(self):
        """Verify that activating swap with a wrong size fails with expected exception"""

        # create swap bigger than the device (twice as big in 1024 sectors)
        ret, out, err = run_command("mkswap -f %s %d" % (self.loop_dev, (self.dev_size * 2) / 1024))
        if ret != 0:
            self.fail("Failed to prepare swap for wrong size test: %s %s" % (out, err))

        # activation should fail because swap is bigger than the underlying device
        with self.assertRaises(BlockDev.SwapActivateError):
            BlockDev.swap.swapon(self.loop_dev)

    def _remove_map(self, map_name):
        run("dmsetup remove -f %s" % map_name)

    @tag_test(TestTags.REGRESSION)
    def test_swapstatus_dm(self):
        """Verify that swapstatus works correctly with DM devices"""

        dm_name = "swapstatus-test"

        self.addCleanup(self._remove_map, dm_name)
        run("dmsetup create %s --table \"0 $((`lsblk -osize -b %s -nd`/512)) linear %s 0\"" % (dm_name, self.loop_dev, self.loop_dev))

        succ = BlockDev.swap_mkswap("/dev/mapper/%s" % dm_name, None, None)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus("/dev/mapper/%s" % dm_name)
        self.assertFalse(on)

        succ = BlockDev.swap_swapon("/dev/mapper/%s" % dm_name, -1)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus("/dev/mapper/%s" % dm_name)
        self.assertTrue(on)

        succ = BlockDev.swap_swapoff("/dev/mapper/%s" % dm_name)
        self.assertTrue(succ)

        on = BlockDev.swap_swapstatus("/dev/mapper/%s" % dm_name)
        self.assertFalse(on)


class SwapDepsTest(SwapTest):
    @tag_test(TestTags.NOSTORAGE)
    def test_missing_dependencies(self):
        """Verify that checking for technology support works as expected"""

        with fake_utils("tests/fake_utils/swap_low_version/"):
            # too low version of mkswap available
            with self.assertRaisesRegex(GLib.GError, "Too low version of mkswap"):
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP, BlockDev.SwapTechMode.CREATE)

        with fake_path(all_but="mkswap"):
            # no mkswap available
            with self.assertRaisesRegex(GLib.GError, "The 'mkswap' utility is not available"):
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP, BlockDev.SwapTechMode.CREATE)

        with fake_path(all_but="swaplabel"):
            # no swaplabel available
            with self.assertRaisesRegex(GLib.GError, "The 'swaplabel' utility is not available"):
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP, BlockDev.SwapTechMode.SET_LABEL)

        with fake_path(all_but="mkswap"):
            with self.assertRaisesRegex(GLib.GError, "The 'mkswap' utility is not available"):
                # the device shouldn't matter, the function should return an
                # error before any other checks or actions
                BlockDev.swap_mkswap("/dev/device", "LABEL", None)


class SwapTechAvailable(SwapTest):

    @tag_test(TestTags.NOSTORAGE)
    def test_check_tech_available(self):
        """Verify that runtime checking mkswap and swaplabel tools availability
           works as expected
        """

        with fake_path(all_but="mkswap"):
            BlockDev.reinit(self.requested_plugins, True, None)

            with self.assertRaises(GLib.GError):
                # we have swaplabel but not mkswap, so this should fail
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                            BlockDev.SwapTechMode.CREATE | BlockDev.SwapTechMode.SET_LABEL)

            with self.assertRaises(GLib.GError):
                # we have swaplabel but not mkswap, so this should fail
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                            BlockDev.SwapTechMode.CREATE)

            # only label checked -- should pass
            succ = BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                               BlockDev.SwapTechMode.SET_LABEL)
            self.assertTrue(succ)

            # only UUID checked -- should pass
            succ = BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                               BlockDev.SwapTechMode.SET_UUID)
            self.assertTrue(succ)

        with fake_path(all_but="swaplabel"):
            BlockDev.reinit(self.requested_plugins, True, None)

            with self.assertRaises(GLib.GError):
                # we have mkswap but not swaplabel, so this should fail
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                            BlockDev.SwapTechMode.CREATE | BlockDev.SwapTechMode.SET_LABEL)

            with self.assertRaises(GLib.GError):
                # we have mkswap but not swaplabel, so this should fail
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                            BlockDev.SwapTechMode.SET_LABEL)

            with self.assertRaises(GLib.GError):
                # we have mkswap but not swaplabel, so this should fail
                BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                            BlockDev.SwapTechMode.SET_UUID)

            # only label checked -- should pass
            succ = BlockDev.swap_is_tech_avail(BlockDev.SwapTech.SWAP,
                                               BlockDev.SwapTechMode.CREATE)
            self.assertTrue(succ)
