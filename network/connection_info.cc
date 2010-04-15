// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "constants.hh"
#include "network/connection_info.hh"

netsync_connection_info::Client::Client() :
  use_argv(false),
  connection_type(netsync_connection),
  input_stream(0),
  output_stream(0)
{ }

std::istream & netsync_connection_info::Client::get_input_stream() const
{
  I(input_stream);
  return *input_stream;
}

automate_ostream & netsync_connection_info::Client::get_output_stream() const
{
  I(output_stream);
  return *output_stream;
}

void netsync_connection_info::Client::set_input_stream(std::istream & is)
{
  input_stream = &is;
}

void netsync_connection_info::Client::set_output_stream(automate_ostream & os)
{
  output_stream = &os;
}

std::size_t netsync_connection_info::Client::get_port() const
{
  if (uri.port.empty())
    return constants::netsync_default_port;
  return atoi(uri.port.c_str());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
