# Determine whether we can build ASM, and 
AC_DEFUN([MTN_ASM],
  [AC_ARG_ENABLE(asm,
   AS_HELP_STRING([--disable-asm],
                  [do not use any platform-specific optimized SHA-1]),
   [], [enable_asm=yes])

   DO_ASM=no
   if test "x$enable_asm" != "no"; then
       AM_PROG_AS
       AC_REQUIRE([AC_PROG_CC])
       if test "x$GCC" != "yes"; then
          # we should be able to do ASM
          DO_ASM=yes
       fi
   fi
   AM_CONDITIONAL([ARM_ASM], test "x$DO_ASM" = yes && expr $host : arm)
   AM_CONDITIONAL([PPC_ASM], test "x$DO_ASM" = yes && expr $host : powerpc)
])