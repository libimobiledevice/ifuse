#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(ifuse, 0.9.2)
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# Checks for libraries.
PKG_CHECK_MODULES(libiphone, libiphone-1.0 >= 0.9.2)
PKG_CHECK_MODULES(libglib2, glib-2.0 >= 2.14.1)
PKG_CHECK_MODULES(libgthread2, gthread-2.0 >= 2.14.1)
PKG_CHECK_MODULES(libfuse, fuse >= 2.7.0)

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

AC_ARG_ENABLE([root-sbin],
              AS_HELP_STRING([--disable-root-sbin],
                             [Don't install mount helper in /sbin]),
              [without_root_sbin=yes],[without_root_sbin=no])
AM_CONDITIONAL([WITHOUT_ROOT_SBIN], test "x$without_root_sbin" = "xyes")

AC_OUTPUT(Makefile src/Makefile fdi/Makefile)
