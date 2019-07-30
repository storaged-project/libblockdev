#!/usr/bin/python3

from __future__ import print_function

import argparse
import datetime
import os
import re
import six
import subprocess
import sys
import unittest
import yaml

from distutils.spawn import find_executable

LIBDIRS = 'src/utils/.libs:src/plugins/.libs:src/plugins/fs/.libs:src/lib/.libs'
GIDIR = 'src/lib'

SKIP_CONFIG = 'skip.yml'


def _get_tests_from_suite(suite, tests):
    """ Extract tests from the test suite """
    # 'tests' we get from 'unittest.defaultTestLoader.discover' are "wrapped"
    # in multiple 'unittest.suite.TestSuite' classes/lists so we need to "unpack"
    # the indivudual test cases
    for test in suite:
        if isinstance(test, unittest.suite.TestSuite):
            _get_tests_from_suite(test, tests)

        if isinstance(test, unittest.TestCase):
            tests.append(test)

    return tests


def _get_test_tags(test):
    """ Get test tags for single test case """

    tags = []

    # test failed to load, usually some ImportError or something really broken
    # in the test file, just return empty list and let it fail
    # with python2 the loader will raise an exception directly without returning
    # a "fake" FailedTest test case
    if six.PY3 and isinstance(test, unittest.loader._FailedTest):
        return tags

    test_fn = getattr(test, test._testMethodName)

    # it is possible to either tag a test funcion or the class so we need to
    # check both for the tag
    if getattr(test_fn, "slow", False) or getattr(test_fn.__self__, "slow", False):
        tags.append(TestTags.SLOW)
    if getattr(test_fn, "unstable", False) or getattr(test_fn.__self__, "unstable", False):
        tags.append(TestTags.UNSTABLE)
    if getattr(test_fn, "unsafe", False) or getattr(test_fn.__self__, "unsafe", False):
        tags.append(TestTags.UNSAFE)
    if getattr(test_fn, "core", False) or getattr(test_fn.__self__, "core", False):
        tags.append(TestTags.CORE)
    if getattr(test_fn, "nostorage", False) or getattr(test_fn.__self__, "nostorage", False):
        tags.append(TestTags.NOSTORAGE)
    if getattr(test_fn, "extradeps", False) or getattr(test_fn.__self__, "extradeps", False):
        tags.append(TestTags.EXTRADEPS)
    if getattr(test_fn, "regression", False) or getattr(test_fn.__self__, "regression", False):
        tags.append(TestTags.REGRESSION)
    if getattr(test_fn, "sourceonly", False) or getattr(test_fn.__self__, "sourceonly", False):
        tags.append(TestTags.SOURCEONLY)

    return tags


def parse_args():
    """ Parse cmdline arguments """

    argparser = argparse.ArgumentParser(description='libblockdev test suite')
    argparser.add_argument('testname', nargs='*', help='name of test class or '
                           'method (e. g. "CryptoTestFormat", '
                           '"GenericResize.test_ext2_generic_resize")')
    argparser.add_argument('-f', '--fast', dest='fast', help='skip slow tests',
                           action='store_true')
    argparser.add_argument('-l', '--lucky', dest='lucky',
                           help='run also potentially dangerous/failing tests',
                           action='store_true')
    argparser.add_argument('-j', '--jenkins', dest='jenkins',
                           help='run also tests that should run only in a CI environment',
                           action='store_true')
    argparser.add_argument('-c', '--core', dest='core',
                           help='run tests that cover basic functionality of the library and regression tests',
                           action='store_true')
    argparser.add_argument('-s', '--stop', dest='stop',
                           help='stop executing after first failed test',
                           action='store_true')
    argparser.add_argument('-i', '--installed', dest='installed',
                           help='run tests against installed version of libblockdev',
                           action='store_true')
    args = argparser.parse_args()

    if args.fast:
        os.environ['SKIP_SLOW'] = ''
    if args.lucky:
        os.environ['FEELINGLUCKY'] = ''
    if args.jenkins:
        os.environ['JENKINS_HOME'] = ''

    # read the environmental variables for backwards compatibility
    if 'JENKINS_HOME' in os.environ:
        args.jenkins = True
    if 'SKIP_SLOW' in os.environ:
        args.fast = True
    if 'FEELINGLUCKY' in os.environ:
        args.lucky = True

    return args

def _split_test_id(test_id):
    # test.id() looks like 'crypto_test.CryptoTestResize.test_luks2_resize'
    # and we want to print 'test_luks2_resize (crypto_test.CryptoTestResize)'
    test_desc = test.id().split(".")
    test_name = test_desc[-1]
    test_module = ".".join(test_desc[:-1])

    return test_name, test_module


def _print_skip_message(test, skip_tag):
    test_id = test.id()
    test_module, test_name = _split_test_id(test_id)

    if skip_tag == TestTags.SLOW:
        reason = "skipping slow tests"
    elif skip_tag == TestTags.UNSTABLE:
        reason = "skipping unstable tests"
    elif skip_tag == TestTags.UNSAFE:
        reason = "skipping test that modifies system configuration"
    elif skip_tag == TestTags.EXTRADEPS:
        reason = "skipping test that requires special configuration"
    elif skip_tag == TestTags.CORE:
        reason = "skipping non-core test"
    elif skip_tag == TestTags.SOURCEONLY:
        reason = "skipping test that can run only against library compiled from source"
    else:
        reason = "unknown reason"  # just to be sure there is some default value

    if test._testMethodDoc:
        print("%s (%s)\n%s ... skipped '%s'" % (test_name, test_module, test._testMethodDoc, reason),
              file=sys.stderr)
    else:
        print("%s (%s) ... skipped '%s'" % (test_name, test_module, reason),
              file=sys.stderr)


