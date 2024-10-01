import json
import re
import shutil
import unittest
import overrides_hack

from packaging.version import Version

from utils import run_command, read_file, fake_path, TestTags, tag_test, required_plugins

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev


@required_plugins(("nvdimm",))
class NVDIMMTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("nvdimm",))

    @classmethod
    def setUpClass(cls):
        if not shutil.which("ndctl"):
            raise unittest.SkipTest("ndctl executable not found in $PATH, skipping.")

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

    def _get_ndctl_version(self):
        _ret, out, _err = run_command("ndctl --version")
        m = re.search(r"([\d\.]+)", out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError("Failed to determine ndctl version from: %s" % out)
        return Version(m.groups()[0])

    def setUp(self):
        self.sys_info = self._get_nvdimm_info()
        if not self.sys_info:
            self.skipTest("No NVDIMM devices available.")

        # skip the test if there is more than one device
        # these tests change nvdimm mode a we need to be sure that we are really
        # working with the 'fake' device created by the memmap kernel cmdline option
        if len(self.sys_info) > 1:
            self.skipTest("Multiple NVDIMM devices found.")

        # get the dict for the first nvdimm device
        self.sys_info = self.sys_info[0]

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
            # libndctl (and libblockdev too) returns 512 as default value
            # even for modes where sector size doesn't make sense
            self.assertEqual(bd_info.sector_size, 512)

    @tag_test(TestTags.EXTRADEPS, TestTags.CORE)
    def test_namespace_info(self):
        # get info about our 'testing' namespace
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self._check_namespace_info(info)

        # test also that getting namespace devname from blockdev name works
        namespace = BlockDev.nvdimm_namespace_get_devname(info.blockdev)
        self.assertEqual(namespace, self.sys_info["dev"])

        # should work even with path, e.g. /dev/pmem0
        namespace = BlockDev.nvdimm_namespace_get_devname("/dev/" + info.blockdev)
        self.assertEqual(namespace, self.sys_info["dev"])

        info = BlockDev.nvdimm_namespace_info("definitely-not-a-namespace")
        self.assertIsNone(info)

    @tag_test(TestTags.EXTRADEPS, TestTags.CORE)
    def test_list_namespaces(self):
        bd_namespaces = BlockDev.nvdimm_list_namespaces()
        self.assertEqual(len(bd_namespaces), 1)

        self._check_namespace_info(bd_namespaces[0])

    @tag_test(TestTags.EXTRADEPS, TestTags.UNSAFE)
    def test_enable_disable(self):
        # non-existing/unknown namespace
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

    @tag_test(TestTags.EXTRADEPS, TestTags.UNSAFE)
    def test_namespace_reconfigure(self):
        # active namespace -- reconfigure doesn't work without force
        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                  BlockDev.NVDIMMNamespaceMode.SECTOR,
                                                  False)

        # non-existing/unknown mode
        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                  BlockDev.NVDIMMNamespaceMode.UNKNOWN,
                                                  True)

        # non-existing/unknown namespace
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

        # ndctl renamed the modes from 'memory' and 'dax' to 'fsdax' and 'devdax'
        # in version 60, so we need to choose different mode based on version
        ndctl_version = self._get_ndctl_version()
        if ndctl_version >= Version("60.0"):
            mode = BlockDev.NVDIMMNamespaceMode.FSDAX
        else:
            mode = BlockDev.NVDIMMNamespaceMode.MEMORY

        ret = BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                    mode, True)
        self.assertTrue(ret)
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertEqual(info.mode, mode)

        # and back to sector
        ret = BlockDev.nvdimm_namespace_reconfigure(self.sys_info["dev"],
                                                    BlockDev.NVDIMMNamespaceMode.SECTOR,
                                                    True, extra={"-l": "512"})
        self.assertTrue(ret)
        info = BlockDev.nvdimm_namespace_info(self.sys_info["dev"])
        self.assertEqual(info.mode, BlockDev.NVDIMMNamespaceMode.SECTOR)


class NVDIMMDepsTest(NVDIMMTestCase):
    @tag_test(TestTags.NOSTORAGE)
    def test_missing_dependencies(self):
        """Verify that checking for technology support works as expected"""

        with fake_path(all_but="ndctl"):
            # no ndctl tool available
            with self.assertRaisesRegex(GLib.GError, "The 'ndctl' utility is not available"):
                BlockDev.nvdimm_is_tech_avail(BlockDev.NVDIMMTech.NAMESPACE, BlockDev.NVDIMMTechMode.RECONFIGURE)

            # ndctl is needed only for reconfigure
            avail = BlockDev.nvdimm_is_tech_avail(BlockDev.NVDIMMTech.NAMESPACE, BlockDev.NVDIMMTechMode.CREATE)
            self.assertTrue(avail)


class NVDIMMNoDevTest(NVDIMMTestCase):

    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.NVDIMM), "libbd_nvdimm.so.3")

    @tag_test(TestTags.NOSTORAGE)
    def test_supported_sector_sizes(self):
        """Verify that getting supported sector sizes works as expected"""

        with self.assertRaises(GLib.GError):
            BlockDev.nvdimm_namespace_get_supported_sector_sizes(BlockDev.NVDIMMNamespaceMode.UNKNOWN)

        sizes = BlockDev.nvdimm_namespace_get_supported_sector_sizes(BlockDev.NVDIMMNamespaceMode.SECTOR)
        self.assertListEqual(sizes, [512, 520, 528, 4096, 4104, 4160, 4224])

        sizes = BlockDev.nvdimm_namespace_get_supported_sector_sizes(BlockDev.NVDIMMNamespaceMode.FSDAX)
        self.assertListEqual(sizes, [512, 4096])
