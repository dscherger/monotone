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

#include <boost/shared_ptr.hpp>

using std::make_pair;
using std::map;
using std::set;
using std::string;
using std::pair;
using std::vector;

using json_io::json_value_t;
using json_io::json_array_t;
using json_io::json_object_t;

using boost::shared_ptr;

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

    symbol const changes("changes");

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

    // request/response pairs
    symbol const inquire_request("inquire_request");
    symbol const inquire_response("inquire_response");

    symbol const descendants_request("descendants_request");
    symbol const descendants_response("descendants_response");

    symbol const put_rev_request("put_rev_request");
    symbol const put_rev_response("put_rev_response");
    symbol const get_rev_request("get_rev_request");
    symbol const get_rev_response("get_rev_request");

    symbol const status("status");

    symbol const get_file_data("get_file_data");
    symbol const get_file_delta("get_file_delta");

    symbol const rev("rev");
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
// message type 'inquire_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_inquire_request(set<revision_id> const & revs)
{
  json_io::builder b;
  b[syms::type].str(syms::inquire_request());
  b[syms::vers].str("1");
  json_io::builder r = b[syms::revs].arr();
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool
decode_msg_inquire_request(json_value_t val,
                           set<revision_id> & revs)
{
  string type, vers;
  json_io::query q(val);
  if (q[syms::type].get(type) && type == syms::inquire_request() &&
      q[syms::vers].get(vers) && vers == "1")
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
// message type 'inquire_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_inquire_response(set<revision_id> const & revs)
{
  json_io::builder b;
  b[syms::type].str(syms::inquire_response());
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
decode_msg_inquire_response(json_value_t val,
                            set<revision_id> & revs)
{
  string type, vers;
  json_io::query q(val);
  if (q[syms::type].get(type) && type == syms::inquire_response() &&
      q[syms::vers].get(vers) && vers == "1")
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
// message type 'descendants_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_descendants_request(set<revision_id> const & revs)
{
  json_io::builder b;
  b[syms::type].str(syms::descendants_request());
  b[syms::vers].str("1");
  json_io::builder r = b[syms::revs].arr();
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool
decode_msg_descendants_request(json_value_t val,
                               set<revision_id> & revs)
{
  string type, vers;
  json_io::query q(val);
  if (q[syms::type].get(type) && type == syms::descendants_request() &&
      q[syms::vers].get(vers) && vers == "1")
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
// message type 'descendants_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_descendants_response(vector<revision_id> const & revs)
{
  json_io::builder b;
  b[syms::type].str(syms::descendants_response());
  b[syms::vers].str("1");
  json_io::builder r = b[syms::revs].arr();
  for (vector<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool
decode_msg_descendants_response(json_value_t val,
                                vector<revision_id> & revs)
{
  string type, vers;
  json_io::query q(val);
  if (q[syms::type].get(type) && type == syms::descendants_response() &&
      q[syms::vers].get(vers) && vers == "1")
    {
      size_t nargs = 0;
      if (q[syms::revs].len(nargs))
        {
          std::string s;
          for (size_t i = 0; i < nargs; ++i)
            if (q[syms::revs][i].get(s))
              revs.push_back(revision_id(s));
          return true;
        }
    }
  return false;
}


/////////////////////////////////////////////////////////////////////
// message type 'rev'
/////////////////////////////////////////////////////////////////////


static void
encode_cset(json_io::builder b, cset const & cs)
{
  for (set<file_path>::const_iterator
         i = cs.nodes_deleted.begin(); i != cs.nodes_deleted.end(); ++i)
    {
      b.add_obj()[syms::delete_node].str(i->as_internal());
    }

  for (map<file_path, file_path>::const_iterator
         i = cs.nodes_renamed.begin(); i != cs.nodes_renamed.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::rename].str(i->first.as_internal());
      tmp[syms::to].str(i->second.as_internal());
    }

  for (set<file_path>::const_iterator
         i = cs.dirs_added.begin(); i != cs.dirs_added.end(); ++i)
    {
      b.add_obj()[syms::add_dir].str(i->as_internal());
    }

  for (map<file_path, file_id>::const_iterator
         i = cs.files_added.begin(); i != cs.files_added.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::add_file].str(i->first.as_internal());
      tmp[syms::content].str(i->second.inner()());
    }

  for (map<file_path, pair<file_id, file_id> >::const_iterator
         i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::patch].str(i->first.as_internal());
      tmp[syms::from].str(i->second.first.inner()());
      tmp[syms::to].str(i->second.second.inner()());
    }

  for (set<pair<file_path, attr_key> >::const_iterator
         i = cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::clear].str(i->first.as_internal());
      tmp[syms::attr].str(i->second());
    }

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator
         i = cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
    {
      json_io::builder tmp = b.add_obj();
      tmp[syms::set].str(i->first.first.as_internal());
      tmp[syms::attr].str(i->first.second());
      tmp[syms::value].str(i->second());
    }
}

static void
decode_cset(json_io::query q, cset & cs)
{
  size_t nargs = 0;
  I(q.len(nargs));
  for (size_t i = 0; i < nargs; ++i)
    {
      json_io::query change = q[i];
      string path;
      if (change[syms::delete_node].get(path))
        {
          cs.nodes_deleted.insert(file_path_external(utf8(path)));
        }
      else if (change[syms::rename].get(path))
        {
          string to;
          I(change[syms::to].get(to));
          cs.nodes_renamed.insert(make_pair(file_path_external(utf8(path)),
                                            file_path_external(utf8(to))));
        }
      else if (change[syms::add_dir].get(path))
        {
          cs.dirs_added.insert(file_path_external(utf8(path)));
        }
      else if (change[syms::add_file].get(path))
        {
          string content;
          I(change[syms::content].get(content));
          cs.files_added.insert(make_pair(file_path_external(utf8(path)),
                                          file_id(content)));
        }
      else if (change[syms::patch].get(path))
        {
          string from, to;
          I(change[syms::from].get(from));
          I(change[syms::to].get(to));
          cs.deltas_applied.insert(make_pair(file_path_external(utf8(path)),
                                             make_pair(file_id(from),
                                                       file_id(to))));
        }
      else if (change[syms::clear].get(path))
        {
          string key;
          I(change[syms::attr].get(key));
          cs.attrs_cleared.insert(make_pair(file_path_external(utf8(path)),
                                            attr_key(key)));
        }
      else if (change[syms::set].get(path))
        {
          string key, val;
          I(change[syms::attr].get(key));
          I(change[syms::value].get(val));
          cs.attrs_set.insert(make_pair(make_pair(file_path_external(utf8(path)),
                                                  attr_key(key)),
                                        attr_value(val)));
        }
      else
        I(false);

    }
}

static void
encode_rev(json_io::builder b, revision_t const & rev)
{
  b[syms::vers].str("1");
  b[syms::new_manifest].str(rev.new_manifest.inner()());
  json_io::builder edges = b[syms::edges].arr();
  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      json_io::builder edge = edges.add_obj();
      edge[syms::old_revision].str(edge_old_revision(e).inner()());
      json_io::builder changes = edge[syms::changes].arr();
      encode_cset(changes, edge_changes(e));
    }
}

static void
decode_rev(json_io::query q, revision_t & rev)
{
  string new_manifest, vers;
  I(q[syms::new_manifest].get(new_manifest));
  I(q[syms::vers].get(vers));
  I(vers == "1");

  rev.new_manifest = manifest_id(new_manifest);
  size_t nargs = 0;
  json_io::query edges = q[syms::edges];
  I(edges.len(nargs));

  for (size_t i = 0; i < nargs; ++i)
    {
      json_io::query edge = edges[i];
      string old_revision;
      I(edge[syms::old_revision].get(old_revision));
      json_io::query changes = edge[syms::changes];
      shared_ptr<cset> cs(new cset());
      decode_cset(changes, *cs);
      rev.edges.insert(make_pair(revision_id(old_revision), cs));
    }
}

json_value_t
encode_msg_put_rev_request(revision_id const & rid, revision_t const & rev)
{
  json_io::builder b;
  b[syms::type].str(syms::put_rev_request());
  b[syms::vers].str("1");
  b[syms::id].str(rid.inner()());
  json_io::builder rb = b[syms::rev].obj();
  encode_rev(rb, rev);
  return b.v;
}

bool
decode_msg_put_rev_request(json_value_t val, revision_id & rid, revision_t & rev)
{
  string type, vers, id;
  json_io::query q(val);
  if (q[syms::type].get(type) && type == syms::put_rev_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id))
    {
      rid = revision_id(id);
      json_io::query rq = q[syms::rev];
      decode_rev(rq, rev);
      return true;
    }
  return false;
}

json_value_t
encode_msg_put_rev_response()
{
  json_io::builder b;
  b[syms::type].str(syms::put_rev_response());
  b[syms::vers].str("1");
  b[syms::status].str("received");
  return b.v;
}

bool
decode_msg_put_rev_response(json_value_t val)
{
  string type, vers, status;
  json_io::query q(val);
  if (q[syms::type].get(type) && type == syms::put_rev_response() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::status].get(status))
    {
      return true;
    }
  return false;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
