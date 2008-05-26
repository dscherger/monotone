# Set up to use either system-provided or bundled versions of
# third-party libraries which can be probed for by pkg-config
# and/or custom *-config executables.

# This is more complicated than it ought to be because (a) we can
# assume that each of these libraries installs either a .pc file or a
# *-config executable, but which one may vary across versions and
# installations, and (b) we can't count on the .pc file, or the
# *-config executable, to have the same name in all versions or
# installations!

# All is not lost, however; we _can_ count on the .pc file or *-config
# executable to have a basename that matches the Perl regular
# expression /^(lib)?\Q${libname}\E(-?[0-9.]+)?(\.pc|-config)$/i,
# where ${libname} is the short name of the library.  Thus, we can
# enumerate all the possibilities for any given library.  This is easy
# for *-config executables (iterate over $PATH) and slightly trickier
# for .pc files (iterate over PKG_CONFIG_PATH, but also we have to
# determine pkg-config's built-in path, which is nontrivial)

AC_DEFUN([MTN_FULL_PKG_CONFIG_PATH],
[AC_REQUIRE([PKG_PROG_PKG_CONFIG])
# The dummy "pkg-config" package is guaranteed to exist.
if test -n "$PKG_CONFIG"; then
  FULL_PKG_CONFIG_PATH=`$PKG_CONFIG --debug pkg-config 2>&1 |
    sed -ne "/^Scanning directory '/{; s///; s/'$//; p;}" | 
    tr "$as_nl" ':' | sed 's/:$//'`
fi
])

