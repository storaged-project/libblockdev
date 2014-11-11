import unittest

from gi.repository import BlockDev
assert BlockDev.init(None, None)

class MDNoDevTestCase(unittest.TestCase):
    def test_get_superblock_size(self):
        """Verify that superblock size si calculated properly"""

        # 2 MiB for versions <= 1.0
        self.assertEqual(BlockDev.md_get_superblock_size(2 * 1024**3, "0.9"), 2 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(2 * 1024**3, "1.0"), 2 * 1024**2)

        # no version, "default" or > 1.0
        self.assertEqual(BlockDev.md_get_superblock_size(256 * 1024**3, None), 128 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(128 * 1024**3, None), 128 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(64 * 1024**3, "default"), 64 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(63 * 1024**3, "default"), 32 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(10 * 1024**3, "1.1"), 8 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(1 * 1024**3, "1.1"), 1 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(1023 * 1024**2, "1.2"), 1 * 1024**2)
        self.assertEqual(BlockDev.md_get_superblock_size(512 * 1024**2, "1.2"), 1 * 1024**2)

        # unsupported version -> default superblock size
        self.assertEqual(BlockDev.md_get_superblock_size(257 * 1024**2, version="unknown version"),
                         2 * 1024**2)
