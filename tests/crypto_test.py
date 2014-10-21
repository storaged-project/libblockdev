import unittest
import os

from utils import create_sparse_tempfile
from gi.repository import BlockDev, GLib
assert BlockDev.init(None, None)

PASSWD = "myshinylittlepassword"
PASSWD2 = "myshinylittlepassword2"

class CryptoTestCase(unittest.TestCase):
    def setUp(self):
        self.dev_file = create_sparse_tempfile("crypto_test", 1024**3)
        self.dev_file2 = create_sparse_tempfile("crypto_test2", 1024**3)
        succ, loop = BlockDev.loop_setup(self.dev_file)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev = "/dev/%s" % loop

        succ, loop = BlockDev.loop_setup(self.dev_file2)
        if  not succ:
            raise RuntimeError("Failed to setup loop device for testing")
        self.loop_dev2 = "/dev/%s" % loop


    def tearDown(self):
        succ = BlockDev.loop_teardown(self.loop_dev)
        if  not succ:
            os.unlink(self.dev_file)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file)

        succ = BlockDev.loop_teardown(self.loop_dev2)
        if  not succ:
            os.unlink(self.dev_file2)
            raise RuntimeError("Failed to tear down loop device used for testing")

        os.unlink(self.dev_file2)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_format(self):
        """Verify that formating device as LUKS works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_luks_open_close(self):
        """Verify that opening/closing LUKS device works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_open(self.loop_dev, "libblockdevTestLUKS", PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_close("/dev/mapper/libblockdevTestLUKS")
        self.assertTrue(succ)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_add_key(self):
        """Verify that adding key to LUKS device works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_add_key(self.loop_dev, PASSWD, None, PASSWD2, None)
        self.assertTrue(succ)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_remove_key(self):
        """Verify that removing key from LUKS device works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_add_key(self.loop_dev, PASSWD, None, PASSWD2, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_remove_key(self.loop_dev, PASSWD, None)
        self.assertTrue(succ)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_change_key(self):
        """Verify that changing key in LUKS device works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

        succ = BlockDev.crypto_luks_change_key(self.loop_dev, PASSWD, PASSWD2)
        self.assertTrue(succ)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_is_luks(self):
        """Verify that LUKS device recognition works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

        is_luks = BlockDev.crypto_device_is_luks(self.loop_dev)
        self.assertTrue(is_luks)

        is_luks = BlockDev.crypto_device_is_luks(self.loop_dev2)
        self.assertFalse(is_luks)

    @unittest.skipIf("SKIP_SLOW" in os.environ, "skipping slow tests")
    def test_get_uuid(self):
        """Verify that getting LUKS device UUID works"""

        succ = BlockDev.crypto_luks_format(self.loop_dev, None, 0, PASSWD, None)
        self.assertTrue(succ)

        uuid = BlockDev.crypto_luks_uuid(self.loop_dev)
        self.assertTrue(uuid)

        with self.assertRaises(GLib.GError):
            uuid = BlockDev.crypto_luks_uuid(self.loop_dev2)
