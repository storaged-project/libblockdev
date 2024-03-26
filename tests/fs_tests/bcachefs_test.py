import os
import re
import subprocess
import unittest
import tempfile

from .fs_test import FSTestCase, FSNoDevTestCase, mounted

import overrides_hack
import utils
from utils import TestTags, tag_test

from gi.repository import BlockDev, GLib


def bcachefs_supported():
    """Alternative try to modinfo bcachefs?"""

    kernel_version = os.uname()[2]
    version_split = kernel_version.rsplit('.')
    major_ver, minor_ver = int(version_split[0]), int(version_split[1])
    if major_ver > 6:
        return True
    elif major_ver == 6 and minor_ver >= 7:
        return True

    return False


class BcachefsTestCase(FSTestCase):
    def setUp(self):
        if not self.bcachefs_avail:
            self.skipTest("skipping bcachefs: not available")

        if not bcachefs_supported():
            self.skipTest("skipping Bcachefs: kernel version not supported")

        self.mount_dir = tempfile.mkdtemp(prefix="libblockdev.", suffix="bcachefs_test")

        super(BcachefsTestCase, self).setUp()


class BcachefsNoDevTestCase(FSNoDevTestCase):
    def setUp(self):
        if not self.bcachefs_avail:
            self.skipTest("skipping Bcachefs: not available")

        if not bcachefs_supported():
            self.skipTest("skipping Bcachefs: kernel version not supported")

        super(BcachefsNoDevTestCase, self).setUp()


class BcachefsTestMkfs(BcachefsTestCase):
    def test_bcachefs_mkfs(self):
        """Verify that it is possible to create a new bcachefs file system"""

        with self.assertRaises(GLib.GError):
            BlockDev.fs_bcachefs_mkfs("/non/existing/device", None)

        succ = BlockDev.fs_bcachefs_mkfs(self.loop_dev, None)
        self.assertTrue(succ)

        # just try if we can mount the file system
        with mounted(self.loop_dev, self.mount_dir):
            pass

        # check the fstype
        fstype = BlockDev.fs_get_fstype(self.loop_dev)
        self.assertEqual(fstype, "bcachefs")

        BlockDev.fs_wipe(self.loop_dev, True)
