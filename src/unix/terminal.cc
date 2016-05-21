// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//                    Nathaniel Smith <njs@pobox.com>
//               2015 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"

#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

#include <iostream>
#include <sstream>

#include "../sanity.hh"
#include "../platform.hh"

// We need to determine the terminal smartness and width before we switch
// our stdout to write to some pager.  Therefore, we determine these during
// initialization and store their value in some globals.
//
// FIXME: these should ideally be part of some UI class or such.
bool terminal_initialized = false;
int _terminal_width = 0;
bool _have_smart_terminal = false;

pid_t pager_pid = 0;
int pager_exitstatus = 0;

static void
child_signal(int signo)
{
  pid_t pid;
  while ((pid = waitpid(-1, &pager_exitstatus, WNOHANG)) > 0)
    {
      if (pid == get_pager_pid())
        {
          pager_pid = 0;
          raise(SIGPIPE);
          break;
        }
    }
}

void initialize_terminal()
{
  // Remember the original terminal width and smart terminal values.
  I(!terminal_initialized);
  _terminal_width = terminal_width();
  _have_smart_terminal = have_smart_terminal();
  terminal_initialized = true;
}

int initialize_pager()
{
  // const char* pager = getenv("PAGER");

  // FIXME: the pager to use should be selectable by configuration
  char const * const pager_args[] = {"/usr/bin/less", "-FRX", NULL};

  int infds[2];
  if (pipe(infds) < 0)
    return -1;

  {
    std::ostringstream cmdline_ss;
    for (const char *const *i = pager_args; *i; ++i)
      {
        if (i != pager_args)
          cmdline_ss << ", ";
        cmdline_ss << "'" << *i << "'";
      }
    L(FL("spawning command: %s\n") % cmdline_ss.str());
  }

  std::cout.flush();
  pager_pid = fork();
  switch (pager_pid)
    {
    case -1: /* Error */
      return -1;

    case 0: /* Child */
      if (close(infds[1]) != 0)
        perror("pager: failed to close monotone's end of the pipe");

      if (dup2(infds[0], 0) == -1)
        {
          perror("pager: failed to redirect stdin");
          exit(1);
        }
      close(infds[0]);  // we don't care about errors, here

      execvp(pager_args[0], (char * const *) pager_args);
      raise(SIGKILL);

    default: /* Parent */
      if (close(infds[0]) != 0)
        perror("mtn: failed to close the pager's end of the pipe");

      // Install yet another signal handler for SIGCHLD to abort monotone
      // once the user quits the pager.
      {
        struct sigaction sa;
        sa.sa_flags = SA_RESETHAND;
        sa.sa_handler = &child_signal;
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, SIGCHLD);
        sigaction(SIGCHLD, &sa, 0);
      }

      // Let monotone write to the pipe of the pager.
      if (dup2(infds[1], 1) == -1)
        {
          perror("mtn: failed to redirect stdout");
          exit(1);
        }

      // Redirect stderr as well, if it's a tty.
      if (isatty(2))
        {
          if (dup2(infds[1], 2) == -1)
            perror("mtn: failed to redirect stderr");
        }

      close(infds[1]); // we don't care about errors, here

      // Ensure the pager terminates before us.
      atexit(finalize_pager);

      return 0;
    }

  return 0;
}

void finalize_pager()
{
  if (pager_pid == 0)
    return;

  pid_t tmp_pid = pager_pid;
  pager_pid = 0;

  L(FL("End of stream. Now waiting for the pager to finish."));

  std::cout.flush();
  std::cerr.flush();

  fflush(stdout);
  fflush(stderr);

  close(1);
  close(2);

  int status;
  pid_t pid;
  while ((pid = waitpid(tmp_pid, &status, 0)) < 0 && errno == EINTR)
    ;

  L(FL("waitpid returned %d, status %d, errno %d") % pid % status % errno);
}

pid_t get_pager_pid()
{
  return pager_pid;
}

bool have_smart_terminal()
{
  if (terminal_initialized)
    return _have_smart_terminal;
  
  std::string term;
  if (const char* term_cstr = getenv("TERM"))
    term = term_cstr;
  else
    term = "";

  // Emacs 22.2.1 on Windows sets TERM to "emacs", but on Debian Emacs sets
  // TERM to "dumb". The fix is to set TERM in your ~/.emacs, not to mess
  // with this logic.
  if (term == "" || term == "dumb" || !isatty(1))
    return false;
  else
    return true;
}

unsigned int terminal_width()
{
  if (terminal_initialized)
    return _terminal_width;

  struct winsize ws;
  int ret = ioctl(2, TIOCGWINSZ, &ws);
  if (ret < 0)
    {
      // FIXME: it would be nice to log something here
      // but we are called by the tick printing code, and trying to print
      // things while in the middle of printing a tick line is a great way to
      // break things.
      return 0;
    }
  return ws.ws_col;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
