# -*- mode: shell-script-mode -*-
#
# Copyright (c) 2014-2016 Los Alamos National Security, LLC. All rights
#                         reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

AC_PREREQ([2.69])
AC_INIT([libhio], [1.4], [hjelmn@lanl.gov])
AC_CONFIG_SRCDIR([src/hio_context.c])
AC_CONFIG_HEADERS([src/include/hio_config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CHECK_PROG(MPICC, [mpicc], [mpicc], [none])
AC_CHECK_PROG(MPICXX, [mpicxx], [mpicxx], [none])
AM_SILENT_RULES([yes])

hio_use_mpi=0

if ( test -n "$XTOS_VERSION" || test -n "$CRAYPE_DIR" ) && test ! "$CC" ; then
    # Work around Cray idiosyncrasies
    # Don't let the Cray wrapper bring in extra crap
    PE_VERSION=`echo $XTOS_VERSION | sed 's/\..*//g'`
    # force the cray wrappers to act in a predictable way
    if test x$PE_VERSION = x4 ; then
        CC="cc -target=native -dynamic"
        CXX="CC -target=native -dynamic"
    else
        CC="cc --cray-bypass-pkgconfig -dynamic"
        CXX="CC --cray-bypass-pkgconfig -dynamic"
    fi
    # We may be cross-compiling so trick configure
    cross_compiling=yes
    if test ! -n "$LD" ; then
        LD=ld
    fi
elif test ${MPICC} = "mpicc" ; then
    CC=${MPICC}
    CXX=${MPICXX}
    hio_use_mpi=1
fi

AM_INIT_AUTOMAKE([serial-tests])
LT_INIT

HIO_CHECK_JSON
HIO_CHECK_DATAWARP
HIO_CHECK_XPMEM
HIO_CHECK_LUSTRE
HIO_CHECK_CVERSION
HIO_CHECK_BZ2

if test $hio_use_mpi = 0 ; then
    AC_MSG_CHECKING([for craypich])
    if test -n "$CRAY_MPICH2_DIR" ; then
        AC_MSG_RESULT([yes])
        CPPFLAGS="$CPPFLAGS -I$CRAY_MPICH2_DIR/include"
        LDFLAGS="$LDFLAGS -L$CRAY_MPICH2_DIR/lib"
        LIBS="$LIBS -lmpich"
        hio_use_mpi=1
    else
        AC_MSG_RESULT([no])
    fi
fi

if test $hio_use_mpi ; then
   HIO_CHECK_MPI_FEATURES
fi

# Checks for programs.
AC_PROG_CC_C99

AC_PROG_LEX

AC_ARG_VAR([HAVE_PDFLATEX], [Whether pdflatex exists and is executable])
AC_CHECK_PROG([HAVE_PDFLATEX], [pdflatex], [yes], [no])

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS_ONCE([strings.h sys/types.h sys/time.h pthread.h dlfcn.h sys/stat.h \
                       sys/param.h sys/mount.h sys/vfs.h bzlib.h])
AC_CHECK_FUNCS_ONCE([access gettimeofday stat statfs MPI_Win_allocate_shared \
                     MPI_Comm_split_type MPI_Win_flush])
AC_SEARCH_LIBS([dlopen],[dl],[hio_dynamic_component=1],[hio_dynamic_component=0])

AX_PTHREAD([])

LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$PTHREAD_CFLAGS $CFLAGS"

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL

AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AC_DEFINE_UNQUOTED([HIO_USE_MPI], [$hio_use_mpi], [Whether to use mpi with hio])
AC_DEFINE_UNQUOTED([USE_DYNAMIC_COMPONENTS], [$hio_dynamic_component], [Whether to use dynamic components])
AC_DEFINE([_GNU_SOURCE], [1], [Define _GNU_SOURCE feature macro])
AC_DEFINE_UNQUOTED([HIO_PREFIX], ["$prefix"], [HIO install prefix])
AM_CONDITIONAL([HAVE_PDFLATEX], [test $HAVE_PDFLATEX = yes])
AM_CONDITIONAL([HAVE_MPI], [test $hio_use_mpi = 1])

AC_SUBST([HIO_PKGCONFIG_REQUIRES], [$hio_pkgconfig_requires])

AC_SUBST(HIO_BUILD_TIMESTAMP,[$(date "+%A %B %d, %Y at %l:%M %p %Z")])

# Checks for library functions.

AC_CONFIG_FILES([Makefile design/Makefile src/Makefile test/Makefile libhio.pc design/doxygen.conf design/template/header.tex])

AC_OUTPUT
