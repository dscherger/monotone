// this file is partially cribbed from gfileutils.c in glib, which is
// copyright (c) 2000 Red Hat. It was released as LGPL version 2.0 or
// greater, so I have copied some of its text into this file and am
// relicensing my derivative work (this file):
//    copyright (C) 2004 graydon hoare, as LGPL version 2.0 or greater also.
//    revised, copyright (C) 2009 zack weinberg <zackw@panix.com>
//
// this is a portable implementation of mkstemp, which is not available on
// all systems and not always correctly implemented even when available.
//
// as with other mkstemp implementations, caller is expected to put the
// string "XXXXXX" somewhere in the template, which will be replaced with a
// random string.  it does not need to be at the end.  unlike the usual
// mkstemp, the return value is just a boolean success/failure condition,
// not a file descriptor; however, the file *has* been created.  only 100
// filenames are tried before we give up.
//
// we use only uppercase letters and digits in the random string, to avoid
// all problems with case (in)sensitivity and magic characters (filesystem
// or shell).  the letters I, L, O, X are not used; I, L, and O are often
// easily confused with the digits 1 or 0, and excluding X ensures that all
// six of the placeholder characters will be modified when the function
// returns, which makes it easier to test.  this also arranges for there to
// be 32 possible characters in the random string, which means there are
// exactly 2^30 possible random strings.
//
// security notes: because we use open() correctly, the worst thing an
// attacker can do to us is predict all 100 candidate filenames and cause
// the function to fail.  it is thus desirable for the attacker not to be
// able to deduce the state of the RNG from the output string.  however, the
// function is not used very often and the runtime cost of a fully seeded,
// cryptographically secure PRNG is very high.  we're not about to drop the
// mersenne twister in here, either, but we don't want something so shabby
// as a 32-bit LCG, which is easily vulnerable to attack as used here (for
// any output string there would be only four possible internal states).
//
// a reasonable choice is Pierre L'Ecuyer's "maximally equidistributed
// combined LFSR (Tausworthe) generator" as described at
// http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme2.ps -- this has
// 128 bits of state and is nice and short.
//
// we don't use the system rand() because we don't know if it has been
// seeded, we don't know if anyone else in the application is using it,
// and it can be extremely poor quality (RANDU, anyone?)

#include "base.hh"
#include "numeric_vocab.hh"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _MSC_VER  // bleh, is this really necessary?
 #undef open
 #define open(p, f, m) _open(p, f, m)
 #undef close
 #define close(f) _close(f)
 #undef O_RDWR
 #define O_RDWR _O_RDWR
 #undef O_CREAT
 #define O_CREAT _O_CREAT
 #undef O_EXCL
 #define O_EXCL _O_EXCL
 #undef O_BINARY
 #define O_BINARY _O_BINARY
#endif

#ifndef O_BINARY
 #define O_BINARY 0
#endif

using std::string;

// RNG implementation: this code is taken from figure 1 of the paper cited
// above, with revision for clarity.

static u32 z1, z2, z3, z4;

static u32
lfsr113()
{
  z1 = ((z1 & 0xfffffffeu) << 18) ^ (((z1 <<  6) ^ z1) >> 13);
  z2 = ((z2 & 0xfffffff8u) <<  2) ^ (((z2 <<  2) ^ z2) >> 27);
  z3 = ((z3 & 0xfffffff0u) <<  7) ^ (((z3 << 13) ^ z3) >> 21);
  z4 = ((z4 & 0xffffff80u) << 13) ^ (((z4 <<  3) ^ z4) >> 12);
  return z1 ^ z2 ^ z3 ^ z4;
}

// RNG implementation: i made this up based on the advice in the paper cited
// above: "before calling lfsr113 for the first time, the variables z1, z2,
// z3, and z4 must be initialized to any (random) integers larger than 1, 7,
// 15, and 127, respectively."  the current time is the traditional source
// of true indeterminacy for this purpose.  note that we perturb the values
// already there rather than replacing them -- this is so successive calls
// to monotone_mkstemp() within the system time granularity do not produce
// the same sequence.  the constants were chosen using a different PRNG.

static void
seed_lfsr113()
{
  u32 b = time(0);
  z1 += b ^ 2421089565u;
  z2 += b ^ 3453830001u;
  z3 += b ^ 1437919543u;
  z4 += b ^ 1406684125u;

  if (z1 <   1) z1 += 1;
  if (z2 <   7) z2 += 7;
  if (z3 <  15) z3 += 15;
  if (z4 < 127) z4 += 127;
}

bool
monotone_mkstemp(string & tmpl)
{
  static const char letters[] = "0123456789ABCDEFGHJKMNPQRSTUVWYZ";
  static const int NLETTERS = sizeof (letters) - 1;

  typedef string::size_type position;

  position len = tmpl.length();
  if (len < 6)
    return false;

  position xes = tmpl.find("XXXXXX");
  if (xes == string::npos)
    return false;

  char buf[len+1];
  memcpy(buf, tmpl.data(), len);
  buf[len] = 0;

  seed_lfsr113();

  // if we can't find a name in 100 tries there's probably a problem
  // requiring user intervention.
  for (int count = 0; count < 100; ++count)
    {
      u32 x = lfsr113();
      for (int i = 0; i < 6; i++)
        {
          buf[xes + i] = letters[x % NLETTERS];
          x /= NLETTERS;
        }

      int fd = open(buf, O_RDWR|O_CREAT|O_EXCL|O_BINARY, 0600);
      if (fd >= 0)
        {
          close(fd);
          tmpl.replace(xes, 6, buf+xes, 6);
          return true;
        }
      else if (errno != EEXIST)
        break;
    }
  return false;
}

#ifdef BUILD_UNIT_TESTS
#include "sanity.hh"
#include "unit_tests.hh"

UNIT_TEST(mkstemp, basic)
{
  // This test verifies that we can create 100x3 temporary files in the
  // same directory (using 3 different templates) and that the correct
  // part of the template pathname is modified in each case.

  char const * const cases[4] = {
    "a-XXXXXX", "XXXXXX-b", "c-XXXXXX.dat", 0
  };

  for (int i = 0; cases[i]; i++)
    for (int j = 0; j < 100; j++)
      {
        string r(cases[i]);
        string s(cases[i]);
        if (monotone_mkstemp(s))
          {
            UNIT_TEST_CHECK_MSG(r.length() == s.length(),
                                FL("same length: from %s got %s")
                                % r % s);
            bool no_scribble = true;
            for (string::size_type n = 0; n < r.length(); n++)
              {
                bool ok = r[n] == s[n];
                if (r[n] == 'X')
                  ok = !ok;
                if (!ok)
                  no_scribble = false;
              }
            UNIT_TEST_CHECK_MSG(no_scribble,
                                FL("modify correct segment: from %s got %s")
                                % r % s);
          }
        else
          {
            UNIT_TEST_CHECK_MSG(false,
                                FL("mkstemp failed with template %s "
                                   "(iteration %d, os error %s)")
                                % r % (j+1) % strerror(errno));
            break;
          }
      }
}
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
