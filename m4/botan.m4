# Set up to use either a bundled or a system-provided version of Botan.
#
# If --with-system-botan is specified and the library cannot be found or is
# unsuitable, the configure script will error out rather than falling back
# to the bundled version.  This is to avoid surprising a user who expected
# their system libbotan to be used.

AC_DEFUN([MTN_LIB_BOTAN],
[AC_ARG_WITH([system-botan],
    AC_HELP_STRING([--with-system-botan],
     [use a system-provided copy of Botan instead of the default bundled
      copy. (To use a specific installed version, use the environment
      variables BOTAN_CPPFLAGS and/or BOTAN_LIBS.)]),
    [case "$withval" in
      ""|yes) with_system_botan=yes ;;
      no)     with_system_botan=no  ;;
      *)      AC_MSG_ERROR([--with(out)-system-botan takes no argument]) ;;
    esac],
   [with_system_botan=no])
 if test "$with_system_botan" = yes; then
   MTN_FIND_BOTAN
 else
   AC_DEFINE([BOTAN_STATIC],[1],[Define if using bundled botan])
   AC_MSG_NOTICE([using the bundled copy of Botan])
 fi
 AM_CONDITIONAL([INCLUDED_BOTAN], [test $with_system_botan = no])
 AC_SUBST([BOTAN_CPPFLAGS])
 AC_SUBST([BOTAN_LIBS])
])

AC_DEFUN([MTN_FIND_BOTAN],
[
    BOTAN_CPPFLAGS="`botan1.7-config --cflags`"
    BOTAN_LIBS="`botan1.7-config --libs`"
])

