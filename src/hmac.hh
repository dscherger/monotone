// Copyright (C) 2005 Matt Johnston <matt@ucc.asn.au>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __HMAC_HH__
#define __HMAC_HH__

#include <botan/botan.h>

#include "vocab.hh"
#include "constants.hh"
#include "string_queue.hh"

struct chained_hmac
{
public:
  chained_hmac(netsync_session_key const & session_key, bool active);
  void set_key(netsync_session_key const & session_key);
  std::string process(std::string const & str, size_t pos = 0,
                      size_t n = std::string::npos);
  std::string process(string_queue const & str, size_t pos = 0,
                      size_t n = std::string::npos);

  size_t const hmac_length;
  bool is_active() { return active; }

private:
  bool active;
  Botan::SymmetricKey key;
  Botan::Pipe engine;
  std::string chain_val;
};

#endif // __HMAC_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
