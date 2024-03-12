from __future__ import print_function

import os
import stat
import re
import glob
import subprocess
import tempfile
import dbus
import unittest
import time
import sys
import json
import uuid
from contextlib import contextmanager
from enum import Enum
from itertools import chain

import gi
gi.require_version('GLib', '2.0')
from gi.repository import GLib

try:
    from subprocess import DEVNULL
except ImportError:
    DEVNULL = open("/dev/null", "w")

_lio_devs = dict()
_nvmet_devs = dict()

def create_sparse_tempfile(name, size, dir="/tmp"):
    """ Create a temporary sparse file.

        :param str name: suffix for filename
        :param size: the file size (in bytes)
        :returns: the path to the newly created file
    """
    (fd, path) = tempfile.mkstemp(prefix="bd.", suffix="-%s" % name, dir=dir)
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
    if not os.path.exists(path):
        raise RuntimeError("Path '%s' not found" % path)
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

ALL_UTILS = {"lvm", "btrfs", "mkswap", "swaplabel", "multipath", "mpathconf", "dmsetup", "mdadm",
             "mkfs.exfat", "fsck.exfat", "tune.exfat",
             "mke2fs", "e2fsck", "tune2fs", "dumpe2fs", "resize2fs",
             "mkfs.f2fs", "fsck.f2fs", "fsck.f2fs", "dump.f2fs", "resize.f2fs",
             "mkfs.nilfs2", "nilfs-tune", "nilfs-resize",
             "mkntfs", "ntfsfix", "ntfsresize", "ntfslabel", "ntfsinfo",
             "mkfs.vfat", "fatlabel", "fsck.vfat", "vfat-resize",
             "mkfs.xfs", "xfs_db", "xfs_repair", "xfs_admin", "xfs_growfs"}

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
    status = subprocess.call(["targetcli", "/loopback/%s/luns delete lun0" % wwn], stdout=DEVNULL)
    if status != 0:
        raise RuntimeError("Failed to delete the '%s' loopback device's lun0" % wwn)
    if delete_target:
        _delete_target(wwn, backstore)

def _get_lio_dev_path(store_wwn, tgt_wwn, store_name, retry=True):
    """Check if the lio device has been really created and is in /dev/disk/by-id"""

    # the backstore's wwn contains '-'s we need to get rid of and then take just
    # the fist 25 characters which participate in the device's ID
    wwn = store_wwn.replace("-", "")
    wwn = wwn[:25]

    globs = glob.glob("/dev/disk/by-id/wwn-*%s" % wwn)
    if len(globs) != 1:
        if retry:
            time.sleep(3)
            os.system("udevadm settle")
            return _get_lio_dev_path(store_wwn, tgt_wwn, store_wwn, False)
        else:
            _delete_target(tgt_wwn, store_name)
            raise RuntimeError("Failed to identify the resulting device for '%s'" % store_name)
    else:
        return os.path.realpath(globs[0])

def create_lio_device(fpath, block_size=0):
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

    # set sector/block size if requested
    if block_size:
        status = subprocess.call(["targetcli", "/backstores/fileio/%s set attribute block_size=%d" % (store_name, block_size)],
                                  stdout=DEVNULL)
        if status != 0:
            raise RuntimeError("Failed to set block size for '%s'" % store_name)

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

    dev_path = _get_lio_dev_path(store_wwn, tgt_wwn, store_name)

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

def find_nvme_ctrl_devs_for_subnqn(subnqn):
    """
    Find NVMe controller devices for the specified subsystem nqn

    :param str subnqn: subsystem nqn
    """

    def _check_subsys(subsys, dev_paths):
        if subsys['SubsystemNQN'] == subnqn:
            for ctrl in subsys['Controllers']:
                if ctrl['Transport'] != 'loop':
                    continue
                path = os.path.join('/dev/', ctrl['Controller'])
                try:
                    st = os.lstat(path)
                    # nvme controller node is a character device
                    if stat.S_ISCHR(st.st_mode):
                        dev_paths += [path]
                except:
                    pass

    ret, out, err = run_command("nvme list --output-format=json --verbose")
    if ret != 0:
        raise RuntimeError("Error getting NVMe list: '%s %s'" % (out, err))

    decoder = json.JSONDecoder()
    decoded = decoder.decode(out)
    if not decoded or 'Devices' not in decoded:
        return []

    dev_paths = []
    for dev in decoded['Devices']:
        # nvme-cli 2.x
        if 'Subsystems' in dev:
            for subsys in dev['Subsystems']:
                _check_subsys(subsys, dev_paths)
        # nvme-cli 1.x
        if 'SubsystemNQN' in dev:
            _check_subsys(dev, dev_paths)

    return dev_paths


