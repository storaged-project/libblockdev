import os
import subprocess
import unittest

from contextlib import contextmanager

import utils
import overrides_hack

from gi.repository import BlockDev


@contextmanager
def mounted(device, where, ro=False):
    utils.mount(device, where, ro)
    yield
    utils.umount(where)


def check_output(args, ignore_retcode=True):
    """Just like subprocess.check_output(), but allows the return code of the process to be ignored"""

    try:
        return subprocess.check_output(args)
    except subprocess.CalledProcessError as e:
        if ignore_retcode:
            return e.output
        else:
            raise


class FSTestCase(unittest.TestCase):

    requested_plugins = BlockDev.plugin_specs_from_names(("fs", "loop"))
    loop_size = 150 * 1024**2

    @classmethod
    def setUpClass(cls):
        BlockDev.switch_init_checks(False)
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)
        BlockDev.switch_init_checks(True)

        try:
            cls.ntfs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.NTFS,
                                                       BlockDev.FSTechMode.MKFS |
                                                       BlockDev.FSTechMode.RESIZE |
                                                       BlockDev.FSTechMode.REPAIR |
                                                       BlockDev.FSTechMode.CHECK |
                                                       BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.ntfs_avail = False

        try:
            # check only for mkfs, we have special checks for resize and check
            cls.f2fs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.F2FS, BlockDev.FSTechMode.MKFS)
        except:
            cls.f2fs_avail = False

        try:
            cls.reiserfs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.REISERFS,
                                                           BlockDev.FSTechMode.MKFS |
                                                           BlockDev.FSTechMode.RESIZE |
                                                           BlockDev.FSTechMode.REPAIR |
                                                           BlockDev.FSTechMode.CHECK |
                                                           BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.reiserfs_avail = False

        try:
            cls.nilfs2_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.NILFS2,
                                                         BlockDev.FSTechMode.MKFS |
                                                         BlockDev.FSTechMode.RESIZE |
                                                         BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.nilfs2_avail = False

        try:
            cls.exfat_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.EXFAT,
                                                        BlockDev.FSTechMode.MKFS |
                                                        BlockDev.FSTechMode.REPAIR |
                                                        BlockDev.FSTechMode.CHECK |
                                                        BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.exfat_avail = False

        try:
            cls.btrfs_avail = BlockDev.fs_is_tech_avail(BlockDev.FSTech.BTRFS,
                                                        BlockDev.FSTechMode.MKFS |
                                                        BlockDev.FSTechMode.RESIZE |
                                                        BlockDev.FSTechMode.REPAIR |
                                                        BlockDev.FSTechMode.CHECK |
                                                        BlockDev.FSTechMode.SET_LABEL)
        except:
            cls.btrfs_avail = False

    def setUp(self):
        self.addCleanup(self._clean_up)
        self.dev_file = utils.create_sparse_tempfile("fs_test", self.loop_size)
        self.dev_file2 = utils.create_sparse_tempfile("fs_test", self.loop_size)
        try:
            self.loop_dev = utils.create_lio_device(self.dev_file)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)
        try:
            self.loop_dev2 = utils.create_lio_device(self.dev_file2)
        except RuntimeError as e:
            raise RuntimeError("Failed to setup loop device for testing: %s" % e)

    def _clean_up(self):
        try:
            utils.delete_lio_device(self.loop_dev)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file)

        try:
            utils.delete_lio_device(self.loop_dev2)
        except RuntimeError:
            # just move on, we can do no better here
            pass
        os.unlink(self.dev_file2)

        try:
            utils.umount(self.mount_dir)
        except:
            pass

    def setro(self, device):
        ret, _out, _err = utils.run_command("blockdev --setro %s" % device)
        if ret != 0:
            self.fail("Failed to set %s read-only" % device)

    def setrw(self, device):
        ret, _out, _err = utils.run_command("blockdev --setrw %s" % device)
        if ret != 0:
            self.fail("Failed to set %s read-write" % device)
