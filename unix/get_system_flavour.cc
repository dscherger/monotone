// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/utsname.h>
#include "sanity.hh"

void get_system_flavour(std::string & ident)
{
  struct utsname n;
  I(uname(&n) == 0);
  ident = (F("%s %s %s %s") 
	   % n.sysname
	   % n.release
	   % n.version
	   % n.machine).str();
}