def find_nvme_ns_devs_for_subnqn(subnqn):
    """
    Find NVMe namespace block devices for the specified subsystem nqn

    :param str subnqn: subsystem nqn
    """

    def _check_namespaces(node, ns_dev_paths):
        for ns in node['Namespaces']:
            path = os.path.join('/dev/', ns['NameSpace'])
            try:
                st = os.lstat(path)
                if stat.S_ISBLK(st.st_mode):
                    ns_dev_paths += [path]
            except:
                pass

    def _check_subsys(subsys, ns_dev_paths):
        if subsys['SubsystemNQN'] == subnqn:
            if 'Namespaces' in subsys:
                _check_namespaces(subsys, ns_dev_paths)
            # kernel 4.18
            if 'Controllers' in subsys:
                for ctrl in subsys['Controllers']:
                    if 'Namespaces' in ctrl:
                        _check_namespaces(ctrl, ns_dev_paths)

    ret, out, err = run_command("nvme list --output-format=json --verbose")
    if ret != 0:
        raise RuntimeError("Error getting NVMe list: '%s %s'" % (out, err))

    decoder = json.JSONDecoder()
    decoded = decoder.decode(out)
    if not decoded or 'Devices' not in decoded:
        return []

    ns_dev_paths = []
    for dev in decoded['Devices']:
        # nvme-cli 2.x
        if 'Subsystems' in dev:
            for subsys in dev['Subsystems']:
                _check_subsys(subsys, ns_dev_paths)
        # nvme-cli 1.x
        if 'SubsystemNQN' in dev:
            _check_subsys(dev, ns_dev_paths)

    return ns_dev_paths


def get_nvme_hostnqn():
    """
    Retrieves NVMe host NQN string from /etc/nvme/hostnqn or uses nvme-cli to generate
    new one (stable, typically generated from machine DMI data) when not available.
    """

    hostnqn = None
    try:
        hostnqn = read_file('/etc/nvme/hostnqn')
    except:
        pass

    if hostnqn is None or len(hostnqn.strip()) < 1:
        ret, hostnqn, err = run_command('nvme gen-hostnqn')
        if ret != 0:
            raise RuntimeError("Cannot get host NQN: '%s %s'" % (hostnqn, err))

    return hostnqn.strip()


def setup_nvme_target(dev_paths, subnqn):
    """
    Sets up a new NVMe target loop device (using nvmetcli) on top of the
    :param:`dev_paths` backing block devices.

    :param set dev_paths: set of backing block device paths
    :param str subnqn: Subsystem NQN
    """

    # modprobe required nvme target modules
    for module in ['nvmet', 'nvme-loop']:
        ret, out, err = run_command("modprobe %s" % module)
        if ret != 0:
            raise RuntimeError("Cannot load required module %s: '%s %s'" % (module, out, err))

    # create a JSON file for nvmetcli
    with tempfile.NamedTemporaryFile(mode='wt',delete=False) as tmp:
        tcli_json_file = tmp.name
        namespaces = ",".join(["""
        {{
          "device": {{
            "nguid": "{nguid}",
            "path": "{path}"
          }},
          "enable": 1,
          "nsid": {nsid}
        }}
        """.format(nguid=uuid.uuid4(), path=dev_path, nsid=i) for i, dev_path in enumerate(dev_paths, start=1)])

        json = """
{
  "ports": [
    {
      "addr": {
        "adrfam": "",
        "traddr": "",
        "treq": "not specified",
        "trsvcid": "",
        "trtype": "loop"
      },
      "portid": 1,
      "referrals": [],
      "subsystems": [
        "%s"
      ]
    }
  ],
  "subsystems": [
    {
      "attr": {
        "allow_any_host": "1"
      },
      "namespaces": [
%s
      ],
      "nqn": "%s"
    }
  ]
}
"""
        tmp.write(json % (subnqn, namespaces, subnqn))

    # export the loop device on the target
    ret, out, err = run_command("nvmetcli restore %s" % tcli_json_file)
    os.unlink(tcli_json_file)
    if ret != 0:
        raise RuntimeError("Error setting up the NVMe target: '%s %s'" % (out, err))


