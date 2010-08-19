
dnl Require autoconf version >= 2.57
AC_PREREQ(2.57)


dnl ############# Initialization

AC_INIT([silentjack], [0.2], [njh@aelius.com])

AC_CONFIG_SRCDIR(silentjack.c)
AC_CANONICAL_SYSTEM

dnl Version 1.7 of automake is recommended
AM_INIT_AUTOMAKE(1.7)
AM_CONFIG_HEADER(config.h)



dnl ############# Compiler and tools Checks

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_C_INLINE



dnl ############## Library Checks

AC_CHECK_LIB([m], [sqrt], , [AC_MSG_ERROR(Can't find libm)])
AC_CHECK_LIB([mx], [powf])

# Check for JACK (need 0.100.0 for jack_client_open)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)



dnl ############## Header Checks

AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])


dnl ############## Function Checks
AC_CHECK_FUNCS( atexit usleep )



dnl ############## Final Output

AC_OUTPUT([Makefile])

