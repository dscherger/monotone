// Copyright (C) 2009 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../base.hh"
#include "wrapped_session.hh"

#include "session.hh"

using std::string;

wrapped_session::~wrapped_session() { }

wrapped_session::wrapped_session(session * owner) :
  owner(owner)
{ }

void wrapped_session::set_owner(session * owner)
{
  I(!this->owner);
  this->owner = owner;
}

void wrapped_session::write_netcmd(netcmd const & cmd) const
{
  owner->write_netcmd(cmd);
}

u8 wrapped_session::get_version() const
{
  return owner->get_version();
}

void wrapped_session::error(int errcode, string const & message)
{
  owner->error(errcode, message);
}

protocol_voice wrapped_session::get_voice() const
{
  return owner->get_voice();
}

bool wrapped_session::encountered_error() const
{
  return owner->encountered_error;
}

int wrapped_session::get_error_code() const
{
  return owner->get_error_code();
}

bool wrapped_session::get_authenticated() const
{
  return owner->get_authenticated();
}

string wrapped_session::get_peer() const
{
  return owner->get_peer();
}

bool wrapped_session::output_overfull() const
{
  return owner->output_overfull();
}

bool wrapped_session::shutdown_confirmed() const
{
  return owner->protocol_state == session_base::confirmed_state;
}

void wrapped_session::request_netsync(protocol_role role,
                                      globish const & include,
                                      globish const & exclude)
{
  owner->request_netsync(role, include, exclude);
}

void wrapped_session::request_automate()
{
  owner->request_automate();
}

void wrapped_session::on_begin(size_t ident, key_identity_info const & remote_key)
{ }

void wrapped_session::on_end(size_t ident)
{ }

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