def teardown_nvme_target():
    """
    Tear down any previously set up kernel nvme target.
    """
    ret, out, err = run_command("nvmetcli clear")
    if ret != 0:
        raise RuntimeError("Error clearing the NVMe target: '%s %s'" % (out, err))


def create_nvmet_device(dev_path):
    """
    Creates a new NVMe target loop device (using nvmetcli) on top of the
    :param:`dev_path` backing block device and initiates a connection to it.

    :param str dev_path: backing block device path
    :returns: path of the NVMe controller device (e.g. "/dev/nvme0")
    :rtype: str
    """

    SUBNQN = 'libblockdev_subnqn'

    setup_nvme_target([dev_path], SUBNQN)

    # connect initiator to the newly created target
    (ret, out, err) = run_command("nvme connect --transport=loop --nqn=%s" % SUBNQN)
    if ret != 0:
        raise RuntimeError("Error connecting to the NVMe target: '%s %s'" % (out, err))

    nvme_devs = find_nvme_ctrl_devs_for_subnqn(SUBNQN)
    if len(nvme_devs) != 1:
        raise RuntimeError("Error looking up block device for the '%s' nqn" % SUBNQN)

    _nvmet_devs[nvme_devs[0]] = (SUBNQN, dev_path)
    return nvme_devs[0]


def delete_nvmet_device(nvme_dev):
    """
    Logout and tear down previously created NVMe target device.

    :param str nvme_dev: path of the NVMe device to delete
    """
    if nvme_dev in _nvmet_devs:
        subnqn, dev_path = _nvmet_devs[nvme_dev]

        # disconnect the initiator
        ret, out, err = run_command("nvme disconnect --nqn=%s" % subnqn)
        if ret != 0:
            raise RuntimeError("Error disconnecting the '%s' nqn: '%s %s'" % (subnqn, out, err))

        # clear the target
        teardown_nvme_target()
    else:
        raise RuntimeError("Unknown device '%s'" % nvme_dev)


def read_file(filename):
    with open(filename, "r") as f:
        content = f.read()
    return content

def write_file(filename, content):
    with open(filename, "w") as f:
        f.write(content)

def run_command(command, cmd_input=None):
    res = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, stdin=subprocess.PIPE)

    out, err = res.communicate(input=cmd_input)
    return (res.returncode, out.decode().strip(), err.decode().strip())

def get_version_from_lsb():
    ret, out, err = run_command("lsb_release -rs")
    if ret != 0:
        raise RuntimeError("Cannot get distro version from lsb_release output: '%s %s'" % (out, err))

    return out.split(".")[0]

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
        version = get_version_from_lsb()

    return (distro, version)


def get_version():
    """ Try to get distro and version
    """

    bus = dbus.SystemBus()

    # get information about the distribution from systemd (hostname1)
    sys_info = bus.get_object("org.freedesktop.hostname1", "/org/freedesktop/hostname1")
    cpe = str(sys_info.Get("org.freedesktop.hostname1", "OperatingSystemCPEName", dbus_interface=dbus.PROPERTIES_IFACE))

    if cpe:
        # 2nd to 4th fields from e.g. "cpe:/o:fedoraproject:fedora:25" or "cpe:/o:redhat:enterprise_linux:7.3:GA:server"
        _project, distro, version = tuple(cpe.split(":")[2:5])
        # we want just the major version, so remove all decimal places (if any)
        version = str(int(float(version)))
    else:
        pretty_name = str(sys_info.Get("org.freedesktop.hostname1", "OperatingSystemPrettyName", dbus_interface=dbus.PROPERTIES_IFACE))
        distro, version = get_version_from_pretty_name(pretty_name)

    return (distro, version)


