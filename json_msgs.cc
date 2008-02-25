// Copyright (C) 2008 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include "json_io.hh"
#include "json_msgs.hh"
#include "cset.hh"

#include <map>
#include <set>
#include <string>

using std::map;
using std::set;
using std::string;
using std::pair;

using json_io::json_value_t;
using json_io::json_array_t;
using json_io::json_object_t;


namespace
{
  namespace syms
  {
    // cset symbols
    symbol const delete_node("delete");
    symbol const rename("rename");
    symbol const content("content");
    symbol const add_file("add_file");
    symbol const add_dir("add_dir");
    symbol const patch("patch");
    symbol const from("from");
    symbol const to("to");
    symbol const clear("clear");
    symbol const set("set");
    symbol const attr("attr");
    symbol const value("value");

    // revision symbols
    symbol const old_revision("old_revision");
    symbol const new_manifest("new_manifest");
    symbol const edges("edges");

    // file delta / data symbols
    symbol const id("id");
    symbol const src_id("src_id");
    symbol const dst_id("dst_id");
    symbol const delta("delta");
    symbol const data("data");

    // command symbols
    symbol const type("type");
    symbol const vers("vers");
    symbol const revs("revs");
    symbol const error("error");

    symbol const inquire("inquire");
    symbol const confirm("confirm");

    symbol const get_descendants("get_descendants");
    symbol const descendants("descendants");

    symbol const get_rev("get_rev");
    symbol const get_full_rev("get_full_rev");
    symbol const get_file_data("get_file_data");
    symbol const get_file_delta("get_file_delta");

    symbol const rev("rev");
    symbol const full_rev("full_rev");
    symbol const file_data("file_data");
    symbol const file_delta("file_delta");
  }
}

/////////////////////////////////////////////////////////////////////
// message type 'error' 
/////////////////////////////////////////////////////////////////////

json_value_t 
encode_msg_error(string const & note)
{
  json_io::builder b;
  b[syms::error].str(note);
  return b.v;
}

bool 
decode_msg_error(json_value_t val, 
                 std::string & note)
{
  json_io::query q(val);
  note.clear();
  return q[syms::error].get(note);
}


/////////////////////////////////////////////////////////////////////
// message type 'inquire' 
/////////////////////////////////////////////////////////////////////

json_value_t 
encode_msg_inquire(set<revision_id> const & revs)
{
  json_io::builder b;
  b[syms::type].str(syms::inquire());
  b[syms::vers].str("1");
  json_io::builder r = b[syms::revs].arr();
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool 
decode_msg_inquire(json_value_t val, 
                   set<revision_id> & revs)
{
  string type, vers;
  json_io::query q(val);  
  if (q[syms::type].get(type) && 
      type == syms::inquire() && 
      q[syms::vers].get(vers) && 
      vers == "1")
    {      
      size_t nargs = 0;
      if (q[syms::revs].len(nargs)) 
        {
          std::string s;
          for (size_t i = 0; i < nargs; ++i)
            if (q[syms::revs][i].get(s)) 
              revs.insert(revision_id(s));
          return true;
        }
    }
  return false;
}


/////////////////////////////////////////////////////////////////////
// message type 'confirm' 
/////////////////////////////////////////////////////////////////////

json_value_t 
encode_msg_confirm(set<revision_id> const & revs)
{
  json_io::builder b;
  b[syms::type].str(syms::confirm());
  b[syms::vers].str("1");
  json_io::builder r = b[syms::revs].arr();
  for (set<revision_id>::const_iterator i = revs.begin();
       i != revs.end(); ++i)
    {
      r.add_str(i->inner()());
    }
  return b.v;
}

bool 
decode_msg_confirm(json_value_t val, 
                   set<revision_id> & revs)
{
  string type, vers;
  json_io::query q(val);  
  if (q[syms::type].get(type) &&
      type == syms::confirm() &&
      q[syms::vers].get(vers) &&
      vers == "1")
    {
      size_t nrevs = 0;
      string tmp;
      json_io::query r = q[syms::revs];
      if (r.len(nrevs))
        for (size_t i = 0; i < nrevs; ++i) 
          if (r[i].get(tmp))
            revs.insert(revision_id(tmp));
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_descendants' 
/////////////////////////////////////////////////////////////////////

json_value_t 
encode_msg_get_descendants(set<revision_id> const & revs);
bool 
decode_msg_get_descendants(json_value_t val, 
                           set<revision_id> & revs);


/////////////////////////////////////////////////////////////////////
// message type 'descendants' 
/////////////////////////////////////////////////////////////////////

json_value_t 
encode_msg_descendants(rev_ancestry_map const & parent_to_child_map);
bool 
decode_msg_descendants(json_value_t val, 
                       rev_ancestry_map & parent_to_child_map);


/////////////////////////////////////////////////////////////////////
// message type 'rev' 
/////////////////////////////////////////////////////////////////////


static void
cset_to_json(json_io::builder b, cset const & cs)
{
    for (set<file_path>::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      b.add_obj()[syms::delete_node].str(i->as_internal());
    }

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::rename].str(i->first.as_internal());
      tmp[syms::to].str(i->second.as_internal());
    }

  for (set<file_path>::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      b.add_obj()[syms::add_dir].str(i->as_internal());
    }

  for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::add_file].str(i->first.as_internal());
      tmp[syms::content].str(i->second.inner()());
    }

  for (map<file_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::patch].str(i->first.as_internal());
      tmp[syms::from].str(i->second.first.inner()());
      tmp[syms::to].str(i->second.second.inner()());
    }

  for (set<pair<file_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::clear].str(i->first.as_internal());
      tmp[syms::attr].str(i->second());
    }

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::set].str(i->first.first.as_internal());
      tmp[syms::attr].str(i->first.second());
      tmp[syms::value].str(i->second());
    }
}

json_value_t
encode_msg_rev(revision_t const & rev)
{
  json_io::builder b;  
  b[syms::type].str(syms::rev());
  b[syms::vers].str("1");
  b[syms::new_manifest].str(rev.new_manifest.inner()());
  json_io::builder edges = b[syms::edges].arr();
  for (edge_map::const_iterator e = rev.edges.begin(); 
       e != rev.edges.end(); ++e)
    {
      json_io::builder edge = edges.add_obj();
      edge[syms::old_revision].str(edge_old_revision(e).inner()());
      cset_to_json(edge, edge_changes(e));
    }
  return b.v;
}




json_value_t 
encode_msg_full_rev(revision_id const & rid,
                    revision_t const & rev,
                    set<file_delta_record> const & deltas,
                    set<file_data_record> const & datas)
{
  json_io::builder b;  
  b[syms::type].str(syms::full_rev());
  b[syms::vers].str("1");
  json_io::builder rev_builder = b[syms::rev];  
  return b.v;
}

bool 
decode_msg_full_rev(json_value_t val, 
                    revision_id & rid,
                    revision_t & rev,
                    set<file_delta_record> & deltas,
                    set<file_data_record> & datas)
{
  json_io::builder b;
  b[syms::type].str(syms::full_rev());
  b[syms::vers].str("1");
  b[syms::rev] = encode_msg_rev(rev);
  return b.v;
}




// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
