import os
import tempfile
from contextlib import contextmanager

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

@contextmanager
def udev_settle():
    yield
    os.system("udevadm settle")
