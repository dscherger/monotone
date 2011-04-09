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
#include "globish.hh"
#include "sanity.hh"

using std::string;

LUAEXT(match, globish)
{
  const char * re = luaL_checkstring(LS, -2);
  const char * str = luaL_checkstring(LS, -1);

  bool result = false;
  try
    {
      globish g(re, origin::user);
      result = g.matches(str);
    }
  catch (recoverable_failure & e)
    {
      return luaL_error(LS, e.what());
    }
  catch (...)
    {
      return luaL_error(LS, "Unknown error.");
    }
  lua_pushboolean(LS, result);
  return 1;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
