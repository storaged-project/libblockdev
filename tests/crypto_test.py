import unittest
import os
import tempfile
import overrides_hack
import shutil
import subprocess
import six
import locale
import re
import tarfile

from utils import create_sparse_tempfile, create_lio_device, delete_lio_device, get_avail_locales, requires_locales, run_command, read_file, TestTags, tag_test
from gi.repository import BlockDev, GLib

PASSWD = "myshinylittlepassword"
PASSWD2 = "myshinylittlepassword2"
PASSWD3 = "myshinylittlepassword3"


def check_cryptsetup_version(version):
    try:
        succ = BlockDev.utils_check_util_version("cryptsetup", version, "--version", r"cryptsetup ([0-9+\.]+)")
    except GLib.GError:
        return False
    else:
        return succ


HAVE_LUKS2 = check_cryptsetup_version("2.0.3")
HAVE_BITLK = check_cryptsetup_version("2.3.0")


class CryptoTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("crypto", "loop"))

    @classmethod
    def setUpClass(cls):
        unittest.TestCase.setUpClass()
        cls.avail_locales = get_avail_locales()

        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("crypto_test", 1024**3)
        self.dev_file2 = create_sparse_tempfile("crypto_test2", 1024**3)
        try:
            self.loop_dev = create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev2 = create_lio_device(self.dev_file2)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

        # make a key file
        handle, self.keyfile = tempfile.mkstemp(prefix="libblockdev_test_keyfile", text=False)
        os.write(handle, b"nobodyknows")
        os.close(handle)

    def _clean_up(self):
        try:
            BlockDev.crypto_luks_close("libblockdevTestLUKS")
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

        os.unlink(self.keyfile)

    def _luks_format(self, device, passphrase, keyfile):
        return BlockDev.crypto_luks_format(device, None, 0, passphrase, keyfile, 0)

    def _luks2_format(self, device, passphrase, keyfile):
        return BlockDev.crypto_luks_format(device, None, 0, passphrase, keyfile, 0, BlockDev.CryptoLUKSVersion.LUKS2, None)

class CryptoTestGenerateBackupPassphrase(CryptoTestCase):
    def setUp(self):
        # we don't need block devices for this test
        pass

    @tag_test(TestTags.NOSTORAGE)
    def test_generate_backup_passhprase(self):
        """Verify that backup passphrase generation works as expected"""

        exp = r"^([0-9A-Za-z./]{5}-){3}[0-9A-Za-z./]{5}$"
        for _i in range(100):
            bp = BlockDev.crypto_generate_backup_passphrase()
            six.assertRegex(self, bp, exp)

