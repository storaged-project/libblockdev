#!/usr/bin/python3

from __future__ import print_function

import argparse
import datetime
import os
import six
import subprocess
import sys
import unittest

LIBDIRS = 'src/utils/.libs:src/plugins/.libs:src/plugins/fs/.libs:src/lib/.libs'
GIDIR = 'src/lib'


if __name__ == '__main__':

    testdir = os.path.abspath(os.path.dirname(__file__))
    projdir = os.path.abspath(os.path.normpath(os.path.join(testdir, '..')))

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
    argparser.add_argument('-s', '--stop', dest='stop',
                           help='stop executing after first failed test',
                           action='store_true')
    args = argparser.parse_args()

    if args.fast:
        os.environ['SKIP_SLOW'] = ''
    if args.lucky:
        os.environ['FEELINGLUCKY'] = ''
    if args.jenkins:
        os.environ['JENKINS_HOME'] = ''

    sys.path.append(testdir)
    sys.path.append(projdir)
    sys.path.append(os.path.join(projdir, 'src/python'))

    start_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    suite = unittest.TestSuite()
    if args.testname:
        loader = unittest.TestLoader()
        tests = loader.loadTestsFromNames(args.testname)
        suite.addTests(tests)
    else:
        loader = unittest.TestLoader()
        tests = loader.discover(start_dir=testdir, pattern='*_test*.py')
        suite.addTests(tests)
    result = unittest.TextTestRunner(verbosity=2, failfast=args.stop).run(suite)

    # dump cropped journal to log file
    with open('journaldump.log', 'w') as outfile:
        subprocess.call(['journalctl', '-S', start_time], stdout=outfile)

    if result.wasSuccessful():
        sys.exit(0)
    else:
        sys.exit(1)
