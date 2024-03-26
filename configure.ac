# configure.ac for libblockdev

AC_INIT([libblockdev], [3.1.1], [], [], [https://github.com/storaged-project/libblockdev])

# Disable building static libraries.
# This needs to be set before initializing automake
AC_DISABLE_STATIC

AM_INIT_AUTOMAKE([foreign -Wall -Werror -Wno-syntax -Wno-portability])
AC_CONFIG_MACRO_DIR([m4])

AM_PATH_PYTHON

AX_CHECK_ENABLE_DEBUG([no], [DEBUG], [NDEBUG])

AM_PROG_AR
AC_PROG_CC

LT_INIT

AC_CONFIG_FILES([Makefile src/Makefile \
                          src/plugins/Makefile \
                          src/plugins/fs/Makefile \
                          src/plugins/nvme/Makefile \
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
                          tools/Makefile \
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

AC_ARG_WITH([s390],
    AS_HELP_STRING([--with-s390], [support s390 @<:@default=check@:>@]),
    [],
    [with_s390=check])

AC_SUBST(WITH_S390, 0)
if test "x$with_s390" != "xno"; then
    AS_IF([test "x$on_s390" = "xno" -a "x$with_s390" = "xyes"],
           [LIBBLOCKDEV_SOFT_FAILURE([s390 support requested, but not on s390 arch])
            AM_CONDITIONAL(WITH_S390, false)],
          [test "x$on_s390" = "xyes" -a "x$with_s390" != "xno"],
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
    AS_IF([test "x$python3" = "xno"],
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
    AS_IF([test "x$gtkdoc_scan" = "xno"],
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

AC_ARG_WITH([escrow],
    AS_HELP_STRING([--with-escrow], [support escrow @<:@default=yes@:>@]),
    [],
    [with_escrow=yes])

AC_SUBST([WITH_ESCROW], [0])
AM_CONDITIONAL(WITH_ESCROW, test "x$with_escrow" != "xno")
AS_IF([test "x$with_escrow" != "xno"],
      [AC_DEFINE([WITH_BD_ESCROW], [], [Define if escrow is supported]) AC_SUBST([WITH_ESCROW], [1])],
      [])

AC_ARG_WITH([tools],
    AS_HELP_STRING([--with-tools], [build tools @<:@default=yes@:>@]),
    [],
    [with_tools=yes])

AC_SUBST([WITH_TOOLS], [0])
AM_CONDITIONAL(WITH_TOOLS, test "x$with_tools" != "xno")
AS_IF([test "x$with_tools" != "xno"],
      [AC_SUBST([WITH_TOOLS], [1])],
      [])

AC_ARG_WITH([nvme],
    AS_HELP_STRING([--with-nvme], [support nvme @<:@default=yes@:>@]),
    [],
    [with_nvme=yes])

AC_SUBST([WITH_NVME], [0])
AM_CONDITIONAL(WITH_NVME, test "x$with_nvme" != "xno")
AS_IF([test "x$with_nvme" != "xno"],
      [AC_DEFINE([WITH_BD_NVME], [], [Define if nvme is supported]) AC_SUBST([WITH_NVME], [1])],
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
LIBBLOCKDEV_PLUGIN([PART], [part])
LIBBLOCKDEV_PLUGIN([FS], [fs])
LIBBLOCKDEV_PLUGIN([NVDIMM], [nvdimm])
LIBBLOCKDEV_PLUGIN([NVME], [nvme])

dnl these packages/modules are always needed
LIBBLOCKDEV_PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([GIO], [gio-2.0 >= 2.42.2])
LIBBLOCKDEV_PKG_CHECK_MODULES([UDEV], [libudev >= 216])
LIBBLOCKDEV_PKG_CHECK_MODULES([KMOD], [libkmod >= 19])

AS_IF([test "x$with_crypto" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([CRYPTSETUP], [libcryptsetup >= 2.3.0])
      AS_IF([$PKG_CONFIG --atleast-version=2.4.0 libcryptsetup],
            [AC_DEFINE([LIBCRYPTSETUP_24])], [])
      AS_IF([$PKG_CONFIG --atleast-version=2.6.0 libcryptsetup],
            [AC_DEFINE([LIBCRYPTSETUP_26])], [])
      AS_IF([test "x$with_escrow" != "xno"],
            [LIBBLOCKDEV_PKG_CHECK_MODULES([NSS], [nss >= 3.18.0])
             LIBBLOCKDEV_CHECK_HEADER([volume_key/libvolume_key.h], [$GLIB_CFLAGS $NSS_CFLAGS], [libvolume_key.h not available])],
            [])
      dnl pkg-config support was added in keyutils 1.6 but CentOS/RHEL 8 has only 1.5
      LIBBLOCKDEV_CHECK_HEADER([keyutils.h], [], [keyutils.h not available])
      ],
      [])

AS_IF([test "x$with_dm" != "xno" -o "x$with_lvm" != "xno" -o "x$with_lvm_dbus" != "xno" -o "x$with_mpath" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([DEVMAPPER], [devmapper >= 1.02.93])],
      [])

AS_IF([test "x$with_part" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([FDISK], [fdisk >= 2.31.0])],
      [])

AS_IF([test "x$with_swap" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([UUID], [uuid])],
      [])

AS_IF([test "x$with_fs" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([UUID], [uuid])
       LIBBLOCKDEV_PKG_CHECK_MODULES([MOUNT], [mount >= 2.23.0])
       # new versions of libmount has some new functions we can use
       AS_IF([$PKG_CONFIG --atleast-version=2.30.0 mount],
             [AC_DEFINE([LIBMOUNT_NEW_ERR_API])], [])
       LIBBLOCKDEV_PKG_CHECK_MODULES([EXT2FS], [ext2fs e2p])]
      [])

AS_IF([test "x$with_tools" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([PARTED], [libparted >= 3.1])

       # older versions of parted don't provide the libparted-fs-resize.pc file
       AS_IF([$PKG_CONFIG libparted-fs-resize],
             [LIBBLOCKDEV_PKG_CHECK_MODULES([PARTED_FS], [libparted-fs-resize >= 3.2])],
             [AC_SUBST([PARTED_FS_LIBS], [-lparted-fs-resize])
              AC_SUBST([PARTED_FS_CFLAGS], [])])],
      [])

AS_IF([test "x$with_fs" != "xno" -o "x$with_crypto" != "xno" -o "x$with_swap" != "xno" -o "x$with_part" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([BLKID], [blkid >= 2.23.0])
      # older versions of libblkid don't support BLKID_SUBLKS_BADCSUM so let's just
      # define it as 0 (neutral value for bit combinations of flags)
      AS_IF([$PKG_CONFIG --atleast-version=2.27.0 blkid], [],
            [AC_DEFINE([BLKID_SUBLKS_BADCSUM], [0],
             [Define as neutral value if libblkid doesn't provide the definition])])]
      [])

AS_IF([test "x$with_btrfs" != "xno" -o "x$with_mdraid" != "xno" -o "x$with_tools" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([BYTESIZE], [bytesize >= 0.1])],
      [])

AS_IF([test "x$with_nvdimm" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([UUID], [uuid])
       LIBBLOCKDEV_PKG_CHECK_MODULES([NDCTL], [libndctl >= 58.4])
       # new versions of libndctl new modes
       AS_IF([$PKG_CONFIG --atleast-version=60 libndctl],
             [AC_DEFINE([LIBNDCTL_NEW_MODES])], [])]
      [])

AS_IF([test "x$with_nvme" != "xno"],
      [LIBBLOCKDEV_PKG_CHECK_MODULES([NVME], [libnvme >= 1.3])
      AS_IF([$PKG_CONFIG --atleast-version=1.4 libnvme],
            [AC_DEFINE([HAVE_LIBNVME_1_4])], [])
      ],
      [])

AC_SUBST([MAJOR_VER], [3])

CFLAGS="$CFLAGS -std=gnu99"

LIBBLOCKDEV_FAILURES
AC_OUTPUT


if test "x$with_python3" != "xno" -a "x$python3" != "xno"; then
  python3_info=yes
else
  python3_info=no
fi

if test "x$with_s390" != "xno" -a "x$on_s390" = "xyes"; then
  s390_info=yes
else
  s390_info=no
fi

dnl ==========================================================================
echo "
               libblockdev $VERSION
             ====================

        prefix:                     ${prefix}
        libdir:                     ${libdir}
        libexecdir:                 ${libexecdir}
        bindir:                     ${bindir}
        sbindir:                    ${sbindir}
        datadir:                    ${datadir}
        sysconfdir:                 ${sysconfdir}
        localstatedir:              ${localstatedir}
        docdir:                     ${docdir}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}
        ldflags:                    ${LDFLAGS}

        BTRFS plugin:               ${with_btrfs}
        Crypto plugin:              ${with_crypto}
            escrow support:         ${with_escrow}
        DM plugin:                  ${with_dm}
        FS plugin:                  ${with_fs}
        Loop plugin:                ${with_loop}
        LVM plugin:                 ${with_lvm}
        LVM DBus plugin:            ${with_lvm_dbus}
        MDRAID plugin:              ${with_mdraid}
        MPath plugin                ${with_mpath}
        NVDIMM plugin (deprecated): ${with_nvdimm}
        NVMe plugin:                ${with_nvme}
        Part plugin:                ${with_part}
        S390 plugin:                ${s390_info}
        Swap plugin:                ${with_swap}

        GObject introspection:      ${found_introspection}
        Python 3 bindings:          ${python3_info}
        tools:                      ${with_tools}
"
