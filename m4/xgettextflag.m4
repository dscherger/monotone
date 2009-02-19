# Check whether xgettext supports the --flag option.  If it does not,
# we forcibly override XGETTEXT_OPTIONS in po/Makefile.in to prevent
# its being used.  (See po/Makevars for further explanation.)  We also
# unconditionally prevent auto-regeneration of the .po files when the
# sources change, because the translators do not want auto-regenerated
# .po files checked in (it makes extra work for them).
#
# Yes, this is a big hack, but due to the way po/Makefile is
# generated, there is no other way to do this in a dynamic fashion.
#
# Note lame hardwiring of AC_CONFIG_COMMANDS first argument to not
# collide with the gettext macros.

AC_DEFUN([AC_PROG_XGETTEXT_FLAG_OPTION],
[AC_CACHE_CHECK([whether xgettext supports --flag],
		ac_cv_prog_xgettext_flag_option,
  [echo 'int main(void) { return 0; }' >> conftest.c
   if ${XGETTEXT} --flag printf:1:c-format -o conftest.po conftest.c \
      >/dev/null 2>&1
   then ac_cv_prog_xgettext_flag_option=yes
   else ac_cv_prog_xgettext_flag_option=no
   fi])
 AC_CONFIG_COMMANDS([[default-2]], [[
   for ac_file in $CONFIG_FILES; do
     # Support "outfile[:infile[:infile...]]"
     case "$ac_file" in
       *:*) ac_file=`echo "$ac_file"|sed 's%:.*%%'` ;;
     esac
     # PO directories have a Makefile.in generated from Makefile.in.in.
     case "$ac_file" in */Makefile.in)
     target="`echo x$ac_file | sed -e 's/.in$//' -e 's/^x//'`"
     test -n "$as_me" && echo "$as_me: fixing $target" || echo "fixing $target"
     mtn_tmp="${target}.tmp"
     sed -e 's/^MSGMERGE =.*$/MSGMERGE = false/' \
         -e 's/^MSGMERGE_UPDATE =.*$/MSGMERGE_UPDATE = true/' \
         "$target" > "$mtn_tmp"
     if test $xgettext_flag_option = no; then
       echo 'XGETTEXT_OPTIONS = $(XGETTEXT_OPTIONS_NO_FLAG)' >>"$mtn_tmp"
     fi
     mv -f "$mtn_tmp" "$target" ;;
     esac
   done
  ]],
  [[xgettext_flag_option=$ac_cv_prog_xgettext_flag_option]])
])
