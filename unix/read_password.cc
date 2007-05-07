// Copyright (C) 2004 Nico Schottelius <nico-linux-monotone@schottelius.org>
// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#include "sanity.hh"
#include "platform.hh"

// read_password prompts the user for a pass phrase and reads the phrase,
// taking pains to use the terminal even if stdin has been redirected (as
// long as there _is_ a terminal).  The user's typing is not echoed, but we
// print a space for each character typed, and provide standard tty line
// editing and interrupt/suspend (by hand - the terminal is in raw mode).
//
// FIXME: Line editing does not take multibyte characters into account.
// (Can you _get_ multibyte characters with the tty in raw mode?  I suspect
// you can...)

namespace
{
  class raw_tty
  {
    int ttyrd;
    int ttywr;
    bool do_close, do_reset;
    struct termios original;
    struct termios raw;

  public:
    raw_tty() : ttyrd(0), ttywr(1), do_close(false), do_reset(false)
    {
      memset(&original, 0, sizeof original);
      memset(&raw, 0, sizeof raw);

      // Try to open /dev/tty.  If that succeeds, set ttyrd and ttywr
      // appropriately and remember that we need to close it.
      int fd = open("/dev/tty", O_RDWR);
      if (fd >= 0)
        {
          ttyrd = ttywr = fd;
          do_close = true;
        }

      // Try to disable echoing and put the terminal in raw mode.  If that
      // succeeds, remember that we need to undo that later.  The lack of
      // error checking is deliberate; we go ahead and read a password
      // (presumably from stdin) even if we don't have a terminal to read it
      // from.
      if (tcgetattr(ttyrd, &original) == 0)
        {
          raw = original;
          cfmakeraw(&raw);
          tcsetattr(ttyrd, TCSAFLUSH, &raw);
          do_reset = true;
        }
    }

    // Reset the terminal to normal mode on destruction.
    // Here and in signal(), it is important to write the \n *after*
    // canonical mode is reactivated, because canonical mode is what
    // turns a single \n into the CR LF sequence that most terminals
    // expect.
    ~raw_tty()
    {
      if (do_reset)
        tcsetattr(ttyrd, TCSAFLUSH, &original);
      write('\n');
      if (do_close)
        close(ttyrd);
    }

    // Reset the terminal to normal mode and raise a signal.  If control
    // returns from the raise(), put the terminal back in raw mode.
    void signal(int signo)
    {
      if (do_reset)
        tcsetattr(ttyrd, TCSAFLUSH, &original);
      write('\n');
      raise(signo);
      if (do_reset)
        tcsetattr(ttyrd, TCSAFLUSH, &raw);
    }

    // Read and return one raw character.  
    char read()
    {
      char c;
      int n;
      // EINTR should be impossible, but let's be careful.
      do
        n = ::read(ttyrd, &c, 1);
      while (n == -1 && errno == EINTR);
      
      E(n >= 0, F("error reading password: %s") % strerror(errno));

      if (n == 0) // EOF indicator; pretend they hit return
        return '\n';
      else
        return c;
    }

    // Write a string to the terminal.  Guarantees to write the
    // entire string or report an error.  If the thing we are
    // reading from is not actually a terminal, all output is
    // suppressed.
    void write(char const * p, size_t n)
    {
      if (!do_reset)
        return;
      do
        {
          ssize_t written = ::write(ttywr, p, n);
          E(written >= 0,
            F("error prompting for password: %s") % strerror(errno));
          E(written > 0,
            F("zero-length write while prompting for password"));

          I((size_t)written <= n);
          n -= written;
          p += written;
        }
      while (n);
    }

    // Write a single character to the terminal.
    void write(char c)
    {
      write(&c, 1);
    }

    // Given a character, return the Vxxx code it corresponds to,
    // or NCCS if it is not a special character.  Some codes are
    // collapsed into one another.
    int dispatch(char c, bool lnext)
    {
      // You cannot have a newline or EOF character in your password,
      // even with LNEXT.
      if (c == original.c_cc[VEOF] || c == original.c_cc[VEOL]
#ifdef VEOL2
          || c == original.c_cc[VEOL2]
#endif
          || c == '\n' || c == '\r' || c == '\0')
        return VEOL;

      if (lnext)
        return NCCS;

      if (c == original.c_cc[VLNEXT])
        return VLNEXT;
      if (c == original.c_cc[VINTR])
        return VINTR;
      if (c == original.c_cc[VQUIT])
        return VQUIT;
      if (c == original.c_cc[VSUSP])
        return VSUSP;
#ifdef VDSUSP
      if (c == original.c_cc[VDSUSP])
        return VSUSP;
#endif
      if (c == original.c_cc[VERASE])
        return VERASE;
      if (c == original.c_cc[VWERASE])
        return VWERASE;
      if (c == original.c_cc[VKILL])
        return VKILL;

      // Other documented special characters: VREPRINT, VDISCARD,
      // VSTATUS, VSWTCH, VSTART, VSTOP.  None of them make sense
      // in this context so we treat them as literal.
      return NCCS;
    }

