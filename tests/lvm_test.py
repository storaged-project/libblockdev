import _lvm_cases

from utils import TestTags, tag_test, required_plugins, fake_path

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev



@required_plugins(("lvm",))
class LvmTestCase(_lvm_cases.LvmTestCase):
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


class LvmNoDevTestCase(_lvm_cases.LvmNoDevTestCase, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmNoDevTestCase.setUpClass()
        LvmTestCase.setUpClass()

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

        with fake_path(all_but=("lvmconfig", "lvmdevices")):
            self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))

            # no lvmconfig tool available, should fail
            with self.assertRaises(GLib.GError):
                BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.CONFIG, 0)

            # no lvmdevices tool available, should fail
            with self.assertRaises(GLib.GError):
                BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.DEVICES, 0)


class LvmVDOTest(_lvm_cases.LvmVDOTest, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmVDOTest.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestPVs(_lvm_cases.LvmTestPVs, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPVs.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestPVmove(_lvm_cases.LvmTestPVmove, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPVmove.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestVGs(_lvm_cases.LvmTestVGs, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestVGs.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestLVs(_lvm_cases.LvmTestLVs, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestLVs.setUpClass()
        LvmTestCase.setUpClass()


class LvmCLITestLVcreateType(_lvm_cases.LvmPVVGLVTestCase, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmPVVGLVTestCase.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestPartialLVs(_lvm_cases.LvmTestPartialLVs, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPartialLVs.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestThpool(_lvm_cases.LvmTestThpool, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestThpool.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestThLV(_lvm_cases.LvmTestThLV, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestThLV.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestCache(_lvm_cases.LvmTestCache, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestCache.setUpClass()
        LvmTestCase.setUpClass()


class LvmTestDevicesFile(_lvm_cases.LvmTestDevicesFile, LvmTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestDevicesFile.setUpClass()
        LvmTestCase.setUpClass()

