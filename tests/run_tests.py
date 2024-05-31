#!/usr/bin/python3

from __future__ import print_function

import argparse
import datetime
import os
import pdb
import re
import shutil
import subprocess
import sys
import traceback
import unittest
import yaml


from utils import TestTags, get_version

LIBDIRS = 'src/utils/.libs:src/plugins/.libs:src/plugins/fs/.libs:src/lib/.libs:src/plugins/nvme/.libs:src/plugins/smart/.libs'
GIDIR = 'src/lib'

SKIP_CONFIG = 'skip.yml'


def _get_tests_from_suite(suite, tests):
    """ Extract tests from the test suite """
    # 'tests' we get from 'unittest.defaultTestLoader.discover' are "wrapped"
    # in multiple 'unittest.suite.TestSuite' classes/lists so we need to "unpack"
    # the individual test cases
    for test in suite:
        if isinstance(test, unittest.suite.TestSuite):
            _get_tests_from_suite(test, tests)

        if isinstance(test, unittest.TestCase):
            tests.add(test)

    return tests


def _get_test_tags(test):
    """ Get test tags for single test case """

    tags = set()
    tags.add(TestTags.ALL)

    # test failed to load, usually some ImportError or something really broken
    # in the test file, just return empty list and let it fail
    if isinstance(test, unittest.loader._FailedTest):
        return tags

    test_fn = getattr(test, test._testMethodName)

    # it is possible to either tag a test function or the class so we need to
    # check both for the tag
    if getattr(test_fn, "slow", False) or getattr(test_fn.__self__, "slow", False):
        tags.add(TestTags.SLOW)
    if getattr(test_fn, "unstable", False) or getattr(test_fn.__self__, "unstable", False):
        tags.add(TestTags.UNSTABLE)
    if getattr(test_fn, "unsafe", False) or getattr(test_fn.__self__, "unsafe", False):
        tags.add(TestTags.UNSAFE)
    if getattr(test_fn, "core", False) or getattr(test_fn.__self__, "core", False):
        tags.add(TestTags.CORE)
    if getattr(test_fn, "nostorage", False) or getattr(test_fn.__self__, "nostorage", False):
        tags.add(TestTags.NOSTORAGE)
    if getattr(test_fn, "extradeps", False) or getattr(test_fn.__self__, "extradeps", False):
        tags.add(TestTags.EXTRADEPS)
    if getattr(test_fn, "regression", False) or getattr(test_fn.__self__, "regression", False):
        tags.add(TestTags.REGRESSION)
    if getattr(test_fn, "sourceonly", False) or getattr(test_fn.__self__, "sourceonly", False):
        tags.add(TestTags.SOURCEONLY)

    return tags


def _check_arguments_compatibility(args):
    if args.include_tags & args.exclude_tags:
        print('Providing same tag in both "--include-tags" and "--exclude-tags" does not make sense.', file=sys.stderr)
        return False

    if args.fast and TestTags.SLOW.value in args.include_tags:
        print('Incompatible arguments: "--fast" and "--include-tags slow".', file=sys.stderr)
        return False

    if args.lucky and TestTags.UNSTABLE.value in args.exclude_tags:
        print('Incompatible arguments: "--lucky" and "--exclude-tags unstable".', file=sys.stderr)
        return False

    if args.jenkins and TestTags.UNSAFE.value in args.exclude_tags:
        print('Incompatible arguments: "--jenkins" and "--exclude-tags unsafe".', file=sys.stderr)
        return False

    if args.core and TestTags.CORE.value in args.exclude_tags:
        print('Incompatible arguments: "--core" and "--exclude-tags core".', file=sys.stderr)
        return False

    if args.installed and TestTags.SOURCEONLY.value in args.include_tags:
        print('Incompatible arguments: "--installed" and "--include-tags sourceonly".', file=sys.stderr)
        return False

    return True


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
    argparser.add_argument('-p', '--pdb', dest='pdb',
                           help='run pdb after a failed test',
                           action='store_true')
    argparser.add_argument('-i', '--installed', dest='installed',
                           help='run tests against installed version of libblockdev',
                           action='store_true')
    argparser.add_argument('--exclude-tags', nargs='+', dest='exclude_tags',
                           help='skip tests tagged with (at least one of) the provided tags')
    argparser.add_argument('--include-tags', nargs='+', dest='include_tags',
                           help='run only tests tagged with (at least one of) the provided tags')
    argparser.add_argument('--list-tags', dest='list_tags', help='print available tags and exit',
                           action='store_true')
    args = argparser.parse_args()

    all_tags = set(TestTags.get_tags())

    if args.list_tags:
        print('Available tags:', ', '.join(all_tags))
        sys.exit(0)

    # lets convert these to sets now to make argument checks easier
    args.include_tags = set(args.include_tags) if args.include_tags else set()
    args.exclude_tags = set(args.exclude_tags) if args.exclude_tags else set()

    # make sure user provided only valid tags
    if not all_tags.issuperset(args.include_tags):
        print('Unknown tag(s) specified:', ', '.join(args.include_tags - all_tags), file=sys.stderr)
        sys.exit(1)
    if not all_tags.issuperset(args.exclude_tags):
        print('Unknown tag(s) specified:', ', '.join(args.exclude_tags - all_tags), file=sys.stderr)
        sys.exit(1)

    if not _check_arguments_compatibility(args):
        sys.exit(1)

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

    # we want to use only the include/exclude sets, not other properties
    if args.fast:
        args.exclude_tags.add(TestTags.SLOW.value)
    if args.installed:
        args.exclude_tags.add(TestTags.SOURCEONLY.value)
    if args.core:
        args.include_tags.add(TestTags.CORE.value)
        args.include_tags.add(TestTags.REGRESSION.value)

    # for backwards compatibility we want to exclude unsafe and unstable by default
    if not args.jenkins and not (TestTags.UNSAFE.value in args.include_tags or
                                 TestTags.ALL.value in args.include_tags):
        args.exclude_tags.add(TestTags.UNSAFE.value)
    if not args.lucky and not (TestTags.UNSTABLE.value in args.include_tags or
                               TestTags.ALL.value in args.include_tags):
        args.exclude_tags.add(TestTags.UNSTABLE.value)

    return args