def skip_on(skip_on_distros=None, skip_on_version="", skip_on_arch="", reason=""):
    """A function returning a decorator to skip some test on a given distribution-version combination

    :param skip_on_distros: distro(s) to skip the test on
    :type skip_on_distros: str or tuple of str
    :param str skip_on_version: version of distro(s) to skip the tests on (only
                                checked on distribution match)

    """
    if isinstance(skip_on_distros, str):
        skip_on_distros = (skip_on_distros,)

    distro, version = get_version()
    arch = os.uname()[-1]

    def decorator(func):
        if (skip_on_distros is None or distro in skip_on_distros) and (not skip_on_version or skip_on_version == version) and \
           (not skip_on_arch or skip_on_arch == arch):
            msg = "not supported on this distribution in this version and arch" + (": %s" % reason if reason else "")
            return unittest.skip(msg)(func)
        else:
            return func

    return decorator

# taken from libbytesize's tests/locale_utils.py
def get_avail_locales():
    return {loc.decode(errors="replace").strip() for loc in subprocess.check_output(["locale", "-a"]).split()}

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


# taken from udisks2/src/tests/dbus-tests/udiskstestcase.py
def unstable_test(test):
    """Decorator for unstable tests

    Failures of tests decorated with this decorator are silently ignored unless
    the ``UNSTABLE_TESTS_FATAL`` environment variable is defined.
    """

    def decorated_test(*args):
        try:
            test(*args)
        except unittest.SkipTest:
            # make sure skipped tests are just skipped as usual
            raise
        except:
            # and swallow everything else, just report a failure of an unstable
            # test, unless told otherwise
            if "UNSTABLE_TESTS_FATAL" in os.environ:
                raise
            print("unstable-fail...", end="", file=sys.stderr)

    return decorated_test


class TestTags(Enum):
    ALL = "all"               # "default" tag for running all tests
    SLOW = "slow"             # slow tests
    UNSTABLE = "unstable"     # randomly failing tests
    UNSAFE = "unsafe"         # tests that change system configuration
    CORE = "core"             # tests covering core functionality
    NOSTORAGE = "nostorage"   # tests that don't work with storage
    EXTRADEPS = "extradeps"   # tests that require special configuration and/or device to run
    REGRESSION = "regression" # regression tests
    SOURCEONLY = "sourceonly" # tests that can't run against installed library

    @classmethod
    def get_tags(cls):
        return [t.value for t in cls.__members__.values()]

    @classmethod
    def get_tag_by_value(cls, value):
        tag = next((t for t in cls.__members__.values() if t.value == value), None)

        if not tag:
            raise ValueError('Unknown value "%s"' % value)

        return tag


def tag_test(*tags):
    def decorator(func):
        func.slow = TestTags.SLOW in tags
        func.unstable = TestTags.UNSTABLE in tags
        func.unsafe = TestTags.UNSAFE in tags
        func.core = TestTags.CORE in tags
        func.nostorage = TestTags.NOSTORAGE in tags
        func.extradeps = TestTags.EXTRADEPS in tags
        func.regression = TestTags.REGRESSION in tags
        func.sourceonly = TestTags.SOURCEONLY in tags

        return func

    return decorator


def run(cmd_string):
    """
    Run the a command with file descriptors closed as lvm is trying to
    make sure everyone else is following best practice and not leaking FDs.
    """
    return subprocess.call(cmd_string, close_fds=True, shell=True)


def mount(device, where, ro=False):
    if not os.path.isdir(where):
        os.makedirs(where)
    if ro:
        os.system("mount -oro %s %s" % (device, where))
    else:
        os.system("mount %s %s" % (device, where))

def umount(what, retry=True):
    try:
        os.system("umount -A -l %s >/dev/null 2>&1" % what)
        os.rmdir(what)
    except OSError as e:
        # retry the umount if the device is busy
        if "busy" in str(e) and retry:
            time.sleep(2)
            umount(what, False)