def _should_skip(distro=None, version=None, arch=None, reason=None):
    # all these can be lists or a single value, so covert everything to list
    if distro is not None and type(distro) is not list:
        distro = [distro]
    if version is not None and type(version) is not list:
        version = [version]
    if arch is not None and type(arch) is not list:
        arch = [arch]

    # DISTRO, VERSION and ARCH variables are set in main, we don't need to
    # call hostnamectl etc. for every test run
    if (distro is None or DISTRO in distro) and (version is None or VERSION in version) and \
       (arch is None or ARCH in arch):
        return True

    return False


def _parse_skip_config(config):
    with open(config) as f:
        data = f.read()
    parsed = yaml.load(data, Loader=yaml.SafeLoader)

    skipped_tests = dict()

    for entry in parsed:
        for skip in entry["skip_on"]:
            if _should_skip(**skip):
                skipped_tests[entry["test"]] = skip["reason"]

    return skipped_tests


if __name__ == '__main__':

    testdir = os.path.abspath(os.path.dirname(__file__))
    projdir = os.path.abspath(os.path.normpath(os.path.join(testdir, '..')))

    args = parse_args()
    if args.installed:
        os.environ['LIBBLOCKDEV_TESTS_SKIP_OVERRIDE'] = ''
        os.environ['LIBBLOCKDEV_CONFIG_DIR'] = '/etc/libblockdev/conf.d/'
    else:
        if 'LD_LIBRARY_PATH' not in os.environ and 'GI_TYPELIB_PATH' not in os.environ:
            os.environ['LD_LIBRARY_PATH'] = LIBDIRS
            os.environ['GI_TYPELIB_PATH'] = GIDIR
            os.environ['LIBBLOCKDEV_CONFIG_DIR'] = os.path.join(testdir, 'default_config')

            try:
                pyver = 'python3' if six.PY3 else 'python'
                os.execv(sys.executable, [pyver] + sys.argv)
            except OSError as e:
                print('Failed re-exec with a new LD_LIBRARY_PATH and GI_TYPELIB_PATH: %s' % str(e))
                sys.exit(1)

        sys.path.append(testdir)
        sys.path.append(projdir)
        sys.path.append(os.path.join(projdir, 'src/python'))

    start_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()

    if args.testname:
        test_cases = loader.loadTestsFromNames(args.testname)
    else:
        test_cases = loader.discover(start_dir=testdir, pattern='*_test*.py')

    # extract list of test classes so we can check/run them manually one by one
    tests = []
    tests = _get_tests_from_suite(test_cases, tests)

    # get distro and arch here so we don't have to do this for every test
    from utils import get_version
    DISTRO, VERSION = get_version()
    ARCH = os.uname()[-1]

    # get list of tests to skip from the config file
    skipping = _parse_skip_config(os.path.join(testdir, SKIP_CONFIG))

    # for some reason overrides_hack will fail if we import this at the start
    # of the file
    from utils import TestTags

    for test in tests:
        test_id = test.id()

        # get tags and (possibly) skip the test
        tags = _get_test_tags(test)

        if TestTags.SLOW in tags and args.fast:
            _print_skip_message(test, TestTags.SLOW)
            continue
        if TestTags.UNSTABLE in tags and not args.lucky:
            _print_skip_message(test, TestTags.UNSTABLE)
            continue
        if TestTags.UNSAFE in tags or TestTags.EXTRADEPS in tags and not args.jenkins:
            _print_skip_message(test, TestTags.UNSAFE)
            continue
        if TestTags.EXTRADEPS in tags and not args.jenkins:
            _print_skip_message(test, TestTags.EXTRADEPS)
            continue
        if TestTags.SOURCEONLY in tags and args.installed:
            _print_skip_message(test, TestTags.SOURCEONLY)
            continue

        if args.core and TestTags.CORE not in tags and TestTags.REGRESSION not in tags:
            _print_skip_message(test, TestTags.CORE)
            continue

        # check if the test is in the list of tests to skip
        skip_id = next((test_pattern for test_pattern in skipping.keys() if re.search(test_pattern, test_id)), None)
        if skip_id:
            test_name, test_module = _split_test_id(test_id)
            reason = "not supported on this distribution in this version and arch: %s" % skipping[skip_id]
            print("%s (%s)\n%s ... skipped '%s'" % (test_name, test_module,
                                                    test._testMethodDoc, reason),
                  file=sys.stderr)
            continue

        # finally add the test to the suite
        suite.addTest(test)

    result = unittest.TextTestRunner(verbosity=2, failfast=args.stop).run(suite)

    # dump cropped journal to log file
    if find_executable('journalctl'):
        with open('journaldump.log', 'w') as outfile:
            subprocess.call(['journalctl', '-S', start_time], stdout=outfile)

    if result.wasSuccessful():
        sys.exit(0)
    else:
        sys.exit(1)
