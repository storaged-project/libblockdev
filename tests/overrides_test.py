import unittest
import math
import overrides_hack
from gi.repository import BlockDev

from utils import TestTags, tag_test


class OverridesTest(unittest.TestCase):
    # all plugins except for 'btrfs', 'fs' and 'mpath' -- these don't have all
    # the dependencies on CentOS/Debian and we don't need them for this test
    requested_plugins = BlockDev.plugin_specs_from_names(("crypto", "dm",
                                                          "kbd", "loop", "lvm",
                                                          "mdraid", "part", "swap"))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

class OverridesTestCase(OverridesTest):
    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_error_proxy(self):
        """Verify that the error proxy works as expected"""

        # calls via the error proxy has to be done as
        # e.g. BlockDev.swap.swapon() instead of BlockDev.swap_swapon(), since
        # BlockDev.swap is an ErrorProxy instance and BlockDev.swap_swapon() is
        # the function it calls

        # test that exceptions are correctly transformed
        try:
            # no need to specify priority since we are using overrides that
            # define the default value for the parameter (-1)
            BlockDev.swap.swapon("/non/existing", -1)
        except BlockDev.BlockDevError as e:
            # we caught the generic error, now let's test that it is also the
            # fine-grained one
            self.assertTrue(isinstance(e, BlockDev.SwapError))

        # test that a second call like that works the same (should go from the cache)
        try:
            BlockDev.swap.swapon("/non/existing", -1)
        except BlockDev.BlockDevError as e:
            self.assertTrue(isinstance(e, BlockDev.SwapError))

        # test that successful calls propagate the results
        self.assertTrue(BlockDev.lvm.is_supported_pe_size(4 * 1024))
        self.assertEqual(BlockDev.lvm.round_size_to_pe(11 * 1024**2, 4 * 1024**2, True), 12 * 1024**2)

        # test that utils functions are available via a proxy too
        try:
            BlockDev.utils.version_cmp("1.1", "malformed")
        except BlockDev.BlockDevError as e:
            self.assertTrue(isinstance(e, BlockDev.UtilsError))

        self.assertEqual(BlockDev.utils.version_cmp("1.1", "1.2"), -1)

        # test that overrides are used over the proxy
        expected_padding = BlockDev.lvm_round_size_to_pe(int(math.ceil(11 * 1024**2 * 0.2)),
                                                         4 * 1024**2, True)
        # the original lvm_get_thpool_padding takes 3 arguments, but one is enough for the overriden version
        self.assertEqual(BlockDev.lvm_get_thpool_padding(11 * 1024**2),
                         expected_padding)

class OverridesUnloadTestCase(OverridesTest):
    def tearDown(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_xrules(self):
        """Verify that regexp-based transformation rules work as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        # no longer loaded
        with self.assertRaises(BlockDev.BlockDevNotImplementedError):
            BlockDev.lvm.get_max_lv_size()

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

    @tag_test(TestTags.NOSTORAGE, TestTags.CORE)
    def test_exception_inheritance(self):
        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        # the exception should be properly inherited from two classes
        with self.assertRaises(NotImplementedError):
            BlockDev.lvm.get_max_lv_size()
        with self.assertRaises(BlockDev.BlockDevError):
            BlockDev.lvm.get_max_lv_size()

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
