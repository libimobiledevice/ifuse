#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(ifuse, 1.1.2)
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# Checks for libraries.
PKG_CHECK_MODULES(libimobiledevice, libimobiledevice-1.0 >= 1.0.0)
PKG_CHECK_MODULES(libimobiledevice11, libimobiledevice-1.0 >= 1.1.0, libimobiledevice_1_1=yes, libimobiledevice_1_1=no)
if test x"$libimobiledevice_1_1" = xyes; then
  AC_DEFINE([HAVE_LIBIMOBILEDEVICE_1_1], 1, [Define if libimobiledevice is using 1.1.0 API])
fi
libimobiledevice_VERSION=`$PKG_CONFIG --modversion "libimobiledevice-1.0" 2>&1`
PKG_CHECK_MODULES(libfuse, fuse >= 2.7.0)
PKG_CHECK_MODULES(libplist, libplist)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([strcasecmp strdup strerror strndup])

AC_OUTPUT(Makefile src/Makefile docs/Makefile)

echo "
Configuration for $PACKAGE $VERSION:
-------------------------------------------

  Install prefix ..........: $prefix
  libimobiledevice API ....: $libimobiledevice_VERSION

  Now type 'make' to build $PACKAGE $VERSION,
  and then 'make install' for installation.
"
