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
#include "transforms.hh"

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

using json_io::builder;
using json_io::query;

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

    // revision symbols
    symbol const old_revision("old_revision");
    symbol const new_manifest("new_manifest");
    symbol const edges("edges");
    symbol const changes("changes");

    // file delta / data symbols
    symbol const id("id");
    symbol const src_id("src_id");
    symbol const dst_id("dst_id");
    symbol const delta("delta");
    symbol const data("data");
    symbol const data_records("data_records");
    symbol const delta_records("delta_records");

    // command symbols
    symbol const type("type");
    symbol const vers("vers");
    symbol const revs("revs");
    symbol const error("error");
    symbol const status("status");
    symbol const rev("rev");

    // request/response pairs
    symbol const inquire_request("inquire_request");
    symbol const inquire_response("inquire_response");

    symbol const descendants_request("descendants_request");
    symbol const descendants_response("descendants_response");

    symbol const get_full_rev_request("get_full_rev_request");
    symbol const get_full_rev_response("get_full_rev_response");

    symbol const put_full_rev_request("put_full_rev_request");
    symbol const put_full_rev_response("put_full_rev_response");

    symbol const get_rev_request("get_rev_request");
    symbol const get_rev_response("get_rev_response");

    symbol const put_rev_request("put_rev_request");
    symbol const put_rev_response("put_rev_response");

    symbol const get_file_data_request("get_file_data_request");
    symbol const get_file_data_response("get_file_data_response");

    symbol const put_file_data_request("put_file_data_request");
    symbol const put_file_data_response("put_file_data_response");

    symbol const get_file_delta_request("get_file_delta_request");
    symbol const get_file_delta_response("get_file_delta_response");

    symbol const put_file_delta_request("put_file_delta_request");
    symbol const put_file_delta_response("put_file_delta_response");
  }
}