class CryptoTestFormat(CryptoTestCase):
    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_luks_format(self):
        """Verify that formating device as LUKS works"""

        # no passphrase nor keyfile
        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_format(self.loop_dev, None, 0, None, None, 0)

        # the simple case with password
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, PASSWD, None, 0)
        self.assertTrue(succ)

        # create with a keyfile
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0)
        self.assertTrue(succ)

        # the simple case with password blob
        succ = BlockDev.crypto_luks_format_blob(self.loop_dev, "aes-xts-plain64", 0, [ord(c) for c in PASSWD], 0)
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW, TestTags.CORE)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_format(self):
        """Verify that formating device as LUKS 2 works"""

        # no passphrase nor keyfile
        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_format(self.loop_dev, None, 0, None, None, 0)

        # the simple case with password
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, PASSWD, None, 0)
        self.assertTrue(succ)

        # create with a keyfile
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0)
        self.assertTrue(succ)

        # the simple case with password blob
        succ = BlockDev.crypto_luks_format_blob(self.loop_dev, "aes-xts-plain64", 0, [ord(c) for c in PASSWD], 0)
        self.assertTrue(succ)

        # simple case with extra options
        extra = BlockDev.CryptoLUKSExtra(label="blockdevLUKS")
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        _ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        m = re.search(r"Label:\s*(\S+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get label information from:\n%s %s" % (out, err))
        self.assertEqual(m.group(1), "blockdevLUKS")

        # different key derivation function
        pbkdf = BlockDev.CryptoLUKSPBKDF(type="pbkdf2")
        extra = BlockDev.CryptoLUKSExtra(pbkdf=pbkdf)
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        _ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        m = re.search(r"PBKDF:\s*(\S+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        self.assertEqual(m.group(1), "pbkdf2")

        # different options for argon2 -- all parameters set
        pbkdf = BlockDev.CryptoLUKSPBKDF(type="argon2id", max_memory_kb=100*1024, iterations=10, parallel_threads=1)
        extra = BlockDev.CryptoLUKSExtra(pbkdf=pbkdf)
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        _ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        m = re.search(r"PBKDF:\s*(\S+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        self.assertEqual(m.group(1), "argon2id")

        m = re.search(r"Memory:\s*(\d+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        # both iterations and memory is set --> cryptsetup will use exactly max_memory_kb
        self.assertEqual(int(m.group(1)), 100*1024)

        m = re.search(r"Threads:\s*(\d+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        self.assertEqual(int(m.group(1)), 1)

        m = re.search(r"Time cost:\s*(\d+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        self.assertEqual(int(m.group(1)), 10)

        # different options for argon2 -- only memory set
        pbkdf = BlockDev.CryptoLUKSPBKDF(max_memory_kb=100*1024)
        extra = BlockDev.CryptoLUKSExtra(pbkdf=pbkdf)
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        _ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        m = re.search(r"Memory:\s*(\d+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        # only memory is set -> cryptsetup will run a benchmark and use
        # at most max_memory_kb
        self.assertLessEqual(int(m.group(1)), 100*1024)

        # different options for argon2 -- only miterations set
        pbkdf = BlockDev.CryptoLUKSPBKDF(iterations=5)
        extra = BlockDev.CryptoLUKSExtra(pbkdf=pbkdf)
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, None, self.keyfile, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        _ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        m = re.search(r"Time cost:\s*(\d+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get pbkdf information from:\n%s %s" % (out, err))
        self.assertEqual(int(m.group(1)), 5)

    def _get_luks1_key_size(self, device):
        _ret, out, err = run_command("cryptsetup luksDump %s" % device)
        m = re.search(r"MK bits:\s*(\S+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get key size information from:\n%s %s" % (out, err))
        key_size = m.group(1)
        if not key_size.isnumeric():
            self.fail("Failed to get key size information from: %s" % key_size)
        return int(key_size)

    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_luks_format_key_size(self):
        """Verify that formating device as LUKS works"""

        # aes-xts: key size should default to 512
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 0, PASSWD, None, 0)
        self.assertTrue(succ)

        key_size = self._get_luks1_key_size(self.loop_dev)
        self.assertEqual(key_size, 512)

        # aes-cbc: key size should default to 256
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-cbc-essiv:sha256", 0, PASSWD, None, 0)
        self.assertTrue(succ)

        key_size = self._get_luks1_key_size(self.loop_dev)
        self.assertEqual(key_size, 256)

        # try specifying key size for aes-xts
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-xts-plain64", 256, PASSWD, None, 0)
        self.assertTrue(succ)

        key_size = self._get_luks1_key_size(self.loop_dev)
        self.assertEqual(key_size, 256)


class CryptoTestResize(CryptoTestCase):

    def _get_key_location(self, device):
        _ret, out, err = run_command("cryptsetup status %s" % device)
        m = re.search(r"\s*key location:\s*(\S+)\s*", out)
        if not m or len(m.groups()) != 1:
            self.fail("Failed to get key locaton from:\n%s %s" % (out, err))

        return m.group(1)

    @tag_test(TestTags.SLOW)
    def test_luks_resize(self):
        """Verify that resizing LUKS device works"""

        # the simple case with password
        succ = self._luks_format(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        # resize to 512 KiB (1024 * 512B sectors)
        succ = BlockDev.crypto_luks_resize("libblockdevTestLUKS", 1024)
        self.assertTrue(succ)

        # resize back to full size
        succ = BlockDev.crypto_luks_resize("libblockdevTestLUKS", 0)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_resize(self):
        """Verify that resizing LUKS 2 device works"""

        # the simple case with password
        succ = self._luks2_format(self.loop_dev, PASSWD, self.keyfile)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        # resize without passphrase should fail if key is saved in keyring
        if self._get_key_location("libblockdevTestLUKS") == "keyring":
            with self.assertRaises(GLib.GError):
                BlockDev.crypto_luks_resize("libblockdevTestLUKS", 1024)

        # resize to 512 KiB (1024 * 512B sectors)
        succ = BlockDev.crypto_luks_resize("libblockdevTestLUKS", 1024, PASSWD)
        self.assertTrue(succ)

        # resize back to full size (using the keyfile)
        succ = BlockDev.crypto_luks_resize("libblockdevTestLUKS", 0, None, self.keyfile)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

class CryptoTestOpenClose(CryptoTestCase):
    def _luks_open_close(self, create_fn):
        """Verify that opening/closing LUKS device works"""

        succ = create_fn(self.loop_dev, PASSWD, self.keyfile)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open("/non/existing/device", "libblockdevTestLUKS", PASSWD, None, False)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", None, None, False)

        with six.assertRaisesRegex(self, GLib.GError, r"Incorrect passphrase"):
            BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", "wrong-passhprase", None, False)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", None, "wrong-keyfile", False)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        # use the full /dev/mapper/ path
        succ = BlockDev.crypto_luks_close("/dev/mapper/libblockdevTestLUKS")
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", None, self.keyfile, False)
        self.assertTrue(succ)

        # use just the LUKS device name
        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_luks_open_close(self):
        self._luks_open_close(self._luks_format)

    @tag_test(TestTags.SLOW, TestTags.CORE)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_open_close(self):
        self._luks_open_close(self._luks2_format)

class CryptoTestAddKey(CryptoTestCase):
    def _add_key(self, create_fn):
        """Verify that adding key to LUKS device works"""

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_add_key(self.loop_dev, "wrong-passphrase", None, PASSWD2, None)

        succ = BlockDev.crypto_luks_add_key(self.loop_dev, PASSWD, None, PASSWD2, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_add_key_blob(self.loop_dev, [ord(c) for c in PASSWD2], [ord(c) for c in PASSWD3])
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_add_key(self):
        self._add_key(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_add_key(self):
        self._add_key(self._luks2_format)

class CryptoTestRemoveKey(CryptoTestCase):
    def _remove_key(self, create_fn):
        """Verify that removing key from LUKS device works"""

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_add_key(self.loop_dev, PASSWD, None, PASSWD2, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_add_key(self.loop_dev, PASSWD, None, PASSWD3, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_remove_key(self.loop_dev, "wrong-passphrase", None)

        succ = BlockDev.crypto_luks_remove_key(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_remove_key_blob(self.loop_dev, [ord(c) for c in PASSWD2])
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_remove_key(self):
        self._remove_key(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_remove_key(self):
        self._remove_key(self._luks2_format)

class CryptoTestErrorLocale(CryptoTestCase):
    def setUp(self):
        self._orig_loc = None
        CryptoTestCase.setUp(self)
        self._orig_loc = ".".join(locale.getdefaultlocale())

    def _clean_up(self):
        CryptoTestCase._clean_up(self)
        if self._orig_loc:
            locale.setlocale(locale.LC_ALL, self._orig_loc)

    @tag_test(TestTags.SLOW)
    @requires_locales({"cs_CZ.UTF-8"})
    def test_error_locale_key(self):
        """Verify that the error msg is locale agnostic"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None, 0)
        self.assertTrue(succ)

        locale.setlocale(locale.LC_ALL, "cs_CZ.UTF-8")
        try:
            BlockDev.crypto_luks_remove_key(self.loop_dev, "wrong-passphrase", None)
        except GLib.GError as e:
            self.assertIn("Operation not permitted", str(e))

class CryptoTestChangeKey(CryptoTestCase):
    def _change_key(self, create_fn):
        """Verify that changing key in LUKS device works"""

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, r"No keyslot with given passphrase found."):
            BlockDev.crypto_luks_change_key(self.loop_dev, "wrong-passphrase", PASSWD2)

        succ = BlockDev.crypto_luks_change_key(self.loop_dev, PASSWD, PASSWD2)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_change_key_blob(self.loop_dev, [ord(c) for c in PASSWD2], [ord(c) for c in PASSWD3])
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_change_key(self):
        self._change_key(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_change_key(self):
        self._change_key(self._luks2_format)

class CryptoTestIsLuks(CryptoTestCase):
    def _is_luks(self, create_fn):
        """Verify that LUKS device recognition works"""

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_device_is_luks("/non/existing/device")

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        is_luks = BlockDev.crypto_device_is_luks(self.loop_dev)
        self.assertTrue(is_luks)

        is_luks = BlockDev.crypto_device_is_luks(self.loop_dev2)
        self.assertFalse(is_luks)

    @tag_test(TestTags.SLOW)
    def test_is_luks(self):
        self._is_luks(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_is_luks2(self):
        self._is_luks(self._luks2_format)

class CryptoTestLuksStatus(CryptoTestCase):
    def _luks_status(self, create_fn):
        """Verify that LUKS device status reporting works"""

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_status("/non/existing/device")

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        # use the full /dev/mapper path
        status = BlockDev.crypto_luks_status("/dev/mapper/libblockdevTestLUKS")
        self.assertEqual(status, "active")

        # use just the LUKS device name
        status = BlockDev.crypto_luks_status("libblockdevTestLUKS")
        self.assertEqual(status, "active")

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_status("libblockdevTestLUKS")

    @tag_test(TestTags.SLOW)
    def test_luks_status(self):
        self._luks_status(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_status(self):
        self._luks_status(self._luks2_format)

class CryptoTestGetUUID(CryptoTestCase):
    def _get_uuid(self, create_fn):
        """Verify that getting LUKS device UUID works"""

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        uuid = BlockDev.crypto_luks_uuid(self.loop_dev)
        self.assertTrue(uuid)

        with self.assertRaises(GLib.GError):
            uuid = BlockDev.crypto_luks_uuid(self.loop_dev2)

    @tag_test(TestTags.SLOW)
    def test_luks_get_uuid(self):
        self._get_uuid(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_get_uuid(self):
        self._get_uuid(self._luks2_format)

class CryptoTestGetMetadataSize(CryptoTestCase):

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_get_metadata_size(self):
        """Verify that getting LUKS 2 device metadata size works"""

        succ = self._luks2_format(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        meta_size = BlockDev.crypto_luks_get_metadata_size(self.loop_dev)

        ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        if ret != 0:
            self.fail("Failed to get LUKS 2 header from %s:\n%s %s" % (self.loop_dev, out, err))

        m = re.search(r"offset:\s*([0-9]+)\s*\[bytes\]", out)
        if m is None:
            self.fail("Failed to get LUKS 2 offset information from %s:\n%s %s" % (self.loop_dev, out, err))
        offset = int(m.group(1))
        self.assertEquals(meta_size, offset, "LUKS 2 metadata sizes differ")

    @tag_test(TestTags.SLOW)
    def test_luks_get_metadata_size(self):
        """Verify that getting LUKS device metadata size works"""

        succ = self._luks_format(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        meta_size = BlockDev.crypto_luks_get_metadata_size(self.loop_dev)

        ret, out, err = run_command("cryptsetup luksDump %s" % self.loop_dev)
        if ret != 0:
            self.fail("Failed to get LUKS header from %s:\n%s %s" % (self.loop_dev, out, err))

        m = re.search(r"Payload offset:\s*([0-9]+)", out)
        if m is None:
            self.fail("Failed to get LUKS 2 offset information from %s:\n%s %s" % (self.loop_dev, out, err))
        # offset value is in 512B blocks; we need to multiply to get the real metadata size
        offset = int(m.group(1)) * 512

        self.assertEquals(meta_size, offset, "LUKS metadata sizes differ")

class CryptoTestLuksOpenRW(CryptoTestCase):
    def _luks_open_rw(self, create_fn):
        """Verify that a LUKS device can be activated as RW as well as RO"""

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        # tests that we can write something to the raw LUKS device
        succ = BlockDev.utils_exec_and_report_error(["dd", "if=/dev/zero", "of=/dev/mapper/libblockdevTestLUKS", "bs=1M", "count=1"])
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

        # now try the same with LUKS device opened as RO
        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, True)
        self.assertTrue(succ)

        # tests that we can write something to the raw LUKS device
        with self.assertRaises(GLib.GError):
            BlockDev.utils_exec_and_report_error(["dd", "if=/dev/zero", "of=/dev/mapper/libblockdevTestLUKS", "bs=1M", "count=1"])

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_open_rw(self):
        self._luks_open_rw(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_open_rw(self):
        self._luks_open_rw(self._luks2_format)

class CryptoTestEscrow(CryptoTestCase):
    def setUp(self):
        super(CryptoTestEscrow, self).setUp()

        # Create the certificate used to encrypt the escrow packet and backup passphrase.
        # volume_key requires a nss database directory to decrypt any of the
        # packet files, and python-nss is python2 only, just do everything with
        # shell commands.

        self.nss_dir = tempfile.mkdtemp(prefix='libblockdev_test_escrow')
        self.addCleanup(shutil.rmtree, self.nss_dir)
        subprocess.check_call(['certutil', '-d', self.nss_dir, '--empty-password', '-N'])

        # Gather some entropy to keep certutil from asking for input
        with tempfile.NamedTemporaryFile() as noise_file:
            noise_file.write(os.urandom(20))
            noise_file.flush()

            subprocess.check_call(['certutil', '-d', self.nss_dir, '-S', '-x', '-n',
                'escrow_cert', '-s', 'CN=Escrow Test', '-t', ',,TC', '-z',
                noise_file.name])

        # Export the public certificate
        handle, self.public_cert = tempfile.mkstemp(prefix='libblockdev_test_escrow')

        os.close(handle)
        subprocess.check_call(['certutil', '-d', self.nss_dir, '-L', '-n', 'escrow_cert',
            '-a', '-o', self.public_cert])
        self.addCleanup(os.unlink, self.public_cert)

    @tag_test(TestTags.SLOW)
    def test_escrow_packet(self):
        """Verify that an escrow packet can be created for a device"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None, 0)
        self.assertTrue(succ)

        escrow_dir = tempfile.mkdtemp(prefix='libblockdev_test_escrow')
        self.addCleanup(shutil.rmtree, escrow_dir)
        with open(self.public_cert, 'rb') as cert_file:
            succ = BlockDev.crypto_escrow_device(self.loop_dev, PASSWD, cert_file.read(),
                    escrow_dir, None)
        self.assertTrue(succ)

        # Find the escrow packet
        escrow_packet_file = '%s/%s-escrow' % (escrow_dir, BlockDev.crypto_luks_uuid(self.loop_dev))
        self.assertTrue(os.path.isfile(escrow_packet_file))

        # Use the volume_key utility (see note in setUp about why not python)
        # to decrypt the escrow packet and restore access to the volume under
        # a new passphrase

        # Just use the existing temp directory to output the re-encrypted packet
        # PASSWD2 is the passphrase of the new escrow packet
        p = subprocess.Popen(['volume_key', '--reencrypt', '-b', '-d', self.nss_dir,
            escrow_packet_file, '-o', '%s/escrow-out' % escrow_dir],
            stdin=subprocess.PIPE)
        p.communicate(input=('%s\0%s\0' % (PASSWD2, PASSWD2)).encode('utf-8'))
        if p.returncode != 0:
            raise subprocess.CalledProcessError(p.returncode, 'volume_key'
)
        # Restore access to the volume
        # PASSWD3 is the new passphrase for the LUKS device
        p = subprocess.Popen(['volume_key', '--restore', '-b', self.loop_dev,
            '%s/escrow-out' % escrow_dir], stdin=subprocess.PIPE)
        p.communicate(input=('%s\0%s\0%s\0' % (PASSWD2, PASSWD3, PASSWD3)).encode('utf-8'))
        if p.returncode != 0:
            raise subprocess.CalledProcessError(p.returncode, 'volume_key')

        # Open the volume with the new passphrase
        succ = BlockDev.crypto_luks_open(self.loop_dev, 'libblockdevTestLUKS', PASSWD3, None)
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_backup_passphrase(self):
        """Verify that a backup passphrase can be created for a device"""
        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None, 0)
        self.assertTrue(succ)

        escrow_dir = tempfile.mkdtemp(prefix='libblockdev_test_escrow')
        self.addCleanup(shutil.rmtree, escrow_dir)
        backup_passphrase = BlockDev.crypto_generate_backup_passphrase()
        with open(self.public_cert, 'rb') as cert_file:
            succ = BlockDev.crypto_escrow_device(self.loop_dev, PASSWD, cert_file.read(),
                    escrow_dir, backup_passphrase)
        self.assertTrue(succ)

        # Find the backup passphrase
        escrow_backup_passphrase = "%s/%s-escrow-backup-passphrase" % (escrow_dir, BlockDev.crypto_luks_uuid(self.loop_dev))
        self.assertTrue(os.path.isfile(escrow_backup_passphrase))

        # Check that the encrypted file contains what we put in
        env = {k: v for k, v in os.environ.items()}
        env.update({"LC_ALL": "C"})
        passphrase = subprocess.check_output(
                ['volume_key', '--secrets', '-d', self.nss_dir, escrow_backup_passphrase],
                env=env)
        passphrase = passphrase.strip().split()[1].decode('ascii')
        self.assertEqual(passphrase, backup_passphrase)

        # Check that the backup passphrase works
        succ = BlockDev.crypto_luks_open(self.loop_dev, 'libblockdevTestLUKS', backup_passphrase, None)
        self.assertTrue(succ)

class CryptoTestSuspendResume(CryptoTestCase):
    def _luks_suspend_resume(self, create_fn):

        succ = create_fn(self.loop_dev, PASSWD, self.keyfile)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_suspend("/non/existing/device")

        # use the full /dev/mapper/ path
        succ = BlockDev.crypto_luks_suspend("/dev/mapper/libblockdevTestLUKS")
        self.assertTrue(succ)

        _ret, state, _err = run_command("lsblk -oSTATE -n /dev/mapper/libblockdevTestLUKS")
        self.assertEqual(state, "suspended")

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_resume("libblockdevTestLUKS", None, None)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_resume("libblockdevTestLUKS", "wrong-passhprase", None)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_resume("libblockdevTestLUKS", None, "wrong-keyfile")

        succ = BlockDev.crypto_luks_resume("libblockdevTestLUKS", PASSWD, None)
        self.assertTrue(succ)

        _ret, state, _err = run_command("lsblk -oSTATE -n /dev/mapper/libblockdevTestLUKS")
        self.assertEqual(state, "running")

        # use just the LUKS device name
        succ = BlockDev.crypto_luks_suspend("libblockdevTestLUKS")
        self.assertTrue(succ)

        _ret, state, _err = run_command("lsblk -oSTATE -n /dev/mapper/libblockdevTestLUKS")
        self.assertEqual(state, "suspended")

        succ = BlockDev.crypto_luks_resume("libblockdevTestLUKS", None, self.keyfile)
        self.assertTrue(succ)

        _ret, state, _err = run_command("lsblk -oSTATE -n /dev/mapper/libblockdevTestLUKS")
        self.assertEqual(state, "running")

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_suspend_resume(self):
        """Verify that suspending/resuming LUKS device works"""
        self._luks_suspend_resume(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_suspend_resume(self):
        """Verify that suspending/resuming LUKS 2 device works"""
        self._luks_suspend_resume(self._luks2_format)

class CryptoTestKillSlot(CryptoTestCase):
    def _luks_kill_slot(self, create_fn):

        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_add_key(self.loop_dev, PASSWD, None, PASSWD2, None)
        self.assertTrue(succ)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_kill_slot("/non/existing/device", -1)

        # invalid slot
        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_kill_slot(self.loop_dev, -1)

        # unused slot
        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_kill_slot(self.loop_dev, 2)

        # destroy second keyslot
        succ = BlockDev.crypto_luks_kill_slot(self.loop_dev, 1)
        self.assertTrue(succ)

        # opening with the second passphrase should fail
        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD2)

        # opening with passphrase should still work
        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_kill_slot(self):
        """Verify that killing a key slot on LUKS device works"""
        self._luks_kill_slot(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_kill_slot(self):
        """Verify that killing a key slot on LUKS 2 device works"""
        self._luks_kill_slot(self._luks2_format)

class CryptoTestHeaderBackupRestore(CryptoTestCase):

    def setUp(self):
        super(CryptoTestHeaderBackupRestore, self).setUp()

        self.backup_dir = tempfile.mkdtemp(prefix='libblockdev_test_header')
        self.addCleanup(shutil.rmtree, self.backup_dir)

    def _luks_header_backup_restore(self, create_fn):
        succ = create_fn(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

        backup_file = os.path.join(self.backup_dir, "luks-header.txt")

        succ = BlockDev.crypto_luks_header_backup(self.loop_dev, backup_file)
        self.assertTrue(succ)
        self.assertTrue(os.path.isfile(backup_file))

        # now completely destroy the luks header
        ret, out, err = run_command("cryptsetup erase %s -q && wipefs -a %s" % (self.loop_dev, self.loop_dev))
        if ret != 0:
            self.fail("Failed to erase LUKS header from %s:\n%s %s" % (self.loop_dev, out, err))

        _ret, fstype, _err = run_command("blkid -p -ovalue -sTYPE %s" % self.loop_dev)
        self.assertFalse(fstype)  # false == empty

        # header is destroyed, should not be possible to open
        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None)

        # and restore the header back
        succ = BlockDev.crypto_luks_header_restore(self.loop_dev, backup_file)
        self.assertTrue(succ)

        _ret, fstype, _err = run_command("blkid -p -ovalue -sTYPE %s" % self.loop_dev)
        self.assertEqual(fstype, "crypto_LUKS")

        # opening should now work
        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW)
    def test_luks_header_backup_restore(self):
        """Verify that header backup/restore with LUKS works"""
        self._luks_header_backup_restore(self._luks_format)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_header_backup_restore(self):
        """Verify that header backup/restore with LUKS2 works"""
        self._luks_header_backup_restore(self._luks2_format)

class CryptoTestInfo(CryptoTestCase):
    @tag_test(TestTags.SLOW, TestTags.CORE)
    def test_luks_format(self):
        """Verify that we can get information about a LUKS device"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-cbc-essiv:sha256", 256, PASSWD, None, 0)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        info = BlockDev.crypto_luks_info("libblockdevTestLUKS")
        self.assertIsNotNone(info)

        self.assertEqual(info.version, BlockDev.CryptoLUKSVersion.LUKS1)
        self.assertEqual(info.cipher, "aes")
        self.assertEqual(info.mode, "cbc-essiv:sha256")
        self.assertEqual(info.backing_device, self.loop_dev)

        _ret, uuid, _err = run_command("blkid -p -ovalue -sUUID %s" % self.loop_dev)
        self.assertEqual(info.uuid, uuid)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

    @tag_test(TestTags.SLOW, TestTags.CORE)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_format(self):
        """Verify that we can get information about a LUKS 2 device"""

        extra = BlockDev.CryptoLUKSExtra()
        extra.sector_size = 4096

        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-cbc-essiv:sha256", 256, PASSWD, None, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        info = BlockDev.crypto_luks_info("libblockdevTestLUKS")
        self.assertIsNotNone(info)

        self.assertEqual(info.version, BlockDev.CryptoLUKSVersion.LUKS2)
        self.assertEqual(info.cipher, "aes")
        self.assertEqual(info.mode, "cbc-essiv:sha256")
        self.assertEqual(info.backing_device, self.loop_dev)
        self.assertEqual(info.sector_size, 4096)

        _ret, uuid, _err = run_command("blkid -p -ovalue -sUUID %s" % self.loop_dev)
        self.assertEqual(info.uuid, uuid)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)


class CryptoTestLuksSectorSize(CryptoTestCase):
    def setUp(self):
        if not check_cryptsetup_version("2.4.0"):
            self.skipTest("cryptsetup encryption sector size not available, skipping.")

        # we need a loop devices for this test case
        self.addCleanup(self._clean_up)
        self.dev_file = create_sparse_tempfile("crypto_test", 1024**3)
        self.dev_file2 = create_sparse_tempfile("crypto_test", 1024**3)

        succ, loop = BlockDev.loop_setup(self.dev_file)
        if not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

        succ, loop = BlockDev.loop_setup(self.dev_file)
        if not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop

        # set sector size of the loop device to 4k
        ret, _out, _err = run_command("losetup --sector-size 4096 %s" % self.loop_dev)
        self.assertEqual(ret, 0)

    def _clean_up(self):
        try:
            BlockDev.crypto_luks_close("libblockdevTestLUKS")
        except:
            pass

        BlockDev.loop_teardown(self.loop_dev)
        os.unlink(self.dev_file)

        BlockDev.loop_teardown(self.loop_dev2)
        os.unlink(self.dev_file2)

    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_sector_size_autodetect(self):
        """Verify that we can autodetect 4k drives and set 4k sector size for them"""

        # format the 4k loop device, encryption sector size should default to 4096
        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-cbc-essiv:sha256", 256, PASSWD, None, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        info = BlockDev.crypto_luks_info("libblockdevTestLUKS")
        self.assertIsNotNone(info)

        self.assertEqual(info.version, BlockDev.CryptoLUKSVersion.LUKS2)
        self.assertEqual(info.sector_size, 4096)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)

        # with the 512 loop device, we should still get 512
        succ = BlockDev.crypto_luks_format(self.loop_dev2, "aes-cbc-essiv:sha256", 256, PASSWD, None, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev2, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        info = BlockDev.crypto_luks_info("libblockdevTestLUKS")
        self.assertIsNotNone(info)

        self.assertEqual(info.version, BlockDev.CryptoLUKSVersion.LUKS2)
        self.assertEqual(info.sector_size, 512)

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)


class CryptoTestIntegrity(CryptoTestCase):
    @tag_test(TestTags.SLOW)
    @unittest.skipUnless(HAVE_LUKS2, "LUKS 2 not supported")
    def test_luks2_integrity(self):
        """Verify that we can get create a LUKS 2 device with integrity"""

        if not BlockDev.utils_have_kernel_module("dm-integrity"):
            self.skipTest('dm-integrity kernel module not available, skipping.')

        extra = BlockDev.CryptoLUKSExtra()
        extra.integrity = "hmac(sha256)"

        succ = BlockDev.crypto_luks_format(self.loop_dev, "aes-cbc-essiv:sha256", 512, PASSWD, None, 0,
                                           BlockDev.CryptoLUKSVersion.LUKS2, extra)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None, False)
        self.assertTrue(succ)

        info = BlockDev.crypto_integrity_info("libblockdevTestLUKS")
        self.assertIsNotNone(info)

        self.assertEqual(info.algorithm, "hmac(sha256)")

        # get integrity device dm name
        _ret, int_name, _err = run_command('ls /sys/block/%s/holders/' % self.loop_dev.split("/")[-1])
        self.assertTrue(int_name)  # true == not empty

        tag_size = read_file("/sys/block/%s/integrity/tag_size" % int_name)
        self.assertEqual(info.tag_size, int(tag_size))

        succ = BlockDev.crypto_luks_close("libblockdevTestLUKS")
        self.assertTrue(succ)


class CryptoTestTrueCrypt(CryptoTestCase):

    # we can't create TrueCrypt/VeraCrypt formats using libblockdev
    # so we are using these images from cryptsetup test suite
    # https://gitlab.com/cryptsetup/cryptsetup/blob/master/tests/tcrypt-images.tar.bz2
    tc_img = "tc-sha512-xts-aes"
    vc_img = "vc-sha512-xts-aes"
    passphrase = "aaaaaaaaaaaa"
    tempdir = None

    @classmethod
    def setUpClass(cls):
        super(CryptoTestTrueCrypt, cls).setUpClass()
        cls.tempdir = tempfile.mkdtemp(prefix="bd_test_tcrypt")
        images = os.path.join(os.path.dirname(__file__), "truecrypt-images.tar.gz")
        with tarfile.open(images, "r") as tar:
            tar.extractall(cls.tempdir)

    @classmethod
    def tearDownClass(cls):
        super(CryptoTestTrueCrypt, cls).tearDownClass()
        shutil.rmtree(cls.tempdir)

    def setUp(self):
        self.addCleanup(self._clean_up)

        succ, loop = BlockDev.loop_setup(os.path.join(self.tempdir, self.tc_img))
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.tc_dev = "/dev/%s" % loop
        succ, loop = BlockDev.loop_setup(os.path.join(self.tempdir, self.vc_img))
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.vc_dev = "/dev/%s" % loop

    def _clean_up(self):
        try:
            BlockDev.crypto_tc_close("libblockdevTestTC")
        except:
            pass

        succ = BlockDev.loop_teardown(self.tc_dev)
        if not succ:
            raise RuntimeError("Failed to tear down loop device used for testing")

        succ = BlockDev.loop_teardown(self.vc_dev)
        if not succ:
            raise RuntimeError("Failed to tear down loop device used for testing")

    @tag_test(TestTags.NOSTORAGE)
    def test_truecrypt_open_close(self):
        """Verify that opening/closing TrueCrypt device works"""

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open("/non/existing/device", "libblockdevTestTC", self.passphrase)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.tc_dev, "libblockdevTestTC", None)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.tc_dev, "libblockdevTestTC", "wrong-passhprase")

        succ = BlockDev.crypto_tc_open(self.tc_dev, "libblockdevTestTC", self.passphrase)
        self.assertTrue(succ)
        self.assertTrue(os.path.exists("/dev/mapper/libblockdevTestTC"))

        succ = BlockDev.crypto_tc_close("libblockdevTestTC")
        self.assertTrue(succ)
        self.assertFalse(os.path.exists("/dev/mapper/libblockdevTestTC"))

    @tag_test(TestTags.NOSTORAGE)
    def test_veracrypt_open_close(self):
        """Verify that opening/closing VeraCrypt device works"""

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open("/non/existing/device", "libblockdevTestTC", self.passphrase)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.vc_dev, "libblockdevTestTC", None)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.vc_dev, "libblockdevTestTC", "wrong-passhprase")

        succ = BlockDev.crypto_tc_open(self.vc_dev, "libblockdevTestTC", self.passphrase, veracrypt=True)
        self.assertTrue(succ)
        self.assertTrue(os.path.exists("/dev/mapper/libblockdevTestTC"))

        succ = BlockDev.crypto_tc_close("libblockdevTestTC")
        self.assertTrue(succ)
        self.assertFalse(os.path.exists("/dev/mapper/libblockdevTestTC"))


class CryptoTestBitlk(CryptoTestCase):

    # we can't create BitLocker formats using libblockdev
    # so we are using these images from cryptsetup test suite
    # https://gitlab.com/cryptsetup/cryptsetup/blob/master/tests/bitlk-images.tar.xz
    bitlk_img = "bitlk-aes-xts-128.img"
    passphrase = "anaconda"
    tempdir = None

    @classmethod
    def setUpClass(cls):
        super(CryptoTestBitlk, cls).setUpClass()
        cls.tempdir = tempfile.mkdtemp(prefix="bd_test_bitlk")
        images = os.path.join(os.path.dirname(__file__), "bitlk-images.tar.gz")
        with tarfile.open(images, "r") as tar:
            tar.extractall(cls.tempdir)

    @classmethod
    def tearDownClass(cls):
        super(CryptoTestBitlk, cls).tearDownClass()
        shutil.rmtree(cls.tempdir)

    def setUp(self):
        self.addCleanup(self._clean_up)

        succ, loop = BlockDev.loop_setup(os.path.join(self.tempdir, self.bitlk_img))
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.bitlk_dev = "/dev/%s" % loop

    def _clean_up(self):
        try:
            BlockDev.crypto_bitlk_close("libblockdevTestBitlk")
        except:
            pass

        succ = BlockDev.loop_teardown(self.bitlk_dev)
        if not succ:
            raise RuntimeError("Failed to tear down loop device used for testing")

    @unittest.skipUnless(HAVE_BITLK, "BITLK not supported")
    def test_bitlk_open_close(self):
        """Verify that opening/closing a BitLocker device works"""

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open("/non/existing/device", "libblockdevTestBitlk", self.passphrase)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.bitlk_dev, "libblockdevTestBitlk", None)

        with self.assertRaises(GLib.GError):
            BlockDev.crypto_luks_open(self.bitlk_dev, "libblockdevTestBitlk", "wrong-passhprase")

        succ = BlockDev.crypto_bitlk_open(self.bitlk_dev, "libblockdevTestBitlk", self.passphrase)
        self.assertTrue(succ)
        self.assertTrue(os.path.exists("/dev/mapper/libblockdevTestBitlk"))

        succ = BlockDev.crypto_bitlk_close("libblockdevTestBitlk")
        self.assertTrue(succ)
        self.assertFalse(os.path.exists("/dev/mapper/libblockdevTestBitlk"))
