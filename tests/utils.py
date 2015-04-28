import os
import tempfile
from contextlib import contextmanager
from itertools import chain

from gi.repository import GLib

def create_sparse_tempfile(name, size):
    """ Create a temporary sparse file.

        :param str name: suffix for filename
        :param size: the file size (in bytes)
        :returns: the path to the newly created file
    """
    (fd, path) = tempfile.mkstemp(prefix="libblockdev.", suffix="-%s" % name)
    os.close(fd)
    create_sparse_file(path, size)
    return path

def create_sparse_file(path, size):
    """ Create a sparse file.

        :param str path: the full path to the file
        :param size: the size of the file (in bytes)
        :returns: None
    """
    fd = os.open(path, os.O_WRONLY|os.O_CREAT|os.O_TRUNC)
    os.ftruncate(fd, size)
    os.close(fd)

def wipe_all(dev, *args):
    for device in chain([dev], args):
        os.system("wipefs -a %s &>/dev/null" % device)

@contextmanager
def udev_settle():
    yield
    os.system("udevadm settle")

@contextmanager
def fake_utils(path="."):
    old_path = os.environ.get("PATH", "")
    if old_path:
        new_path = path + ":" + old_path
    else:
        new_path = path
    os.environ["PATH"] = new_path

    yield

    os.environ["PATH"] = old_path

@contextmanager
def fake_path(path=None, keep_utils=None):
    keep_utils = keep_utils or []
    created_utils = set()
    if path:
        for util in keep_utils:
            util_path = GLib.find_program_in_path(util)
            if util_path:
                os.symlink(util_path, os.path.join(path, util))
                created_utils.add(util)
    old_path = os.environ.get("PATH", "")
    os.environ["PATH"] = path or ""

    yield

    os.environ["PATH"] = old_path
    for util in created_utils:
        os.unlink(os.path.join(path, util))
