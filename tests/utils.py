import os
import re
import glob
import subprocess
import tempfile
import dbus
import unittest
import time
from contextlib import contextmanager
from itertools import chain

from gi.repository import GLib

try:
    from subprocess import DEVNULL
except ImportError:
    DEVNULL = open("/dev/null", "w")

_lio_devs = dict()

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
        os.system("wipefs -a %s >/dev/null 2>&1" % device)

@contextmanager
def udev_settle():
    try:
        yield
    finally:
        os.system("udevadm settle")

@contextmanager
def fake_utils(path="."):
    old_path = os.environ.get("PATH", "")
    if old_path:
        new_path = path + ":" + old_path
    else:
        new_path = path
    os.environ["PATH"] = new_path

    try:
        yield
    finally:
        os.environ["PATH"] = old_path

ALL_UTILS = {"lvm", "thin_metadata_size", "btrfs", "mkswap", "multipath", "mpathconf", "dmsetup", "mdadm", "make-bcache", "sgdisk", "sfdisk"}

@contextmanager
def fake_path(path=None, keep_utils=None, all_but=None):
    if all_but is not None:
        if isinstance(all_but, (list, set, tuple)):
            keep_utils = ALL_UTILS - set(all_but)
        else:
            keep_utils = ALL_UTILS - {all_but}
    keep_utils = keep_utils or []
    created_utils = set()
    rm_path = False
    if keep_utils:
        if path is None:
            path = tempfile.mkdtemp(prefix="libblockdev-fake-path", dir="/tmp")
            rm_path = True
        for util in keep_utils:
            util_path = GLib.find_program_in_path(util)
            if util_path:
                os.symlink(util_path, os.path.join(path, util))
                created_utils.add(util)
    old_path = os.environ.get("PATH", "")
    os.environ["PATH"] = path or ""

    try:
        yield
    finally:
        os.environ["PATH"] = old_path
        for util in created_utils:
            os.unlink(os.path.join(path, util))
        if rm_path:
            os.rmdir(path)

def _delete_backstore(name):
    status = subprocess.call(["targetcli", "/backstores/fileio/ delete %s" % name], stdout=DEVNULL)
    if status != 0:
        raise RuntimeError("Failed to delete the '%s' fileio backstore" % name)

def _delete_target(wwn, backstore=None):
    status = subprocess.call(["targetcli", "/loopback delete %s" % wwn], stdout=DEVNULL)
    if status != 0:
        raise RuntimeError("Failed to delete the '%s' loopback device" % wwn)

    if backstore is not None:
        _delete_backstore(backstore)

def _delete_lun(wwn, delete_target=True, backstore=None):
    status = subprocess.call(["targetcli", "/loopback/%s/luns/lun0 delete"], stdout=DEVNULL)
    if status != 0:
        raise RuntimeError("Failed to delete the '%s' loopback device's lun0" % wwn)
    if delete_target:
        _delete_target(wwn, backstore)

def create_lio_device(fpath):
    """
    Creates a new LIO loopback device (using targetcli) on top of the
    :param:`fpath` backing file.

    :param str fpath: path of the backing file
    :returns: path of the newly created device (e.g. "/dev/sdb")
    :rtype: str

    """

    # "register" the backing file as a fileio backstore
    store_name = os.path.basename(fpath)
    status = subprocess.call(["targetcli", "/backstores/fileio/ create %s %s" % (store_name, fpath)], stdout=DEVNULL)
    if status != 0:
        raise RuntimeError("Failed to register '%s' as a fileio backstore" % fpath)

    out = subprocess.check_output(["targetcli", "/backstores/fileio/%s info" % store_name])
    out = out.decode("utf-8")
    store_wwn = None
    for line in out.splitlines():
        if line.startswith("wwn: "):
            store_wwn = line[5:]
    if store_wwn is None:
        raise RuntimeError("Failed to determine '%s' backstore's wwn" % store_name)

    # set the optimal alignment because the default is weird and our
    # partitioning tests expect 2048
    status = subprocess.call(["targetcli", "/backstores/fileio/%s set attribute optimal_sectors=2048" % store_name], stdout=DEVNULL)
    if status != 0:
        raise RuntimeError("Failed to set optimal alignment for '%s'" % store_name)

    # create a new loopback device
    out = subprocess.check_output(["targetcli", "/loopback create"])
    out = out.decode("utf-8")
    match = re.match(r'Created target (.*).', out)
    if match:
        tgt_wwn = match.groups()[0]
    else:
        _delete_backstore(store_name)
        raise RuntimeError("Failed to create a new loopback device")

    with udev_settle():
        status = subprocess.call(["targetcli", "/loopback/%s/luns create /backstores/fileio/%s" % (tgt_wwn, store_name)], stdout=DEVNULL)
    if status != 0:
        _delete_target(tgt_wwn, store_name)
        raise RuntimeError("Failed to create a new LUN for '%s' using '%s'" % (tgt_wwn, store_name))

    # the backstore's wwn contains '-'s we need to get rid of and then take just
    # the fist 25 characters which participate in the device's ID
    store_wwn = store_wwn.replace("-", "")
    store_wwn = store_wwn[:25]

    globs = glob.glob("/dev/disk/by-id/wwn-*%s" % store_wwn)
    if len(globs) != 1:
        _delete_target(tgt_wwn, store_name)
        raise RuntimeError("Failed to identify the resulting device for '%s'" % store_name)

    dev_path = os.path.realpath(globs[0])
    _lio_devs[dev_path] = (tgt_wwn, store_name)
    return dev_path

