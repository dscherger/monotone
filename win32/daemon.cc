/*
**  daemon() -- daemonize current process, fallback for daemon(3)
**  Copyright (c) 2007 Ben Walton <bdwalton@gmail.com>
**
**	This is the windows version, which 'does nothing.'  See unix/daemon.cc
**	for Ralf's original daemon(3) workalike code.
**
**	Licensed for use under the GPL (v2 or greater).  See COPYING for
**	details.
*/


int mtn_daemon(int nochdir, int noclose)
{
#ifndef HAVE_DAEMON
	//if we don't have a real daemon(3) available, we'll no-op
	return 0;
#else
	//otherwise, use the system version
	return (daemon(nochdir, noclose));
#endif
}
