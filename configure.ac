# configure.ac for libblockdev

AC_INIT([libblockdev], [1.5], [vpodzime@redhat.com])

# Disable building static libraries.
# This needs to be set before initializing automake
AC_DISABLE_STATIC

AM_INIT_AUTOMAKE([foreign -Wall -Werror -Wno-syntax -Wno-portability])
AC_CONFIG_MACRO_DIR([m4])

AM_PATH_PYTHON

AM_PROG_AR
AC_PROG_CC

LT_INIT

AC_CONFIG_FILES([Makefile src/Makefile \
                          src/plugins/Makefile \
                          src/utils/Makefile \
                          src/lib/Makefile \
                          src/lib/plugin_apis/Makefile \
                          src/lib/blockdev.c \
                          src/lib/blockdev.pc \
                          src/python/Makefile \
                          src/python/gi/Makefile \
                          src/python/gi/overrides/Makefile \
                          docs/Makefile \
                          docs/libblockdev-docs.xml \
                          dist/Makefile \
                          dist/libblockdev.spec \
                          scripts/Makefile \
                          data/Makefile \
                          data/conf.d/Makefile])

LIBBLOCKDEV_PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([GIO], [gio-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([CRYPTSETUP], [libcryptsetup >= 1.6.7])
LIBBLOCKDEV_PKG_CHECK_MODULES([NSS], [nss >= 3.18.0])
LIBBLOCKDEV_PKG_CHECK_MODULES([DEVMAPPER], [devmapper >= 1.02.93])
LIBBLOCKDEV_PKG_CHECK_MODULES([UDEV], [libudev >= 216])
LIBBLOCKDEV_PKG_CHECK_MODULES([KMOD], [libkmod >= 19])

m4_ifdef([GOBJECT_INTROSPECTION_CHECK],
[GOBJECT_INTROSPECTION_CHECK([1.3.0])],
[found_introspection=no
AM_CONDITIONAL(HAVE_INTROSPECTION, false)])

uname -p|grep s390
on_s390=$?

AM_CONDITIONAL(ON_S390, test "$on_s390" = "0")

# Complain if introspection was not enabled
AS_IF([test "x$found_introspection" = xyes], [:],
      [LIBBLOCKDEV_SOFT_FAILURE([GObject introspection must be enabled])])

AC_PATH_PROG([PYTHON3], [python3], [no])
AS_IF([test "x$PYTHON3" == "xno"], [LIBBLOCKDEV_SOFT_FAILURE([\
      The python3 program was not found in the search path. Please ensure that it is installed and its directory is included in the search path.])],
      [:])

AC_SUBST([PYTHON3_EXEC_PREFIX], ['${exec_prefix}'])
PYTHON3_EXECDIR=`$PYTHON3 -c "import distutils.sysconfig; \
                          print(distutils.sysconfig.get_python_lib(1,0,prefix='$PYTHON3_EXEC_PREFIX'))"`
AC_SUBST(py3execdir, $PYTHON3_EXECDIR)
AM_CONDITIONAL(USE_PYTHON3, test ${use_python3} = "yes")

# Run tests on build?
AC_ARG_ENABLE([tests], AS_HELP_STRING([--enable-tests], [Run tests at compile time (default=yes)]))
test "x$enable_tests" = "x" && enable_tests="yes"
AM_CONDITIONAL([TESTS_ENABLED], [test "x$enable_tests" = "xyes"])

AC_CHECK_HEADERS([dlfcn.h string.h unistd.h sys/fcntl.h sys/ioctl.h linux/random.h glob.h syslog.h math.h],
                 [],
                 [LIBBLOCKDEV_SOFT_FAILURE([Header file $ac_header not found.])],
                 [])

AC_SUBST([MAJOR_VER], [\"0\"])

AC_OUTPUT
LIBBLOCKDEV_FAILURES
