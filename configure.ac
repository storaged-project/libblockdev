# configure.ac for libblockdev

AC_INIT([libblockdev], [2.15], [vpodzime@redhat.com])

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
                          src/plugins/fs/Makefile \
                          src/utils/Makefile \
                          src/utils/blockdev-utils.pc \
                          src/lib/Makefile \
                          src/lib/plugin_apis/Makefile \
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

m4_ifdef([GOBJECT_INTROSPECTION_CHECK],
[GOBJECT_INTROSPECTION_CHECK([1.3.0])],
[found_introspection=no
AM_CONDITIONAL(HAVE_INTROSPECTION, false)])
AS_IF([test "x$found_introspection" = xyes],
      [AC_SUBST(WITH_GI, 1)],
      [AC_SUBST(WITH_GI, 0)])

AC_CANONICAL_HOST
case $host_cpu in
  s390|s390x) on_s390=yes ;;
  *) on_s390=no ;;
esac

skip_patterns=""

AC_ARG_WITH([s390],
    AS_HELP_STRING([--with-s390], [support s390 @<:@default=check@:>@]),
    [],
    [with_s390=check])

AC_SUBST(WITH_S390, 0)
if test "x$with_s390" != "xno"; then
    AS_IF([test "x$on_s390" = "xno" -a "x$with_s390" = "xyes"],
           [LIBBLOCKDEV_SOFT_FAILURE([s390 support requested, but not on s390 arch])
            AM_CONDITIONAL(WITH_S390, false)],
          [test "x$on_s390" == "xyes" -a "x$with_s390" != "xno"],
           [AC_SUBST(WITH_S390, 1)
            AM_CONDITIONAL(WITH_S390, true)
           ],
          [AC_SUBST(WITH_S390, 0)
           AM_CONDITIONAL(WITH_S390, false)
          ])
else
  AM_CONDITIONAL(WITH_S390, false)
fi

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
    [AC_SUBST(WITH_PYTHON3, 1)])
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
      [AC_DEFINE([WITH_BD_BCACHE], [], [Define if bcache is supported]) AC_SUBST([WITH_BCACHE], [1])],
      [])

LIBBLOCKDEV_PLUGIN([BTRFS], [btrfs])
LIBBLOCKDEV_PLUGIN([CRYPTO], [crypto])
LIBBLOCKDEV_PLUGIN([DM], [dm])
LIBBLOCKDEV_PLUGIN([LOOP], [loop])
LIBBLOCKDEV_PLUGIN([LVM], [lvm])
LIBBLOCKDEV_PLUGIN([LVM_DBUS], [lvm_dbus])
LIBBLOCKDEV_PLUGIN([MDRAID], [mdraid])
LIBBLOCKDEV_PLUGIN([MPATH], [mpath])
LIBBLOCKDEV_PLUGIN([SWAP], [swap])
LIBBLOCKDEV_PLUGIN([KBD], [kbd])
LIBBLOCKDEV_PLUGIN([PART], [part])
LIBBLOCKDEV_PLUGIN([FS], [fs])

AM_CONDITIONAL(WITH_PART_O_WITH_FS, test "x$with_part" != "xno" -o "x$with_fs" != "xno")

dnl these two modules are always needed
LIBBLOCKDEV_PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([GIO], [gio-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([UDEV], [libudev >= 216])
LIBBLOCKDEV_PKG_CHECK_MODULES([KMOD], [libkmod >= 19])

AS_IF([test "x$with_crypto" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([CRYPTSETUP], [libcryptsetup >= 1.6.7])
       LIBBLOCKDEV_PKG_CHECK_MODULES([NSS], [nss >= 3.18.0])
       LIBBLOCKDEV_CHECK_HEADER([volume_key/libvolume_key.h], [$GLIB_CFLAGS $NSS_CFLAGS], [libvolume_key.h not available])
      ],
      [])

AS_IF([test "x$with_dm" != "xno" -o "x$with_lvm" != "xno" -o "x$with_lvm_dbus" != "xno" -o "x$with_mpath" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([DEVMAPPER], [devmapper >= 1.02.93])],
      [])

AS_IF([test "x$with_dm" != "xno"],
      [LIBBLOCKDEV_CHECK_HEADER([dmraid/dmraid.h], [], [dmraid.h not available])],
      [])

AS_IF([test "x$with_part" != "xno" -o "x$with_fs" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([PARTED], [libparted >= 3.1])]
      [])

AS_IF([test "x$with_fs" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([MOUNT], [mount >= 2.23.0])
       # new versions of libmount has some new functions we can use
       AS_IF([$PKG_CONFIG --atleast-version=2.30.0 mount],
             [AC_DEFINE([LIBMOUNT_NEW_ERR_API])], [])

       LIBBLOCKDEV_PKG_CHECK_MODULES([BLKID], [blkid >= 2.23.0])
       # older versions of libblkid don't support BLKID_SUBLKS_BADCSUM so let's just
       # define it as 0 (neutral value for bit combinations of flags)
       AS_IF([$PKG_CONFIG --atleast-version=2.27.0 blkid], [],
             [AC_DEFINE([BLKID_SUBLKS_BADCSUM], [0],
              [Define as neutral value if libblkid doesn't provide the definition])])

       # older versions of parted don't provide the libparted-fs-resize.pc file
       AS_IF([$PKG_CONFIG libparted-fs-resize],
             [LIBBLOCKDEV_PKG_CHECK_MODULES([PARTED_FS], [libparted-fs-resize >= 3.2])],
             [AC_SUBST([PARTED_FS_LIBS], [-lparted-fs-resize])
              AC_SUBST([PARTED_FS_CFLAGS], [])])],
      [])

AS_IF([test "x$with_btrfs" != "xno" -o "x$with_mdraid" != "xno" -o "x$with_kbd" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([BYTESIZE], [bytesize >= 0.1])],
      [])

if test "x$with_mpath" != "xno"; then
    LIBBLOCKDEV_PKG_CHECK_MODULES([LIBDMMP], [libdmmp])
    # Upstream of libdmmp forgot to bump version for new below two functions
    # We may remove it once libdmmp.pc has version bump to 0.2.0
    AC_CHECK_LIB(dmmp, dmmp_flush_mpath, [],
                 AC_MSG_ERROR([Please upgrade your \
libdmmp/device-mapper-multipath/multipath-tools to 0.7.3 or later]))
    AC_CHECK_LIB(dmmp, dmmp_reconfig, [],
                 AC_MSG_ERROR([Please upgrade your \
libdmmp/device-mapper-multipath/multipath-tools to 0.7.3 or later]))
fi

AC_SUBST([skip_patterns], [$skip_patterns])
AC_SUBST([MAJOR_VER], [\"2\"])

CFLAGS="$CFLAGS -std=gnu99"

AC_OUTPUT
LIBBLOCKDEV_FAILURES