def _split_test_id(test_id):
    # test.id() looks like 'crypto_test.CryptoTestResize.test_luks2_resize'
    # and we want to print 'test_luks2_resize (crypto_test.CryptoTestResize)'
    test_desc = test.id().split(".")
    test_name = test_desc[-1]
    test_module = ".".join(test_desc[:-1])

    return test_name, test_module


def _print_skip_message(test, skip_tags, missing):
    test_id = test.id()
    test_module, test_name = _split_test_id(test_id)

    if missing:
        reason = 'skipping test because it is not tagged as one of: ' + ', '.join((t.value for t in skip_tags))
    else:
        reason = 'skipping test because it is tagged as: ' + ', '.join((t.value for t in skip_tags))

    if test._testMethodDoc:
        print("%s (%s)\n%s ... skipped '%s'" % (test_name, test_module, test._testMethodDoc, reason),
              file=sys.stderr)
    else:
        print("%s (%s) ... skipped '%s'" % (test_name, test_module, reason),
              file=sys.stderr)


class DebugTestResult(unittest.TextTestResult):

    def addError(self, test, err):
        traceback.print_exception(*err)
        pdb.post_mortem(err[2])
        super(DebugTestResult, self).addError(test, err)

    def addFailure(self, test, err):
        traceback.print_exception(*err)
        pdb.post_mortem(err[2])
        super(DebugTestResult, self).addFailure(test, err)


def _should_skip(distro=None, version=None, arch=None, reason=None):
    # all these can be lists or a single value, so convert everything to list
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
    os.environ['LIBBLOCKDEV_PROJ_DIR'] = projdir

    args = parse_args()
    if args.installed:
        os.environ['LIBBLOCKDEV_TESTS_SKIP_OVERRIDE'] = ''
        os.environ['LIBBLOCKDEV_CONFIG_DIR'] = '/etc/libblockdev/3/conf.d/'
    else:
        if 'LD_LIBRARY_PATH' not in os.environ and 'GI_TYPELIB_PATH' not in os.environ:
            os.environ['LD_LIBRARY_PATH'] = LIBDIRS
            os.environ['GI_TYPELIB_PATH'] = GIDIR
            os.environ['LIBBLOCKDEV_CONFIG_DIR'] = os.path.join(testdir, 'test_configs/default_config')
            os.environ['PATH'] += ':' + os.path.join(projdir, 'tools')

            try:
                os.execv(sys.executable, ['python3'] + sys.argv)
            except OSError as e:
                print('Failed re-exec with a new LD_LIBRARY_PATH and GI_TYPELIB_PATH: %s' % str(e))
                sys.exit(1)

        sys.path.append(testdir)
        sys.path.append(projdir)
        sys.path.append(os.path.join(projdir, 'src/python'))

    if not args.installed:
        import gi.overrides
        gi.overrides.__path__.insert(0, os.path.join(projdir, 'src/python/gi/overrides'))

    start_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()

    if args.testname:
        test_cases = loader.loadTestsFromNames(args.testname)
    else:
        test_cases = loader.discover(start_dir=testdir, pattern='*_test*.py')

    # extract list of test classes so we can check/run them manually one by one
    tests = set()
    tests = _get_tests_from_suite(test_cases, tests)
    tests = sorted(tests, key=lambda test: test.id())

    # get distro and arch here so we don't have to do this for every test
    DISTRO, VERSION = get_version()
    ARCH = os.uname()[-1]

    # get list of tests to skip from the config file
    skipping = _parse_skip_config(os.path.join(testdir, SKIP_CONFIG))

    # get sets of include/exclude tags as tags not strings from arguments
    include_tags = set(TestTags.get_tag_by_value(t) for t in args.include_tags)
    exclude_tags = set(TestTags.get_tag_by_value(t) for t in args.exclude_tags)

    for test in tests:
        test_id = test.id()

        # get tags and (possibly) skip the test
        tags = _get_test_tags(test)

        # if user specified include_tags, test must have at least one of these to run
        if include_tags and not (include_tags & tags):
            _print_skip_message(test, include_tags - tags, missing=True)
            continue

        # if user specified exclude_tags, test can't have any of these
        if exclude_tags and (exclude_tags & tags):
            _print_skip_message(test, exclude_tags & tags, missing=False)
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

    if args.pdb:
        runner = unittest.TextTestRunner(verbosity=2, failfast=args.stop, resultclass=DebugTestResult)
    else:
        runner = unittest.TextTestRunner(verbosity=2, failfast=args.stop)

    result = runner.run(suite)

    # dump cropped journal to log file
    if shutil.which('journalctl'):
        with open('journaldump.log', 'w') as outfile:
            subprocess.call(['journalctl', '-S', start_time], stdout=outfile)

    if result.wasSuccessful():
        sys.exit(0)
    else:
        sys.exit(1)