bool
decode_msg_header(json_value_t val,
                  string & type,
                  string & vers)
{
  query q(val);
  if (q[syms::type].get(type) && q[syms::vers].get(vers))
    return true;
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'error'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_error(string const & note)
{
  builder b;
  b[syms::error].str(note);
  return b.v;
}

bool
decode_msg_error(json_value_t val,
                 std::string & note)
{
  query q(val);
  note.clear();
  return q[syms::error].get(note);
}


/////////////////////////////////////////////////////////////////////
// message type 'inquire_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_inquire_request(set<revision_id> const & revs)
{
  builder b;
  b[syms::type].str(syms::inquire_request());
  b[syms::vers].str("1");
  builder r = b[syms::revs].arr();
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool
decode_msg_inquire_request(json_value_t val,
                           set<revision_id> & revs)
{
  string type, vers;
  query q(val);
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
  builder b;
  b[syms::type].str(syms::inquire_response());
  b[syms::vers].str("1");
  builder r = b[syms::revs].arr();
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
  query q(val);
  if (q[syms::type].get(type) && type == syms::inquire_response() &&
      q[syms::vers].get(vers) && vers == "1")
    {
      size_t nrevs = 0;
      string tmp;
      query r = q[syms::revs];
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
  builder b;
  b[syms::type].str(syms::descendants_request());
  b[syms::vers].str("1");
  builder r = b[syms::revs].arr();
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool
decode_msg_descendants_request(json_value_t val,
                               set<revision_id> & revs)
{
  string type, vers;
  query q(val);
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
  builder b;
  b[syms::type].str(syms::descendants_response());
  b[syms::vers].str("1");
  builder r = b[syms::revs].arr();
  for (vector<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    r.add_str(i->inner()());
  return b.v;
}

bool
decode_msg_descendants_response(json_value_t val,
                                vector<revision_id> & revs)
{
  string type, vers;
  query q(val);
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
// encode/decode cset
/////////////////////////////////////////////////////////////////////


static void
encode_cset(builder b, cset const & cs)
{
  for (set<file_path>::const_iterator
         i = cs.nodes_deleted.begin(); i != cs.nodes_deleted.end(); ++i)
    {
      b.add_obj()[syms::delete_node].str(i->as_internal());
    }

  for (map<file_path, file_path>::const_iterator
         i = cs.nodes_renamed.begin(); i != cs.nodes_renamed.end(); ++i)
    {
      builder tmp = b.add_obj();
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
      builder tmp = b.add_obj();
      tmp[syms::add_file].str(i->first.as_internal());
      tmp[syms::content].str(i->second.inner()());
    }

  for (map<file_path, pair<file_id, file_id> >::const_iterator
         i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
    {
      builder tmp = b.add_obj();
      tmp[syms::patch].str(i->first.as_internal());
      tmp[syms::from].str(i->second.first.inner()());
      tmp[syms::to].str(i->second.second.inner()());
    }

  for (set<pair<file_path, attr_key> >::const_iterator
         i = cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
    {
      builder tmp = b.add_obj();
      tmp[syms::clear].str(i->first.as_internal());
      tmp[syms::attr].str(i->second());
    }

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator
         i = cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
    {
      builder tmp = b.add_obj();
      tmp[syms::set].str(i->first.first.as_internal());
      tmp[syms::attr].str(i->first.second());
      tmp[syms::value].str(i->second());
    }
}

static void
decode_cset(query q, cset & cs)
{
  size_t nargs = 0;
  I(q.len(nargs));
  for (size_t i = 0; i < nargs; ++i)
    {
      query change = q[i];
      string path;
      if (change[syms::delete_node].get(path))
        {
          cs.nodes_deleted.insert(file_path_internal(path));
        }
      else if (change[syms::rename].get(path))
        {
          string to;
          I(change[syms::to].get(to));
          cs.nodes_renamed.insert(make_pair(file_path_internal(path),
                                            file_path_internal(to)));
        }
      else if (change[syms::add_dir].get(path))
        {
          cs.dirs_added.insert(file_path_internal(path));
        }
      else if (change[syms::add_file].get(path))
        {
          string content;
          I(change[syms::content].get(content));
          cs.files_added.insert(make_pair(file_path_internal(path),
                                          file_id(content)));
        }
      else if (change[syms::patch].get(path))
        {
          string from, to;
          I(change[syms::from].get(from));
          I(change[syms::to].get(to));
          cs.deltas_applied.insert(make_pair(file_path_internal(path),
                                             make_pair(file_id(from),
                                                       file_id(to))));
        }
      else if (change[syms::clear].get(path))
        {
          string key;
          I(change[syms::attr].get(key));
          cs.attrs_cleared.insert(make_pair(file_path_internal(path),
                                            attr_key(key)));
        }
      else if (change[syms::set].get(path))
        {
          string key, val;
          I(change[syms::attr].get(key));
          I(change[syms::value].get(val));
          cs.attrs_set.insert(make_pair(make_pair(file_path_internal(path),
                                                  attr_key(key)),
                                        attr_value(val)));
        }
      else
        I(false);

    }
}

/////////////////////////////////////////////////////////////////////
// encode/decode rev
/////////////////////////////////////////////////////////////////////

static void
encode_rev(builder b, revision_t const & rev)
{
  b[syms::vers].str("1");
  b[syms::new_manifest].str(rev.new_manifest.inner()());
  builder edges = b[syms::edges].arr();
  for (edge_map::const_iterator e = rev.edges.begin();
       e != rev.edges.end(); ++e)
    {
      builder edge = edges.add_obj();
      edge[syms::old_revision].str(edge_old_revision(e).inner()());
      builder changes = edge[syms::changes].arr();
      encode_cset(changes, edge_changes(e));
    }
}

static void
decode_rev(query q, revision_t & rev)
{
  string new_manifest, vers;
  I(q[syms::new_manifest].get(new_manifest));
  I(q[syms::vers].get(vers));
  I(vers == "1");

  rev.new_manifest = manifest_id(new_manifest);
  size_t nargs = 0;
  query edges = q[syms::edges];
  I(edges.len(nargs));

  for (size_t i = 0; i < nargs; ++i)
    {
      query edge = edges[i];
      string old_revision;
      I(edge[syms::old_revision].get(old_revision));
      query changes = edge[syms::changes];
      shared_ptr<cset> cs(new cset());
      decode_cset(changes, *cs);
      rev.edges.insert(make_pair(revision_id(old_revision), cs));
    }
  rev.made_for = made_for_database;
}

/////////////////////////////////////////////////////////////////////
// encode/decode file data records
/////////////////////////////////////////////////////////////////////

static void
encode_data_records(builder b, vector<file_data_record> const & data_records)
{
  for (vector<file_data_record>::const_iterator
         i = data_records.begin(); i != data_records.end(); ++i)
    {
      builder tmp = b.add_obj();
      tmp[syms::id].str(i->id.inner()());
      tmp[syms::data].str(encode_base64(i->dat.inner())());
    }
}

static void
decode_data_records(query q, vector<file_data_record> & data_records)
{
  size_t nargs = 0;
  I(q.len(nargs));

  for (size_t i = 0; i < nargs; ++i)
    {
      query d = q[i];
      string id, dat;
      d[syms::id].get(id);
      d[syms::data].get(dat);
      file_data data(decode_base64_as<string>(dat));
      data_records.push_back(file_data_record(file_id(id),
                                              data));
    }
}

/////////////////////////////////////////////////////////////////////
// encode/decode file delta records
/////////////////////////////////////////////////////////////////////

static void
encode_delta_records(builder b, vector<file_delta_record> const & delta_records)
{
  for (vector<file_delta_record>::const_iterator
         i = delta_records.begin(); i != delta_records.end(); ++i)
    {
      builder tmp = b.add_obj();
      tmp[syms::src_id].str(i->src_id.inner()());
      tmp[syms::dst_id].str(i->dst_id.inner()());
      tmp[syms::delta].str(encode_base64(i->del.inner())());
    }
}

static void
decode_delta_records(query q, vector<file_delta_record> & delta_records)
{
  size_t nargs = 0;
  I(q.len(nargs));

  for (size_t i = 0; i < nargs; ++i)
    {
      query d = q[i];
      string src_id, dst_id, del;
      d[syms::src_id].get(src_id);
      d[syms::dst_id].get(dst_id);
      d[syms::delta].get(del);
      file_delta delta(decode_base64_as<string>(del));
      delta_records.push_back(file_delta_record(file_id(src_id),
                                                file_id(dst_id),
                                                delta));
    }
}

/////////////////////////////////////////////////////////////////////
// message type 'get_full_rev_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_full_rev_request(revision_id const & rid)
{
  builder b;
  b[syms::type].str(syms::get_full_rev_request());
  b[syms::vers].str("1");
  b[syms::id].str(rid.inner()());
  return b.v;
}

bool
decode_msg_get_full_rev_request(json_value_t val,
                                revision_id & rid)
{
  string type, vers, id;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_full_rev_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id))
    {
      rid = revision_id(id);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_full_rev_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_full_rev_response(revision_t const & rev,
                                 vector<file_data_record> const & data_records,
                                 vector<file_delta_record> const & delta_records)
{
  builder b;
  b[syms::type].str(syms::get_full_rev_response());
  b[syms::vers].str("1");
  builder rb = b[syms::rev].obj();
  encode_rev(rb, rev);
  builder dat = b[syms::data_records].arr();
  encode_data_records(dat, data_records);
  builder del = b[syms::delta_records].arr();
  encode_delta_records(del, delta_records);
  return b.v;
}

bool
decode_msg_get_full_rev_response(json_value_t val,
                                 revision_t & rev,
                                 vector<file_data_record> & data_records,
                                 vector<file_delta_record> & delta_records)
{
  string type, vers;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_full_rev_response() &&
      q[syms::vers].get(vers) && vers == "1")
    {
      query rq = q[syms::rev];
      decode_rev(rq, rev);
      query dat = q[syms::data_records];
      decode_data_records(dat, data_records);
      query del = q[syms::delta_records];
      decode_delta_records(del, delta_records);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_full_rev_request'
/////////////////////////////////////////////////////////////////////

json_value_t encode_msg_put_full_rev_request(revision_id const & rid,
                                             revision_t const & rev,
                                             vector<file_data_record> const & data_records,
                                             vector<file_delta_record> const & delta_records)
{
  builder b;
  b[syms::type].str(syms::put_full_rev_request());
  b[syms::vers].str("1");
  b[syms::id].str(rid.inner()());
  builder rb = b[syms::rev].obj();
  encode_rev(rb, rev);
  builder dat = b[syms::data_records].arr();
  encode_data_records(dat, data_records);
  builder del = b[syms::delta_records].arr();
  encode_delta_records(del, delta_records);
  return b.v;
}

bool
decode_msg_put_full_rev_request(json_value_t val,
                                revision_id & rid,
                                revision_t & rev,
                                vector<file_data_record> & data_records,
                                vector<file_delta_record> & delta_records)
{
  string type, vers, id;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_full_rev_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id))
    {
      rid = revision_id(id);
      query rq = q[syms::rev];
      decode_rev(rq, rev);
      query dat = q[syms::data_records];
      decode_data_records(dat, data_records);
      query del = q[syms::delta_records];
      decode_delta_records(del, delta_records);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_full_rev_response'
/////////////////////////////////////////////////////////////////////

json_value_t encode_msg_put_full_rev_response()
{
  builder b;
  b[syms::type].str(syms::put_full_rev_response());
  b[syms::vers].str("1");
  b[syms::status].str("received");
  return b.v;
}

bool
decode_msg_put_full_rev_response(json_value_t val)
{
  string type, vers, status;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_full_rev_response() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::status].get(status))
    {
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_rev_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_rev_request(revision_id const & rid)
{
  builder b;
  b[syms::type].str(syms::get_rev_request());
  b[syms::vers].str("1");
  b[syms::id].str(rid.inner()());
  return b.v;
}

bool
decode_msg_get_rev_request(json_value_t val, revision_id & rid)
{
  string type, vers, id;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_rev_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id))
    {
      rid = revision_id(id);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_rev_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_rev_response(revision_t const & rev)
{
  builder b;
  b[syms::type].str(syms::get_rev_response());
  b[syms::vers].str("1");
  builder rb = b[syms::rev].obj();
  encode_rev(rb, rev);
  return b.v;
}

bool
decode_msg_get_rev_response(json_value_t val, revision_t & rev)
{
  string type, vers;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_rev_response() &&
      q[syms::vers].get(vers) && vers == "1")
    {
      query rq = q[syms::rev];
      decode_rev(rq, rev);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_rev_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_put_rev_request(revision_id const & rid, revision_t const & rev)
{
  builder b;
  b[syms::type].str(syms::put_rev_request());
  b[syms::vers].str("1");
  b[syms::id].str(rid.inner()());
  builder rb = b[syms::rev].obj();
  encode_rev(rb, rev);
  return b.v;
}

bool
decode_msg_put_rev_request(json_value_t val, revision_id & rid, revision_t & rev)
{
  string type, vers, id;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_rev_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id))
    {
      rid = revision_id(id);
      query rq = q[syms::rev];
      decode_rev(rq, rev);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_rev_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_put_rev_response()
{
  builder b;
  b[syms::type].str(syms::put_rev_response());
  b[syms::vers].str("1");
  b[syms::status].str("received");
  return b.v;
}

bool
decode_msg_put_rev_response(json_value_t val)
{
  string type, vers, status;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_rev_response() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::status].get(status))
    {
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_file_data_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_file_data_request(file_id const & fid)
{
  builder b;
  b[syms::type].str(syms::get_file_data_request());
  b[syms::vers].str("1");
  b[syms::id].str(fid.inner()());
  return b.v;
}

bool
decode_msg_get_file_data_request(json_value_t val,
                                 file_id & fid)
{
  string type, vers, id;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_file_data_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id))
    {
      fid = file_id(id);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_file_data_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_file_data_response(file_data const & data)
{
  builder b;
  b[syms::type].str(syms::get_file_data_response());
  b[syms::vers].str("1");
  b[syms::data].str(encode_base64(data.inner())());
  return b.v;
}

bool
decode_msg_get_file_data_response(json_value_t val,
                                  file_data & data)
{
  string type, vers, dat;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_file_data_response() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::data].get(dat))
    {
      data = file_data(decode_base64_as<string>(dat));
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_file_data_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_put_file_data_request(file_id const & fid,
                                 file_data const & data)
{
  builder b;
  b[syms::type].str(syms::put_file_data_request());
  b[syms::vers].str("1");
  b[syms::id].str(fid.inner()());
  b[syms::data].str(encode_base64(data.inner())());
  return b.v;
}

bool
decode_msg_put_file_data_request(json_value_t val,
                                 file_id & fid,
                                 file_data & data)
{
  string type, vers, id, dat;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_file_data_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::id].get(id) &&
      q[syms::data].get(dat))
    {
      fid = file_id(id);
      data = file_data(decode_base64_as<string>(dat));
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_file_data_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_put_file_data_response()
{
  builder b;
  b[syms::type].str(syms::put_file_data_response());
  b[syms::vers].str("1");
  b[syms::status].str("received");
  return b.v;
}

bool
decode_msg_put_file_data_response(json_value_t val)
{
  string type, vers, status;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_file_data_response() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::status].get(status))
    {
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_file_delta_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_file_delta_request(file_id const & src_id,
                                  file_id const & dst_id)
{
  builder b;
  b[syms::type].str(syms::get_file_delta_request());
  b[syms::vers].str("1");
  b[syms::src_id].str(src_id.inner()());
  b[syms::dst_id].str(dst_id.inner()());
  return b.v;
}

bool
decode_msg_get_file_delta_request(json_value_t val,
                                  file_id & src_id,
                                  file_id & dst_id)
{
  string type, vers, src, dst;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_file_delta_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::src_id].get(src) &&
      q[syms::dst_id].get(dst))
    {
      src_id = file_id(src);
      dst_id = file_id(dst);
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'get_file_delta_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_get_file_delta_response(file_delta const & delta)
{
  builder b;
  b[syms::type].str(syms::get_file_delta_response());
  b[syms::vers].str("1");
  b[syms::delta].str(encode_base64(delta.inner())());
  return b.v;
}

bool
decode_msg_get_file_delta_response(json_value_t val,
                                  file_delta & delta)
{
  string type, vers, del;
  query q(val);
  if (q[syms::type].get(type) && type == syms::get_file_delta_response() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::delta].get(del))
    {
      delta = file_delta(decode_base64_as<string>(del));
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_file_delta_request'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_put_file_delta_request(file_id const & src_id,
                                  file_id const & dst_id,
                                  file_delta const & delta)
{
  builder b;
  b[syms::type].str(syms::put_file_delta_request());
  b[syms::vers].str("1");
  b[syms::src_id].str(src_id.inner()());
  b[syms::dst_id].str(dst_id.inner()());
  b[syms::delta].str(encode_base64(delta.inner())());
  return b.v;
}

bool
decode_msg_put_file_delta_request(json_value_t val,
                                  file_id & src_id,
                                  file_id & dst_id,
                                  file_delta & delta)
{
  string type, vers, src, dst, del;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_file_delta_request() &&
      q[syms::vers].get(vers) && vers == "1" &&
      q[syms::src_id].get(src) &&
      q[syms::src_id].get(src) &&
      q[syms::delta].get(del))
    {
      src_id = file_id(src);
      dst_id = file_id(dst);
      delta = file_delta(decode_base64_as<string>(del));
      return true;
    }
  return false;
}

/////////////////////////////////////////////////////////////////////
// message type 'put_file_delta_response'
/////////////////////////////////////////////////////////////////////

json_value_t
encode_msg_put_file_delta_response()
{
  builder b;
  b[syms::type].str(syms::put_file_delta_response());
  b[syms::vers].str("1");
  b[syms::status].str("received");
  return b.v;
}

bool
decode_msg_put_file_delta_response(json_value_t val)
{
  string type, vers, status;
  query q(val);
  if (q[syms::type].get(type) && type == syms::put_file_delta_response() &&
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
