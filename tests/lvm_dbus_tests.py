import unittest
from itertools import chain

import _lvm_cases

from utils import run_command, TestTags, tag_test, required_plugins

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '3.0')
from gi.repository import GLib, BlockDev

import dbus
sb = dbus.SystemBus()
lvm_dbus_running = any("lvmdbus" in name for name in chain(sb.list_names(), sb.list_activatable_names()))


@required_plugins(("lvm-dbus",))
@unittest.skipUnless(lvm_dbus_running, "LVM DBus not running")
class LVMDBusTestCase(_lvm_cases.LVMTestCase):
    test_type = "dbus"

    @classmethod
    def setUpClass(cls):
        ps = BlockDev.PluginSpec(name=BlockDev.Plugin.LVM, so_name="libbd_lvm-dbus.so.3")
        cls.requested_plugins = [ps]

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
                BlockDev.reinit(cls.requested_plugins, True, None)

        try:
            cls.devices_avail = BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.DEVICES, 0)
        except:
            cls.devices_avail = False


class LvmDBusNoDevTestCase(_lvm_cases.LvmNoDevTestCase, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmNoDevTestCase.setUpClass()
        LVMDBusTestCase.setUpClass()

    @tag_test(TestTags.NOSTORAGE)
    def test_plugin_version(self):
       self.assertEqual(BlockDev.get_plugin_soname(BlockDev.Plugin.LVM), "libbd_lvm-dbus.so.3")

    @tag_test(TestTags.NOSTORAGE)
    def test_tech_available(self):
        """Verify that checking lvm dbus availability by technology works as expected"""

        # stop the lvmdbusd service
        _ret, _out, _err = run_command("systemctl stop lvm2-lvmdbusd")

        # reinit libblockdev -- init checks are switched off so nothing should start the service
        self.assertTrue(BlockDev.reinit(self.requested_plugins, True, None))
        ret, _out, _err = run_command("systemctl status lvm2-lvmdbusd")
        self.assertNotEqual(ret, 0)

        # check tech availability -- service should be started
        succ = BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.BASIC, BlockDev.LVMTechMode.CREATE)
        self.assertTrue(succ)

        ret, _out, _err = run_command("systemctl status lvm2-lvmdbusd")
        self.assertEqual(ret, 0)

        # only query is supported with calcs
        with self.assertRaisesRegex(GLib.GError, "Only 'query' supported for thin calculations"):
            BlockDev.lvm_is_tech_avail(BlockDev.LVMTech.THIN_CALCS, BlockDev.LVMTechMode.CREATE)


class LVMVDOTest(_lvm_cases.LVMVDOTest, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LVMVDOTest.setUpClass()
        LVMDBusTestCase.setUpClass()

    @tag_test(TestTags.SLOW)
    def test_vdo_pool_convert(self):
        self.skipTest("LVM VDO pool convert not implemented in LVM DBus API.")


class LvmDBusTestPVs(_lvm_cases.LvmTestPVs, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPVs.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestVGs(_lvm_cases.LvmTestVGs, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestVGs.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestLVs(_lvm_cases.LvmTestLVs, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestLVs.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmDBusTestLVcreateType(_lvm_cases.LvmPVVGLVTestCase, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmPVVGLVTestCase.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestPartialLVs(_lvm_cases.LvmTestPartialLVs, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestPartialLVs.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestThpool(_lvm_cases.LvmTestThpool, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestThpool.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestThLV(_lvm_cases.LvmTestThLV, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestThLV.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestCache(_lvm_cases.LvmTestCache, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestCache.setUpClass()
        LVMDBusTestCase.setUpClass()


class LvmTestDevicesFile(_lvm_cases.LvmTestDevicesFile, LVMDBusTestCase):
    @classmethod
    def setUpClass(cls):
        _lvm_cases.LvmTestDevicesFile.setUpClass()
        LVMDBusTestCase.setUpClass()