# Subroutine of MTN_3RDPARTY_LIB, for readability only.
AC_DEFUN([MTN_VERSION_AT_LEAST],
[( echo "$1"; echo "$2" ) | tr -c "$as_cr_alnum$as_nl" ' ' |
$AWK 'NR==1 {
       for (i = 1; i <= NF; i++) 
         v[i] = $i; 
      NV = NF;
     }
     NR==2 {
       for(i = 1; i <= NF; i++)
         c[i] = $i;

	     if (NV < NF) {
         for (i = NV+1; i <= NF; i++)
           v[i] = 0;
         NV = NF;
       } else if (NV > NF) {
         for (i = NF+1; i <= NV; i++)
           c[i] = 0;
       }
     }
     END {
       if (NR != 2)
         print "ERROR"
       else {
         win = 0;
         for(i=1; i<=NV; i++) {
           if (c[i] > v[i]) {
             win = 1;
             break;
           } else if (c[i] < v[i]) {
             break;
           }
         }
         if (win == 1 || i == NV+1)
           print "true"
         else
           print "false"
       }
     }'])

# MTN_3RDPARTY_LIB(libname)
# Adds to CPPFLAGS and LIBS as necessary to compile and link with LIBNAME.
AC_DEFUN([MTN_3RDPARTY_LIB],
[AC_REQUIRE([MTN_FULL_PKG_CONFIG_PATH])
AC_REQUIRE([AC_PROG_EGREP])
AC_REQUIRE([AC_PROG_AWK])

# Determine the bundled version of $1.
dnl The quotation marks in the middle of AC_INIT and m4_define ensure
dnl that they will neither expand nor trip up the unexpanded-macro check.
_libname=$1
dnl pcre and botan are special
m4_if($1, [pcre],
[eval `sed -ne 's/^m4''_define(pcre_\(major\|minor\|prerelease\), \@<:@\(.*\)\@:>@)$/_\1="\2"/p' ${srcdir}/../pcre/configure.ac`
_version=${_major}.${_minor}${_prerelease}],
[m4_if($1, [botan],
[eval `sed -ne 's/^my \$\(\MAJOR\|MINOR\|PATCH\)_VERSION = \(.*\);/_\1=\2/p' ${srcdir}/../botan/configure.pl`
if test x${_PATCH} = x; then _PATCH=0; fi
_version=${_MAJOR}.${_MINOR}.${_PATCH}],
[_version=`grep AC''_INIT ${srcdir}/../$_libname/configure.ac | cut -d, -f2`])])
_notfound=true

# Try a "naive" pkg-config operation first.  This also ensures that the
# ARG_VARs for the desired library are sane.
PKG_CHECK_MODULES([$1], [$1 >= $_version],
  [_notfound=false
   CPPFLAGS="$CPPFLAGS $[]$1[]_CFLAGS"
   LIBS="$LIBS $[]$1[]_LIBS"],
  [:])

# Second, try looking for alternative names known to pkg-config for
# the library.
if $_notfound; then
  _save_IFS="$IFS"
  IFS=":"
  set fnord $FULL_PKG_CONFIG_PATH
  shift
  IFS="$_save_IFS"

  for pkgcfgdir; do
    echo $pkgcfgdir/*${_libname}*.pc
  done | tr ' ' "$as_nl" | 
  $EGREP '/(lib)?'${_libname}'(-?@<:@0-9.@:>@+)?\.pc$' > conftest.candpc
  while read f; do
    c=`AS_BASENAME([$f])`
    c=`expr X"$c" : 'X\(.*\)\.pc'`
    AC_MSG_NOTICE([trying $c])
    PKG_CHECK_MODULES([$1], [$c >= $_version],
      [CPPFLAGS="$CPPFLAGS $[$1]_CFLAGS"
     LIBS="$LIBS $[$1]_LIBS"
     _notfound=false
     break],
     [:])
  done < conftest.candpc
fi

# If that didn't find anything usable, try -config binaries.
if $_notfound; then
  _save_IFS="$IFS"
  IFS=":"
  set fnord $PATH
  shift
  IFS="$_save_IFS"

  for pathdir; do
    echo $pathdir/*${_libname}*-config
  done | tr ' ' "$as_nl" |
  $EGREP '/(lib)?'${_libname}'(-?@<:@0-9.@:>@+)?-config$' > conftest.candcfg
  while read c; do
    n=`AS_BASENAME([$c])`
    AC_MSG_CHECKING([for $1 using $n])
    if _cvers=`$c --version 2>&AS_MESSAGE_LOG_FD` && 
       _ccflg=`$c --cflags 2>&AS_MESSAGE_LOG_FD` &&
       _clibs=`$c --libs 2>&AS_MESSAGE_LOG_FD`
    then
      # Accept if $_cvers >= $_version.
      _goodvers=`MTN_VERSION_AT_LEAST([$_version], [$_cvers])`
      case $_goodvers in
        true) 
	  _notfound=false
          CPPFLAGS="$CPPFLAGS $_ccflg"
	  LIBS="$LIBS $_clibs"
	  AC_MSG_RESULT([yes])
          break ;;
        false)
          AC_MSG_RESULT([too old, $_cvers])
          continue ;;
        ERROR)
          AC_MSG_ERROR([internal error in MTN_VERSION_[]AT_LEAST]) ;;
      esac
    else
      AC_MSG_RESULT([no])
      continue
    fi
  done < conftest.candcfg

  if $_notfound; then
    AC_MSG_ERROR([A version of $_libname >= $_version is required])
  fi
fi
rm -f conftest.candcfg conftest.candpc
])

# The above concerns mean we cannot use a single PKG_CHECK_MODULES
# invocation to probe for everything, and that in turn means we have
# to do our removal of redundant -I and -L switches from the eventual
# Makefile variables that are set.

AC_DEFUN([MTN_REMOVE_REDUNDANT_LIB_SWITCHES],
[# Remove redundant -I and -L switches from CPPFLAGS and LIBS respectively.
# CPPFLAGS is easy, because we can assume that all duplicates are redundant.
new_CPPFLAGS=" "
for s in $CPPFLAGS; do
  case "$new_CPPFLAGS" in 
    *" $s "*) ;;
    *) new_CPPFLAGS="${new_CPPFLAGS}${s} "
  esac
done
CPPFLAGS="$new_CPPFLAGS"

# LIBS is a little more complicated.  Only duplicate -L switches are
# redundant; all other switches (notably -l and -Wl,thing) are not
# necessarily redundant if duplicated.  Order of non-L switches is
# significant, but, fortunately, it is safe to sort all -L switches to
# the beginning.
new_LIBS_L=" "
new_LIBS_rest=" "
for s in $LIBS; do
  case $s in 
    -L*)
      case "$new_LIBS_L" in
        *" $s "*) ;;
        *) new_LIBS_L="${new_LIBS_L}${s} " ;;
      esac ;;
    *)
      new_LIBS_rest="${new_LIBS_rest}${s} " ;;
  esac
done
LIBS="${new_LIBS_L}${new_LIBS_rest}"
AC_MSG_NOTICE([using CPPFLAGS: $CPPFLAGS])
AC_MSG_NOTICE([using LIBS: $LIBS])
])
