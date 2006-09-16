# Determine whether we can build ASM, and 
AC_DEFUN([MTN_ASM],
  [AC_ARG_ENABLE(asm,
   AS_HELP_STRING([--disable-asm],
                  [do not use any platform-specific optimized SHA-1]),
   [], [enable_asm=yes])

   DO_ASM=no
   if test "x$enable_asm" != "xno"; then
       AM_PROG_AS
       AC_REQUIRE([AC_PROG_CC])
       if test "x$GCC" = "xyes"; then
          # we should be able to do ASM
          DO_ASM=yes
       fi
   fi
   AM_CONDITIONAL([ARM_ASM], test "x$DO_ASM" = xyes && expr $host : arm > /dev/null)
   AM_CONDITIONAL([PPC_ASM], test "x$DO_ASM" = xyes && expr $host : powerpc > /dev/null)
   AM_CONDITIONAL([X86_ASM], test "x$DO_ASM" = xyes && expr $host : .\*86 > /dev/null)
])