    // Tell caller which WERASE algorithm to implement (see below).
    bool word_boundary_is_whitespace()
    {
#ifdef ALTWERASE
      return !(original.c_lflag & ALTWERASE);
#else
      return true;
#endif
    }
  };
}

void
read_password(char const * prompt, char * buf, size_t bufsz)
{
  size_t promptbufbase = strlen(prompt); 
  size_t promptbufsz = promptbufbase + bufsz + 1; // for a NUL
  char promptbuf[promptbufsz];
  size_t i = 0;
  bool lnext = false;

  memset(buf, 0, bufsz);

  strcpy(promptbuf, prompt);
  memset(promptbuf + promptbufbase, ' ', bufsz);
  promptbuf[promptbufbase+bufsz] = '\0';

  // open the terminal and put it in raw mode
  raw_tty tio;
  tio.write(promptbuf, promptbufbase);

  for (;;)
    {
      char c = tio.read();
      switch (tio.dispatch(c, lnext))
        {
        case NCCS: // normal character
          if (i == bufsz)
            {
              tio.write('\a');
              break;
            }
          buf[i] = c;
          i++;
          tio.write(' ');
          lnext = false;
          break;

        case VEOL: // end of line
          if (!lnext)
            {
              buf[i] = '\0';
              return;
            }

          tio.write('\a');
          lnext = false;
          break;

        case VLNEXT: // treat next character as a normal character (^V)
          lnext = true;
          break;

        case VINTR: // interrupt process (^C)
          tio.signal(SIGINT);
          tio.write(promptbuf, promptbufbase + i);
          break;

        case VQUIT: // kill process and dump core (^\)
          tio.signal(SIGQUIT);
          tio.write(promptbuf, promptbufbase + i);
          break;

        case VSUSP: // suspend process (^Z)
          tio.signal(SIGTSTP);
          tio.write(promptbuf, promptbufbase + i);
          break;

        case VERASE: // delete previous character (backspace)
          if (i == 0)
            tio.write('\a');
          else
            {
              i--;
              tio.write('\b');
            }
          break;


        case VWERASE: // erase previous word (^W)
          if (i == 0)
            tio.write('\a');
          else
            {
              size_t last = i;
              
              // According to FreeBSD 7's termios(4):
              //
              // If the ALTWERASE flag is not set, first any preceding
              // whitespace is erased, and then the maximal sequence of
              // non-whitespace characters.  If ALTWERASE is set, first any
              // preceding whitespace is erased, and then the maximal
              // sequence of alphabetic/underscores or non alphabetic/
              // underscores.  As a special case in this second algorithm,
              // the first previous non-whitespace character is skipped in
              // determining whether the preceding word is a sequence of
              // alphabetic/underscores.  This sounds confusing but turns
              // out to be quite practical.

              while (i > 0 && isspace(buf[i-1]))
                i--;

              if (i > 0)
                i--;

              if (tio.word_boundary_is_whitespace())
                {
                  while (i > 0 && !isspace(buf[i-1]))
                    i--;
                }
              else if (i > 0 && (isalnum(buf[i-1]) || buf[i-1] == '_'))
                {
                  while (i > 0 && (isalnum(buf[i-1]) || buf[i-1] == '_'))
                    i--;
                }
              else
                {
                  while (i > 0
                         && !isalnum(buf[i-1]) && !isspace(buf[i-1])
                         && buf[i-1] != '_')
                    i--;
                }

              for (; last > i; last--)
                tio.write('\b');
            }
          break;

        case VKILL: // erase line (^U)
          if (i == 0)
            tio.write('\a');
          else
            {
              while (i > 0)
                {
                  i--;
                  tio.write('\b');
                }
            }
          break;

        default:
          I(false);
        }
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
