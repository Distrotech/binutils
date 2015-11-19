dnl Copyright (C) 2014-2015 Free Software Foundation, Inc.
dnl
dnl This file is part of GDB.
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

dnl GDB_AC_BUILD_WITH_CXX()
dnl Provide an --enable-build-with-cxx/--disable-build-with-cxx set of options
dnl allowing a user to build with a C++ compiler.

AC_DEFUN([GDB_AC_BUILD_WITH_CXX],
[
  # The "doesn't support C++ yet" hall of shame.
  case $host in
    *-*aix* | \
    *-*go32* | \
    *-*darwin* | \
    *-*solaris* | \
    *-*nto* | \
    *-*bsd* | \
    xtensa*-*-linux* | \
    null)
      enable_build_with_cxx=no ;;
    *)
      enable_build_with_cxx=yes ;;
  esac

  AC_ARG_ENABLE(build-with-cxx,
  AS_HELP_STRING([--enable-build-with-cxx], [build with C++ compiler instead of C compiler]),
    [case $enableval in
      yes | no)
	  ;;
      *)
	  AC_MSG_ERROR([bad value $enableval for --enable-build-with-cxx]) ;;
    esac])

  if test "$enable_build_with_cxx" = "yes"; then
    COMPILER='$(CXX)'
   else
    COMPILER='$(CC)'
  fi
  AC_SUBST(COMPILER)
])
