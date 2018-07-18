dnl autoconf macros for libblockdev
dnl
dnl Copyright (C) 2014  Red Hat, Inc.
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU Lesser General Public License as published
dnl by the Free Software Foundation; either version 2.1 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU Lesser General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.
dnl
dnl THIS IS A MODIFIED VERSION OF THE ANACONDA'S acinclude.m4 FILE.
dnl
dnl Author: David Shea <dshea@redhat.com>
dnl         Vratislav Podzimek <vpodzime@redhat.com>

dnl LIBBLOCKDEV_SOFT_FAILURE(MESSAGE)
dnl
dnl Store a message that in some contexts could be considered indicative
dnl of a failure, but in other contexts could be indicative of who cares.
dnl
dnl Any message sent to this macro will be stored, and they can all be
dnl displayed at the end of configure using the LIBBLOCKDEV_FAILURES macro.
AC_DEFUN([LIBBLOCKDEV_SOFT_FAILURE], [dnl
AS_IF([test x"$libblockdev_failure_messages" = x],
    [libblockdev_failure_messages="[$1]"],
    [libblockdev_failure_messages="$libblockdev_failure_messages
[$1]"
])])dnl

dnl LIBBLOCKDEV_PKG_CHECK_MODULES(VARIABLE-PREFIX, MODULES)
dnl
dnl Check whether a module is available, using pkg-config. Instead of failing
dnl if a module is not found, store the failure in a message that can be
dnl printed using the LIBBLOCKDEV_FAILURES macro.
dnl
dnl The syntax and behavior of VARIABLE-PREFIX and MODULES is the same as for
dnl PKG_CHECK_MODULES.
AC_DEFUN([LIBBLOCKDEV_PKG_CHECK_MODULES], [dnl
PKG_CHECK_MODULES([$1], [$2], [], [LIBBLOCKDEV_SOFT_FAILURE($[$1]_PKG_ERRORS)])
])dnl

dnl LIBBLOCKDEV_PKG_CHECK_EXISTS(MODULES)
dnl
dnl Check whether a module exists, using pkg-config. Instead of failing
dnl if a module is not found, store the failure in a message that can be
dnl printed using the LIBBLOCKDEV_FAILURES macro.
dnl
dnl The syntax and behavior of MODULES is the same as for
dnl PKG_CHECK_EXISTS.
AC_DEFUN([LIBBLOCKDEV_PKG_CHECK_EXISTS], [dnl
PKG_CHECK_EXISTS([$1], [], [LIBBLOCKDEV_SOFT_FAILURE([Check for $1 failed])])
])dnl

dnl LIBBLOCKDEV_CHECK_HEADER(HEADER, CFLAGS, ERR_MSG)
dnl
dnl Check if the given HEADER exists and is usable -- gcc can compile a source
dnl file that just includes the HEADER using the given CFLAGS. In case of
dnl failure, the ERR_MSG will be printed using the LIBBLOCKDEV_FAILURES macro.
AC_DEFUN([LIBBLOCKDEV_CHECK_HEADER], [dnl
echo -n "Checking header [$1] existence and usability..."
temp_file=$(mktemp --tmpdir XXXXX.c)
echo "#include <$1>" > $temp_file
${CC} -c [$2] $temp_file
status=$?
rm -f $temp_file
rm -f $(basename ${temp_file%%.c}.o)
if test $status = 0; then
  echo yes
else
  echo no
  libblockdev_failure_messages="$libblockdev_failure_messages
[$3]"
fi
])dnl


dnl LIBBLOCKDEV_PLUGIN(NAME, name)
dnl
dnl Define things needed in Makefile.am`s as well as sources for making
dnl compilation and build modular.
AC_DEFUN([LIBBLOCKDEV_PLUGIN], [dnl
AC_ARG_WITH([$2],
    AS_HELP_STRING([--with-$2], [support $2 @<:@default=yes@:>@]),
    [],
    [with_$2=yes])

AC_SUBST([WITH_$1], [0])
AM_CONDITIONAL(WITH_$1, test "x$with_$2" != "xno")
AS_IF([test "x$with_$2" != "xno"],
      [AC_DEFINE([WITH_BD_$1], [], [Define if $2 is supported]) AC_SUBST([WITH_$1], [1])],
      [])
])dnl


dnl LIBBLOCKDEV_FAILURES
dnl
dnl Print the failure messages collected by LIBBLOCKDEV_SOFT_FAILURE,
dnl LIBBLOCKDEV_PKG_CHECK_MODULES and LIBBLOCKDEV_CHECK_HEADER
AC_DEFUN([LIBBLOCKDEV_FAILURES], [dnl
AS_IF([test x"$libblockdev_failure_messages" = x], [], [dnl
echo ""
echo "*** Libblockdev encountered the following issues during configuration:"
echo "$libblockdev_failure_messages"
echo ""
echo "*** Libblockdev will not successfully build without these missing dependencies"
AS_EXIT(1)
])])dnl
