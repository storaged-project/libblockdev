import _lvm_cases

from utils import TestTags, tag_test, required_plugins, fake_path

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev



@required_plugins(("lvm",))
class LVMCLITestCase(_lvm_cases.LVMTestCase):
    test_type = "cli"

    @classmethod
    def setUpClass(cls):
        ps = BlockDev.PluginSpec(name=BlockDev.Plugin.LVM, so_name="libbd_lvm.so.3")
        cls.requested_plugins = [ps]

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

        try:
            cls.devices_avail = BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.DEVICES, 0)
        except:
            cls.devices_avail = False


class LvmNoDevTestCase(_lvm_cases.LvmNoDevTestCase, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmNoDevTestCase.setUpClass()
        LVMCLITestCase.setUpClass()

    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm.so.3")

    def test_tech_available(self):
        """Verify that checking lvm tool availability by technology works as expected"""

        with fake_path(all_but="lvm"):
            self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

            # no lvm tool available, should fail
            with self.assertRaises(GLib.GError):
                BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.BASIC, BlockDev.LVMTechMode.CREATE)

        # only query is support with calcs
        with self.assertRaisesRegex(GLib.GError, "Only 'query' supported for thin calculations"):
            BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.THIN_CALCS, BlockDev.LVMTechMode.CREATE)

        # lvm is available, should pass
        avail = BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.BASIC, BlockDev.LVMTechMode.CREATE)
        self.assertTrue(avail)


class LVMVDOTest(_lvm_cases.LVMVDOTest, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LVMVDOTest.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmCLITestPVs(_lvm_cases.LvmTestPVs, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPVs.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestVGs(_lvm_cases.LvmTestVGs, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestVGs.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestLVs(_lvm_cases.LvmTestLVs, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestLVs.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmCLITestLVcreateType(_lvm_cases.LvmPVVGLVTestCase, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmPVVGLVTestCase.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestPartialLVs(_lvm_cases.LvmTestPartialLVs, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPartialLVs.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestThpool(_lvm_cases.LvmTestThpool, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestThpool.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestThLV(_lvm_cases.LvmTestThLV, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestThLV.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestCache(_lvm_cases.LvmTestCache, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestCache.setUpClass()
        LVMCLITestCase.setUpClass()


class LvmTestDevicesFile(_lvm_cases.LvmTestDevicesFile, LVMCLITestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestDevicesFile.setUpClass()
        LVMCLITestCase.setUpClass()

