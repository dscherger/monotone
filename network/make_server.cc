// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "network/make_server.hh"

#include "lexical_cast.hh"
#include "vocab.hh"

using std::vector;
using std::string;

using boost::lexical_cast;
using boost::shared_ptr;

shared_ptr<Netxx::StreamServer>
make_server(vector<utf8> const & addresses,
            Netxx::port_type default_port,
            Netxx::Timeout timeout,
            bool use_ipv6,
            Netxx::Address & addr)
{
  try
    {
      addr = Netxx::Address(use_ipv6);

      if (addresses.empty())
        addr.add_all_addresses(default_port);
      else
        {
          for (std::vector<utf8>::const_iterator it = addresses.begin();
               it != addresses.end(); ++it)
            {
              const utf8 & address = *it;
              if (!address().empty())
                {
                  size_t l_colon = address().find(':');
                  size_t r_colon = address().rfind(':');

                  if (l_colon == r_colon && l_colon == 0)
                    {
                      // can't be an IPv6 address as there is only one colon
                      // must be a : followed by a port
                      string port_str = address().substr(1);
                      addr.add_all_addresses(std::atoi(port_str.c_str()));
                    }
                  else
                    addr.add_address(address().c_str(), default_port);
                }
            }
        }
      shared_ptr<Netxx::StreamServer> ret(new Netxx::StreamServer(addr, timeout));

      char const * name;
      name = addr.get_name();
      P(F("beginning service on %s : %s")
        % (name != NULL ? name : _("<all interfaces>"))
        % lexical_cast<string>(addr.get_port()));

      return ret;
    }
  // If we use IPv6 and the initialisation of server fails, we want
  // to try again with IPv4.  The reason is that someone may have
  // downloaded a IPv6-enabled monotone on a system that doesn't
  // have IPv6, and which might fail therefore.
  catch(Netxx::NetworkException & e)
    {
      if (use_ipv6)
        return make_server(addresses, default_port, timeout, false, addr);
      else
        throw;
    }
  catch(Netxx::Exception & e)
    {
      if (use_ipv6)
        return make_server(addresses, default_port, timeout, false, addr);
      else
        throw;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
