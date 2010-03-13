// Copyright (C) 2008 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "policies/branch.hh"

#include "app_state.hh"
#include "lazy_rng.hh"
#include "transforms.hh"

namespace {
  branch_uid generate_uid(app_state & app)
  {
    std::string when = date_t::now().as_iso_8601_extended();
    char buf[20];
    app.rng->get().randomize(reinterpret_cast<Botan::byte*>(buf), 20);
    return branch_uid(when + "--" + encode_hexenc(std::string(buf, 20),
                                                  origin::internal),
                      origin::internal);
  }
}

namespace policies {
  branch::branch() { }
  branch branch::create(app_state & app,
                        std::set<external_key_name> const & admins)
  {
    branch ret;
    ret.signers = admins;
    ret.uid = generate_uid(app);
    return ret;
  }
  branch_uid const & branch::get_uid() const
  {
    return uid;
  }
  std::set<external_key_name> const & branch::get_signers() const
  {
    return signers;
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
