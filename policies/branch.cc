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

#include "basic_io.hh"
#include "dates.hh"
#include "lazy_rng.hh"
#include "transforms.hh"

using std::string;

namespace basic_io {
  namespace syms {
    static symbol branch_uid("branch_uid");
    static symbol key("key");
  }
}

namespace {
  branch_uid generate_uid()
  {
    std::string when = date_t::now().as_iso_8601_extended();
    char buf[20];
    lazy_rng::get().randomize(reinterpret_cast<Botan::byte*>(buf), 20);
    return branch_uid(when + "--" + encode_hexenc(std::string(buf, 20),
                                                  origin::internal),
                      origin::internal);
  }
}

namespace policies {
  branch::branch() { }
  branch branch::create(std::set<external_key_name> const & admins)
  {
    branch ret;
    ret.signers = admins;
    ret.uid = generate_uid();
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

  void branch::serialize(std::string & out) const
  {
    basic_io::printer p;
    basic_io::stanza s;

    s.push_str_pair(basic_io::syms::branch_uid, uid());
    for (std::set<external_key_name>::const_iterator k = signers.begin();
         k != signers.end(); ++k)
      {
        s.push_str_pair(basic_io::syms::key, (*k)());
      }

    p.print_stanza(s);
    out = p.buf;
  }
  void branch::deserialize(std::string const & in)
  {
    basic_io::input_source s(in, "branch");
    basic_io::tokenizer t(s);
    basic_io::parser p(t);
    while (p.symp())
      {
        if (p.symp(basic_io::syms::branch_uid))
          {
            p.sym();
            string u;
            p.str(u);
            uid = branch_uid(u, origin::internal);
          }
        else if (p.symp(basic_io::syms::key))
          {
            p.sym();
            string k;
            p.str(k);
            signers.insert(external_key_name(k, origin::internal));
          }
        else
          break;
      }
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
