// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "lua.hh"
#include "file_io.hh"

#include <errno.h>
#include <cstring>

using std::string;
using std::strerror;

LUAEXT(mkstemp, )
{
  char const *filename = luaL_checkstring (LS, 1);
  string dup(filename);

  if (!monotone_mkstemp(dup))
    return 0;

  lua_pushstring(LS, dup.c_str());
  return 1;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
