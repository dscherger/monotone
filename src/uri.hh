// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __URI_HH__
#define __URI_HH__

#include "sanity.hh"

struct uri_t
{
  std::string scheme;
  std::string user;
  std::string host;
  std::string port;
  std::string path;
  std::string query;
  std::string fragment;

  void clear()
  {
    scheme.clear();
    user.clear();
    host.clear();
    port.clear();
    path.clear();
    query.clear();
    fragment.clear();
  }

  std::string resource() const
  {
    std::string res;
    if (!scheme.empty())
      res += scheme + ":";
    std::string authority;
    if (!user.empty())
      authority += user + "@";
    if (!host.empty())
      authority += host;
    if (!port.empty())
      authority += ":" + port;
    if (!authority.empty())
      res += "//" + authority;
    if (!path.empty())
      res += path;
    return res;
  }
};

void
parse_uri(std::string const & in, uri_t & uri, origin::type made_from);

std::string
urldecode(std::string const & in, origin::type made_from);

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
