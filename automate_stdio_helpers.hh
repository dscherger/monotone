// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __AUTOMATE_STDIO_HELPERS__
#define __AUTOMATE_STDIO_HELPERS__

#include <string>
#include <utility>

#include <boost/function.hpp>

#include "commands.hh"

// these are in a class instead of stand-alone just because there's a
// friend declaration in cmd.hh, which shouldn't have to know about boost::function
class automate_stdio_helpers
{
public:
  enum force_ticker_t { force_stdio_ticker, no_force_stdio_ticker };
  static void
  automate_stdio_shared_setup(app_state & app,
                              std::vector<std::string> const & cmdline,
                              std::vector<std::pair<std::string,std::string> >
                              const * const params,
                              commands::command_id & id,
                              /* reference-to-pointer here is intentional */
                              commands::automate const * & acmd,
                              force_ticker_t ft = force_stdio_ticker);
  static std::pair<int, std::string>
  automate_stdio_shared_body(app_state & app,
                             std::vector<std::string> const & cmdline,
                             std::vector<std::pair<std::string,std::string> >
                             const & params,
                             std::ostream & os,
                             boost::function<void()> init_fn,
                             boost::function<void(commands::command_id const &)> pre_exec_fn);
};
#endif
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
