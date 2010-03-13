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
#include "policies/delegation.hh"

#include "basic_io.hh"
#include "transforms.hh"

using std::string;

namespace basic_io {
  namespace syms {
    static symbol revision_id("revision_id");
  }
}

namespace policies {
  delegation::delegation() {}
  delegation::delegation(revision_id const & r)
    : type(revision_type),
      revid(r)
  { }
  delegation::delegation(branch const & b)
    : type(branch_type),
      branch_desc(b)
  { }
  delegation delegation::create(app_state & app,
                                std::set<external_key_name> const & admins)
  {
    delegation ret;
    ret.type = branch_type;
    ret.branch_desc = branch::create(app, admins);
    return ret;
  }

  bool delegation::is_branch_type() const
  {
    return type == branch_type;
  }
  branch const & delegation::get_branch_spec() const
  {
    I(is_branch_type());
    return branch_desc;
  }

  void delegation::serialize(std::string & out) const
  {
    if (type == revision_type)
      {
        basic_io::printer p;
        basic_io::stanza s;
        s.push_binary_pair(basic_io::syms::revision_id, revid.inner());
        p.print_stanza(s);
        out = p.buf;
      }
    else
      {
        branch_desc.serialize(out);
      }
  }
  void delegation::deserialize(std::string const & in)
  {
    {
      basic_io::input_source s(in, "delegation");
      basic_io::tokenizer t(s);
      basic_io::parser p(t);
      if (p.symp(basic_io::syms::revision_id))
        {
          type = revision_type;
          p.sym();
          string rev;
          p.hex(rev);
          revid = decode_hexenc_as<revision_id>(rev, p.tok.in.made_from);
          return;
        }
    }
  type = branch_type;
  branch_desc.deserialize(in);
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
