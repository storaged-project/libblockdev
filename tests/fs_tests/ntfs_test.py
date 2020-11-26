import tempfile
import six
import overrides_hack

from .fs_test import FSTestCase

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


class NTFSTestCase(FSTestCase):
    def setUp(self):
        if not self.ntfs_avail:
            self.skipTest("skipping NTFS: not available")

        super(NTFSTestCase, self).setUp()

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="ntfs_test")


class NTFSSetLabel(NTFSTestCase):
    def test_ntfs_set_label(self):
        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_ntfs_set_label(self.loop_dev, "TEST_LABEL")
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL")

        succ = BlockDev.fs_ntfs_set_label(self.loop_dev, "TEST_LABEL2")
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "TEST_LABEL2")

        succ = BlockDev.fs_ntfs_set_label(self.loop_dev, "")
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.label, "")

        succ = BlockDev.fs_ntfs_check_label("TEST_LABEL")
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, "at most 128 characters long."):
            BlockDev.fs_ntfs_check_label(129 * "a")


class NTFSSetUUID(NTFSTestCase):

    test_uuid = "54E1629A44FD724B"

    def test_ntfs_set_uuid(self):
        """Verify that it is possible to set UUID of an ntfs file system"""
        succ = BlockDev.fs_ntfs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        succ = BlockDev.fs_ntfs_set_uuid(self.loop_dev, self.test_uuid)
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertEqual(fi.uuid, self.test_uuid)

        # no uuid -> random
        succ = BlockDev.fs_ntfs_set_uuid(self.loop_dev, None)
        self.assertTrue(succ)
        fi = BlockDev.fs_ntfs_get_info(self.loop_dev)
        self.assertTrue(fi)
        self.assertNotEqual(fi.uuid, "")
        self.assertNotEqual(fi.uuid, self.test_uuid)

        succ = BlockDev.fs_ntfs_check_uuid(self.test_uuid)
        self.assertTrue(succ)

        with six.assertRaisesRegex(self, GLib.GError, "8 or 16 characters long"):
            BlockDev.fs_ntfs_check_uuid(9 * "a")

        with six.assertRaisesRegex(self, GLib.GError, "must be a hexadecimal number"):
            BlockDev.fs_ntfs_check_uuid(16 * "z")
