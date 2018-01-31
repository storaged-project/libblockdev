import json
import os
import unittest
import overrides_hack

from utils import run_command, read_file, skip_on, fake_path
from gi.repository import BlockDev, GLib


@skip_on("debian", reason="NVDIMM plugin doesn't work on Debian (missing ndctl)")
class NVDIMMTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("nvdimm",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

class NVDIMMNamespaceTestCase(NVDIMMTestCase):

    sys_info = None

    def _get_nvdimm_info(self):
        ret, out, _err = run_command("ndctl list")
        if ret != 0 or not out:
            return None

        decoder = json.JSONDecoder()
        decoded = decoder.decode(out)
        return decoded

    def setUp(self):
        self.sys_info = self._get_nvdimm_info()
        if not self.sys_info:
            self.skipTest("No NVDIMM devices available.")

        # skip the test if there is more than one device
        # these tests change nvdimm mode a we need to be sure that we are really
        # working with the 'fake' device created by the memmap kernel cmdline option
        if type(self.sys_info) is not dict:
            self.skipTest("Multiple NVDIMM devices found.")

        # skip the tests if the nvdimm is a 'fake' one
        cmdline = read_file("/proc/cmdline")
        if "memmap=" not in cmdline:
            self.skipTest("NVDIMM device found, but not created by the 'memmap' kernel command-line option.")

    def _check_namespace_info(self, bd_info):
        self.assertEqual(bd_info.dev, self.sys_info["dev"])
        self.assertEqual(bd_info.mode, BlockDev.nvdimm_namespace_get_mode_from_str(self.sys_info["mode"]))
        self.assertEqual(bd_info.size, self.sys_info["size"])

        if "uuid" in self.sys_info.keys():
            self.assertEqual(bd_info.uuid, self.sys_info["uuid"])
        else:
            self.assertIsNone(bd_info.uuid)

        if "blockdev" in self.sys_info.keys():
            self.assertEqual(bd_info.blockdev, self.sys_info["blockdev"])
        else:
            self.assertIsNone(bd_info.blockdev)

        if "sector_size" in self.sys_info.keys():
            self.assertEqual(bd_info.sector_size, self.sys_info["sector_size"])
        else:
            self.assertEqual(bd_info.sector_size, 0)

    def test_namespace_info(self):
        # get info about our 'testing' namespace
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self._check_namespace_info(info)

        info = BlockDev.nvdimm_namespace_info("definitely-not-a-namespace")
        self.assertIsNone(info)

    def test_list_namespaces(self):
        bd_namespaces = BlockDev.nvdimm_list_namespaces()
        self.assertEqual(len(bd_namespaces), 1)

        self._check_namespace_info(bd_namespaces[0])

    @unittest.skipUnless("JENKINS_HOME" in os.environ, "skipping test that modifies system configuration")
    def test_enable_disable(self):
        # non-existing/unknow namespace
        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_enable("definitely-not-a-namespace")

        # enable the namespace
        ret = BlockDev.nvdimm_namespace_enable(self.sys_info["dev"])
        self.assertTrue(ret)

        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertTrue(info.enabled)

        # disable the namespace
        ret = BlockDev.nvdimm_namespace_disable(self.sys_info["dev"])
        self.assertTrue(ret)

        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertFalse(info.enabled)

        # and enable it again
        ret = BlockDev.nvdimm_namespace_enable(self.sys_info["dev"])
        self.assertTrue(ret)

        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertTrue(info.enabled)

    @unittest.skipUnless("JENKINS_HOME" in os.environ, "skipping test that modifies system configuration")
    def test_namespace_reconfigure(self):
        # active namespace -- reconfigure doesn't work without force
        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                  BlockDev.NVDIMMNamespaceMode.SECTOR,
                                                  False)

        # non-existing/unknow mode
        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                  BlockDev.NVDIMMNamespaceMode.UNKNOWN,
                                                  True)

        # non-existing/unknow namespace
        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_reconfigure("definitely-not-a-namespace",
                                                  BlockDev.NVDIMMNamespaceMode.SECTOR,
                                                  True)

        # switch to sector mode
        ret = BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                    BlockDev.NVDIMMNamespaceMode.SECTOR,
                                                    True, extra={"-l": "512"})
        self.assertTrue(ret)
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertEqual(info.mode, BlockDev.NVDIMMNamespaceMode.SECTOR)

        # and now to memory mode
        ret = BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                    BlockDev.NVDIMMNamespaceMode.MEMORY,
                                                    True)
        self.assertTrue(ret)
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertEqual(info.mode, BlockDev.NVDIMMNamespaceMode.MEMORY)

        # and back to sector
        ret = BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                    BlockDev.NVDIMMNamespaceMode.SECTOR,
                                                    True, extra={"-l": "512"})
        self.assertTrue(ret)
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertEqual(info.mode, BlockDev.NVDIMMNamespaceMode.SECTOR)


class NVDIMMUnloadTest(NVDIMMTestCase):
    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)

    def test_check_no_ndctl(self):
        """Verify that checking ndctl tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="ndctl"):
            # no ndctl tool available, the NVDIMM plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("nvdimm", BlockDev.get_available_plugin_names())

        # load the plugins back
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        self.assertIn("nvdimm", BlockDev.get_available_plugin_names())