def delete_lio_device(dev_path):
    """
    Delete a previously setup/created LIO device

    :param str dev_path: path of the device to delete

    """
    if dev_path in _lio_devs:
        wwn, store_name = _lio_devs[dev_path]
        _delete_lun(wwn, True, store_name)
    else:
        raise RuntimeError("Unknown device '%s'" % dev_path)

def read_file(filename):
    with open(filename, "r") as f:
        content = f.read()
    return content

def write_file(filename, content):
    with open(filename, "w") as f:
        f.write(content)

def run_command(command):
    res = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)

    out, err = res.communicate()
    return (res.returncode, out.decode().strip(), err.decode().strip())

def get_version_from_pretty_name(pretty_name):
    """ Try to get distro and version from 'OperatingSystemPrettyName'
        hostname property.

        It should look like this:
         - "Debian GNU/Linux 9 (stretch)"
         - "Fedora 27 (Workstation Edition)"
         - "CentOS Linux 7 (Core)"

        So just return first word as distro and first number as version.
    """
    distro = pretty_name.split()[0].lower()
    match = re.search(r"\d+", pretty_name)
    if match is not None:
        version = match.group(0)
    else:
        raise RuntimeError("Cannot get distro name and version from '%s'" % pretty_name)

    return (distro, version)


def skip_on(skip_on_distros, skip_on_version="", reason=""):
    """A function returning a decorator to skip some test on a given distribution-version combination

    :param skip_on_distros: distro(s) to skip the test on
    :type skip_on_distros: str or tuple of str
    :param str skip_on_version: version of distro(s) to skip the tests on (only
                                checked on distribution match)

    """
    if isinstance(skip_on_distros, str):
        skip_on_distros = (skip_on_distros,)

    bus = dbus.SystemBus()

    # get information about the distribution from systemd (hostname1)
    sys_info = bus.get_object("org.freedesktop.hostname1", "/org/freedesktop/hostname1")
    cpe = str(sys_info.Get("org.freedesktop.hostname1", "OperatingSystemCPEName", dbus_interface=dbus.PROPERTIES_IFACE))

    if cpe:
        # 2nd to 4th fields from e.g. "cpe:/o:fedoraproject:fedora:25" or "cpe:/o:redhat:enterprise_linux:7.3:GA:server"
        project, distro, version = tuple(cpe.split(":")[2:5])
    else:
        pretty_name = str(sys_info.Get("org.freedesktop.hostname1", "OperatingSystemPrettyName", dbus_interface=dbus.PROPERTIES_IFACE))
        distro, version = get_version_from_pretty_name(pretty_name)

    def decorator(func):
        if distro in skip_on_distros and (not skip_on_version or skip_on_version == version):
            msg = "not supported on this distribution in this version" + (": %s" % reason if reason else "")
            return unittest.skip(msg)(func)
        else:
            return func

    return decorator

# taken from libbytesize's tests/locale_utils.py
def get_avail_locales():
    return {loc.strip() for loc in subprocess.check_output(["locale", "-a"]).split()}

def requires_locales(locales):
    """A decorator factory to skip tests that require unavailable locales

    :param set locales: set of required locales

    **Requires the test to have the set of available locales defined as its
    ``avail_locales`` attribute.**

    """

    canon_locales = {loc.replace("UTF-8", "utf8") for loc in locales}
    def decorator(test_method):
        def decorated(test, *args):
            missing = canon_locales - set(test.avail_locales)
            if missing:
                test.skipTest("requires missing locales: %s" % missing)
            else:
                return test_method(test, *args)

        return decorated

    return decorator


def run(cmd_string):
    """
    Run the a command with file descriptors closed as lvm is trying to
    make sure everyone else is following best practice and not leaking FDs.
    """
    return subprocess.call(cmd_string, close_fds=True, shell=True)


def mount(device, where):
    if not os.path.isdir(where):
        os.makedirs(where)
    os.system("mount %s %s" % (device, where))


def umount(what, retry=True):
    try:
        os.system("umount %s >/dev/null 2>&1" % what)
        os.rmdir(what)
    except OSError as e:
        # retry the umount if the device is busy
        if "busy" in str(e) and retry:
            time.sleep(2)
            umount(what, False)
