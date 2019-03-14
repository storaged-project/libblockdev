import unittest
import os
import overrides_hack

from utils import fake_path, TestTags, tag_test
from gi.repository import BlockDev, GLib

@unittest.skipUnless(os.uname()[4].startswith('s390'), "s390x architecture required")
class S390TestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("s390",))

    @classmethod
    def setUpClass(cls):

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    @tag_test(TestTags.EXTRADEPS, TestTags.NOSTORAGE)
    def test_device_input(self):
        """Verify that s390_sanitize_dev_input works as expected"""
        dev = "1234"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), '0.0.' + dev)

        dev = "123456"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), '0.0.' + dev)

        # the device number is padded on the left with 0s up to 4 digits
        dev = "123.abc"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), "0.0.0abc")
        dev = "abc"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), "0.0.0abc")
        dev = ".abc"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), "0.0.0abc")

        # a complete number is unchanged
        dev = "0.0.abcd"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), dev)

        # a too long number doesn't mean a crash no matter if it makes sense
        dev = "0.0.abcdefgh"
        self.assertEqual(BlockDev.s390_sanitize_dev_input(dev), dev)

    @tag_test(TestTags.EXTRADEPS, TestTags.NOSTORAGE)
    def test_wwpn_input(self):
        """Verify that s390_zfcp_sanitize_wwpn_input works as expected"""
        # missing "0x" from beginning of wwpn; this should be added by fx
        wwpn = "01234567abcdefab"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_wwpn_input(wwpn), "0x01234567abcdefab")
        # this should be fine as-is
        wwpn = "0x01234567abcdefab"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_wwpn_input(wwpn), wwpn)

        # too short
        wwpn = "a"
        with self.assertRaises(GLib.GError):
            BlockDev.s390_zfcp_sanitize_wwpn_input(wwpn)

    @tag_test(TestTags.EXTRADEPS, TestTags.NOSTORAGE)
    def test_lun_input(self):
        """Verify that s390_zfcp_sanitize_lun_input works as expected"""
        # user does not prepend lun with "0x"; this should get added
        lun = "01234567abcdefab"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_lun_input(lun), "0x01234567abcdefab")
        # a user enters a lun that is between 0 and 16 chars long (non-inclusive); 0 padding should be added to expand to 16
        lun = "0x123"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_lun_input(lun), "0x0123000000000000")
        lun = "0x12345"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_lun_input(lun), "0x1234500000000000")
        lun = "0x123456"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_lun_input(lun), "0x1234560000000000")
        # this should be fine as-is
        lun = "0x1234567800000000"
        self.assertEqual(BlockDev.s390_zfcp_sanitize_lun_input(lun), lun)

        # too long
        lun = "12345678901234567890"
        with self.assertRaises(GLib.GError):
            BlockDev.s390_zfcp_sanitize_lun_input(lun)


@unittest.skipUnless(os.uname()[4].startswith('s390'), "s390x architecture required")
class S390UnloadTest(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("s390",))

    @classmethod
    def setUpClass(cls):

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    @tag_test(TestTags.EXTRADEPS, TestTags.NOSTORAGE)
    def test_check_no_dasdfmt(self):
        """Verify that checking dasdfmt tool availability works as expected"""

        # unload all plugins first
        self.assertTrue(BlockDev.reinit([], True, None))

        with fake_path(all_but="dasdfmt"):
            # dasdfmt is not available, so the s390 plugin should fail to load
            with self.assertRaises(GLib.GError):
                BlockDev.reinit(self.requested_plugins, True, None)

            self.assertNotIn("s390", BlockDev.get_available_plugin_names())

    def setUp(self):
        # make sure the library is initialized with all plugins loaded for other
        # tests
        self.addCleanup(BlockDev.reinit, self.requested_plugins, True, None)
