# configure.ac for libblockdev

AC_INIT([libblockdev], [2.4], [vpodzime@redhat.com])

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
                          src/plugins/btrfs.h \
                          src/utils/Makefile \
                          src/lib/Makefile \
                          src/lib/plugin_apis/Makefile \
                          src/lib/plugin_apis/btrfs.api \
                          src/lib/blockdev.c \
                          src/lib/blockdev.pc \
                          src/python/Makefile \
                          src/python/gi/Makefile \
                          src/python/gi/overrides/Makefile \
                          include/Makefile \
                          include/blockdev/Makefile \
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
LIBBLOCKDEV_PKG_CHECK_MODULES([PARTED], [libparted >= 3.1])

LIBBLOCKDEV_CHECK_HEADER([volume_key/libvolume_key.h], [$GLIB_CFLAGS $NSS_CFLAGS], [libvolume_key.h not available])
LIBBLOCKDEV_CHECK_HEADER([dmraid/dmraid.h], [], [dmraid.h not available])

# older versions of parted don't provide the libparted-fs-resize.pc file
AS_IF([pkg-config --atleast-version=3.2 libparted],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([PARTED_FS], [libparted-fs-resize >= 3.2])],
      [AC_SUBST([PARTED_FS_LIBS], [-lparted-fs-resize])
       AC_SUBST([PARTED_FS_CFLAGS], [])])

LIBBLOCKDEV_PKG_CHECK_MODULES([BLKID], [blkid >= 2.23.0])

# older versions of libblkid don't support BLKID_SUBLKS_BADCSUM so let's just
# define it as 0 (neutral value for bit combinations of flags)
AS_IF([pkg-config --atleast-version=2.27.0 blkid], [],
      [AC_DEFINE([BLKID_SUBLKS_BADCSUM], [0],
         [Define as neutral value if libblkid doesn't provide the definition])])

LIBBLOCKDEV_PKG_CHECK_MODULES([BYTESIZE], [bytesize >= 0.1])

m4_ifdef([GOBJECT_INTROSPECTION_CHECK],
[GOBJECT_INTROSPECTION_CHECK([1.3.0])],
[found_introspection=no
AM_CONDITIONAL(HAVE_INTROSPECTION, false)])

uname -p|grep s390
on_s390=$?

AM_CONDITIONAL(ON_S390, test "$on_s390" = "0")

uname -p|grep aarch64
on_aarch64=$?

skip_patterns=""

AS_IF([test "$on_aarch64" = "0"], AC_SUBST([BTRFS_MIN_SIZE], ["(256 MiB)"]), AC_SUBST([BTRFS_MIN_SIZE], ["(128 MiB)"]))

# Complain if introspection was not enabled
AS_IF([test "x$found_introspection" = xyes], [:],
      [LIBBLOCKDEV_SOFT_FAILURE([GObject introspection (devel) must be installed and enabled])])

AC_SUBST([PYTHON_EXEC_PREFIX], ['${exec_prefix}'])
      PYTHON_EXECDIR=`python -c "import distutils.sysconfig; \
                                print(distutils.sysconfig.get_python_lib(1,0,prefix='${exec_prefix}'))"`
      AC_SUBST(pyexecdir, $PYTHON_EXECDIR)

AC_ARG_WITH([python3],
    AS_HELP_STRING([--with-python3], [support python3 @<:@default=check@:>@]),
    [],
    [with_python3=check])

AC_SUBST(WITH_PYTHON3, 0)
if test "x$with_python3" != "xno"; then
    AC_PATH_PROG([python3], [python3], [no])
    AS_IF([test "x$python3" == "xno"],
    [if test "x$with_python3" = "xyes"; then
      LIBBLOCKDEV_SOFT_FAILURE([Python3 support requested, but python3 is not available])
      fi],
    [AC_SUBST([PYTHON3_EXEC_PREFIX], ['${exec_prefix}'])
      PYTHON3_EXECDIR=`$python3 -c "import distutils.sysconfig; \
                                print(distutils.sysconfig.get_python_lib(1,0,prefix='${exec_prefix}'))"`
      AC_SUBST(py3execdir, $PYTHON3_EXECDIR)
      AC_SUBST(WITH_PYTHON3, 1)])
fi
AM_CONDITIONAL(WITH_PYTHON3, test "x$with_python3" != "xno" -a "x$python3" != "xno")


AC_ARG_WITH([gtk-doc],
    AS_HELP_STRING([--with-gtk-doc], [generate documentation with gtk-doc @<:@default=check@:>@]),
    [],
    [with_gtk_doc=check])

AC_SUBST(WITH_GTK_DOC, 0)
if test "x$with_gtk_doc" != "xno"; then
    AC_PATH_PROG([gtkdoc_scan], [gtkdoc-scan], [no])
    AS_IF([test "x$gtkdoc_scan" == "xno"],
    [if test "x$with_gtk_doc" = "xyes"; then
      LIBBLOCKDEV_SOFT_FAILURE([Building documentation with gtk-doc requested, but not available])
      fi],
      [AC_SUBST(WITH_GTK_DOC, 1)])
fi
AM_CONDITIONAL(WITH_GTK_DOC, test "x$with_gtk_doc" != "xno" -a "x$gtkdoc_scan" != "xno")

# Run tests on build?
AC_ARG_ENABLE([tests], AS_HELP_STRING([--enable-tests], [Run tests at compile time (default=yes)]))
test "x$enable_tests" = "x" && enable_tests="yes"
AM_CONDITIONAL([TESTS_ENABLED], [test "x$enable_tests" = "xyes"])

AC_CHECK_HEADERS([dlfcn.h string.h unistd.h sys/fcntl.h sys/ioctl.h linux/random.h glob.h syslog.h math.h],
                 [],
                 [LIBBLOCKDEV_SOFT_FAILURE([Header file $ac_header not found.])],
                 [])

AC_ARG_WITH([bcache],
    AS_HELP_STRING([--with-bcache], [support bcache @<:@default=yes@:>@]),
    [],
    [with_bcache=yes])

AC_SUBST([WITH_BCACHE], [0])
AM_CONDITIONAL(WITH_BCACHE, test "x$with_bcache" != "xno")
AS_IF([test "x$with_bcache" != "xno"],
      [AC_DEFINE([WITH_BD_BCACHE], [], [Define if bcache is supported]) AC_SUBST([WITH_BCACHE], [1]) AC_SUBST([WITH_BCACHE_CFLAGS], [-DWITH_BD_BCACHE])],
      [skip_patterns="$skip_patterns bcache"])

AC_SUBST([skip_patterns], [$skip_patterns])
AC_SUBST([MAJOR_VER], [\"2\"])

AC_OUTPUT
LIBBLOCKDEV_FAILURES
