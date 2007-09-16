/*
**  daemon() -- daemonize current process, fallback for daemon(3)
**  Copyright (c) 1999-2005 Ralf S. Engelschall <rse@engelschall.com>
**  Copyright (c) 2007 Ben Walton <bdwalton@gmail.com>
**
**	Imported to monotone with Ralf's permission as per:
**	http://www.nabble.com/forum/ViewPost.jtp?post=12613057&framed=y
**
**	This program is made available under the GNU GPL version 2.0 or
**	greater. See the accompanying file COPYING for details.
**	
**	This program is distributed WITHOUT ANY WARRANTY; without even the
**	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
**	PURPOSE.
*/

#include <unistd.h>
#ifndef HAVE_DAEMON
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#endif

int mtn_daemon(int nochdir, int noclose)
{
#ifdef HAVE_DAEMON
	return(daemon(nochdir, noclose));
#else
   int fd;
   int rc;

   /* ignore tty related signals */
#ifdef  SIGTTOU
   signal(SIGTTOU, SIG_IGN);
#endif
#ifdef  SIGTTIN
   signal(SIGTTIN, SIG_IGN);
#endif
#ifdef  SIGTSTP
   signal(SIGTSTP, SIG_IGN);
#endif

   /* fork so the parent can exit, this returns control to the command line
      or shell invoking your program. This step is required so that the new
      process is guaranteed not to be a process group leader (The next step,
      setsid, would fail if you're a process group leader). */
   rc = fork();
   switch (rc) {
       case -1: return -1;
       case  0: break;
       default: _exit(0); /* exit original process */
   }

   /* setsid to become a process group and session group leader. Since a
      controlling terminal is associated with a session, and this new session
      has not yet acquired a controlling terminal our process now has no
      controlling terminal, which is a Good Thing for daemons. */
#ifdef HAVE_SETSID
   if (setsid() == -1)
       return -1;
#else
   if (setpgid(0, getpid()) == -1)
       return -1;
#ifndef _PATH_TTY
#define _PATH_TTY "/dev/tty"
#endif
   if ((fd = open(_PATH_TTY, O_RDWR)) == -1)
       return -1;
   ioctl(fd, TIOCNOTTY, NULL);
   close(fd);
#endif

   /* fork again so the parent (the session group leader) can exit. This
      means that we, as a non-session group leader, can never regain a
      controlling terminal. */
   rc = fork();
   switch (rc) {
       case -1: return -1;
       case  0: break;
       default: _exit(0); /* exit original process */
   }

   /* change to root directory ensure that our process doesn't keep
      any directory in use. Failure to do this could make it so that
      an administrator couldn't unmount a filesystem, because it was
      our current directory. [Equivalently, we could change to any
      directory containing files important to the daemon's operation.] */
   if (!nochdir)
       chdir("/");

   /* give us complete control over the permissions of anything we
      write. We don't know what umask we may have inherited.
      [This step is optional] */
   umask(0);

   /* close fds 0, 1, and 2. This releases the standard in, out, and
      error we inherited from our parent process. We have no way of
      knowing where these fds might have been redirected to. */
   if (!noclose && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
       dup2(fd, STDIN_FILENO);
       dup2(fd, STDOUT_FILENO);
       dup2(fd, STDERR_FILENO);
       if (fd > 2)
           close(fd);
   }

   return 0;
#endif	//have_daemon
}
