// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains "diff"/"patch" code that operates directly on rosters
// (with their associated markings).

#include "base.hh"
#include <set>
#include <map>

#include "lexical_cast.hh"

#include "netio.hh"
#include "safe_map.hh"
#include "parallel_iter.hh"
#include "roster.hh"
#include "roster_delta.hh"
#include "basic_io.hh"
#include "paths.hh"
#include "transforms.hh"

using boost::lexical_cast;

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;

namespace
{

  struct roster_delta_t
  {
    typedef std::set<node_id> nodes_deleted_t;
    typedef std::map<pair<node_id, path_component>,
                     node_id> dirs_added_t;
    typedef std::map<pair<node_id, path_component>,
                     pair<node_id, file_id> > files_added_t;
    typedef std::map<node_id,
                     pair<node_id, path_component> > nodes_renamed_t;
    typedef std::map<node_id, file_id> deltas_applied_t;
    typedef std::set<pair<node_id, attr_key> > attrs_cleared_t;
    typedef std::set<pair<node_id,
                          pair<attr_key,
                               pair<bool, attr_value> > > > attrs_changed_t;
    typedef std::map<node_id, marking_t> markings_changed_t;

    nodes_deleted_t nodes_deleted;
    dirs_added_t dirs_added;
    files_added_t files_added;
    nodes_renamed_t nodes_renamed;
    deltas_applied_t deltas_applied;
    attrs_cleared_t attrs_cleared;
    attrs_changed_t attrs_changed;

    // nodes_deleted are automatically removed from the marking_map; these are
    // all markings that are new or changed
    markings_changed_t markings_changed;

    void
    apply(roster_t & roster, marking_map & markings) const;
  };

  void
  roster_delta_t::apply(roster_t & roster, marking_map & markings) const
  {
    // Detach everything that should be detached.
    for (nodes_deleted_t::const_iterator
           i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      roster.detach_node(*i);
    for (nodes_renamed_t::const_iterator
           i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
      roster.detach_node(i->first);

    // Delete the delete-able things.
    for (nodes_deleted_t::const_iterator
           i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      roster.drop_detached_node(*i);

    // Add the new things.
    for (dirs_added_t::const_iterator
           i = dirs_added.begin(); i != dirs_added.end(); ++i)
      roster.create_dir_node(i->second);
    for (files_added_t::const_iterator
           i = files_added.begin(); i != files_added.end(); ++i)
      roster.create_file_node(i->second.second, i->second.first);

    // Attach everything.
    for (dirs_added_t::const_iterator
           i = dirs_added.begin(); i != dirs_added.end(); ++i)
      roster.attach_node(i->second, i->first.first, i->first.second);
    for (files_added_t::const_iterator
           i = files_added.begin(); i != files_added.end(); ++i)
      roster.attach_node(i->second.first, i->first.first, i->first.second);
    for (nodes_renamed_t::const_iterator
           i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
      roster.attach_node(i->first, i->second.first, i->second.second);

    // Okay, all the tricky tree-rearranging is done, just have to do some
    // individual node edits now.
    for (deltas_applied_t::const_iterator
           i = deltas_applied.begin(); i != deltas_applied.end(); ++i)
      roster.set_content(i->first, i->second);

    for (attrs_cleared_t::const_iterator
           i = attrs_cleared.begin(); i != attrs_cleared.end(); ++i)
      roster.erase_attr(i->first, i->second);

    for (attrs_changed_t::const_iterator
           i = attrs_changed.begin(); i != attrs_changed.end(); ++i)
      roster.set_attr_unknown_to_dead_ok(i->first, i->second.first, i->second.second);

    // And finally, update the marking map.
    for (nodes_deleted_t::const_iterator
           i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      safe_erase(markings, *i);
    for (markings_changed_t::const_iterator
           i = markings_changed.begin(); i != markings_changed.end(); ++i)
      markings[i->first] = i->second;
  }

  void
  do_delta_for_node_only_in_dest(node_t new_n, roster_delta_t & d)
  {
    node_id nid = new_n->self;
    pair<node_id, path_component> new_loc(new_n->parent, new_n->name);

    if (is_dir_t(new_n))
      safe_insert(d.dirs_added, make_pair(new_loc, nid));
    else
      {
        file_id const & content = downcast_to_file_t(new_n)->content;
        safe_insert(d.files_added, make_pair(new_loc,
                                             make_pair(nid, content)));
      }
    for (full_attr_map_t::const_iterator i = new_n->attrs.begin();
         i != new_n->attrs.end(); ++i)
      safe_insert(d.attrs_changed, make_pair(nid, *i));
  }

  void
  do_delta_for_node_in_both(node_t old_n, node_t new_n, roster_delta_t & d)
  {
    I(old_n->self == new_n->self);
    node_id nid = old_n->self;
    // rename?
    {
      pair<node_id, path_component> old_loc(old_n->parent, old_n->name);
      pair<node_id, path_component> new_loc(new_n->parent, new_n->name);
      if (old_loc != new_loc)
        safe_insert(d.nodes_renamed, make_pair(nid, new_loc));
    }
    // delta?
    if (is_file_t(old_n))
      {
        file_id const & old_content = downcast_to_file_t(old_n)->content;
        file_id const & new_content = downcast_to_file_t(new_n)->content;
        if (!(old_content == new_content))
          safe_insert(d.deltas_applied, make_pair(nid, new_content));
      }
    // attrs?
    {
      parallel::iter<full_attr_map_t> i(old_n->attrs, new_n->attrs);
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);

            case parallel::in_left:
              safe_insert(d.attrs_cleared, make_pair(nid, i.left_key()));
              break;

            case parallel::in_right:
              safe_insert(d.attrs_changed, make_pair(nid, i.right_value()));
              break;

            case parallel::in_both:
              if (i.left_data() != i.right_data())
                safe_insert(d.attrs_changed, make_pair(nid, i.right_value()));
              break;
            }
        }
    }
  }

  void
  make_roster_delta_t(roster_t const & from, marking_map const & from_markings,
                      roster_t const & to, marking_map const & to_markings,
                      roster_delta_t & d)
  {
    MM(from);
    MM(from_markings);
    MM(to);
    MM(to_markings);
    {
      parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);

            case parallel::in_left:
              // deleted
              safe_insert(d.nodes_deleted, i.left_key());
              break;

            case parallel::in_right:
              // added
              do_delta_for_node_only_in_dest(i.right_data(), d);
              break;

            case parallel::in_both:
              // moved/patched/attribute changes
              do_delta_for_node_in_both(i.left_data(), i.right_data(), d);
              break;
            }
        }
    }
    {
      parallel::iter<marking_map> i(from_markings, to_markings);
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);

            case parallel::in_left:
              // deleted; don't need to do anything (will be handled by
              // nodes_deleted set
              break;

            case parallel::in_right:
              // added
              safe_insert(d.markings_changed, i.right_value());
              break;

            case parallel::in_both:
              // maybe changed
              if (!(i.left_data() == i.right_data()))
                safe_insert(d.markings_changed, i.right_value());
              break;
            }
        }
    }
  }

  namespace syms
  {
    symbol const deleted("deleted");
    symbol const rename("rename");
    symbol const add_dir("add_dir");
    symbol const add_file("add_file");
    symbol const delta("delta");
    symbol const attr_cleared("attr_cleared");
    symbol const attr_changed("attr_changed");
    symbol const marking("marking");

    symbol const content("content");
    symbol const location("location");
    symbol const attr("attr");
    symbol const value("value");
  }

  node_id
  parse_nid(basic_io::parser & parser)
  {
    std::string s;
    parser.str(s);
    return lexical_cast<node_id>(s);
  }

  void
  parse_loc(basic_io::parser & parser,
            pair<node_id, path_component> & loc)
  {
    parser.esym(syms::location);
    loc.first = parse_nid(parser);
    std::string name;
    parser.str(name);
    loc.second = path_component(name);
  }

  void
  parse_roster_delta_t(basic_io::parser & parser, roster_delta_t & d)
  {
    while (parser.symp(syms::deleted))
      {
        parser.sym();
        safe_insert(d.nodes_deleted, parse_nid(parser));
      }
    while (parser.symp(syms::rename))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.nodes_renamed, make_pair(nid, loc));
      }
    while (parser.symp(syms::add_dir))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.dirs_added, make_pair(loc, nid));
      }
    while (parser.symp(syms::add_file))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        parser.esym(syms::content);
        std::string s;
        parser.hex(s);
        safe_insert(d.files_added,
                    make_pair(loc, make_pair(nid, file_id(decode_hexenc(s)))));
      }
    while (parser.symp(syms::delta))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::content);
        std::string s;
        parser.hex(s);
        safe_insert(d.deltas_applied, make_pair(nid, file_id(decode_hexenc(s))));
      }
    while (parser.symp(syms::attr_cleared))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::attr);
        std::string key;
        parser.str(key);
        safe_insert(d.attrs_cleared, make_pair(nid, attr_key(key)));
      }
    while (parser.symp(syms::attr_changed))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::attr);
        std::string key;
        parser.str(key);
        parser.esym(syms::value);
        std::string value_bool, value_value;
        parser.str(value_bool);
        parser.str(value_value);
        pair<bool, attr_value> full_value(lexical_cast<bool>(value_bool),
                                          attr_value(value_value));
        safe_insert(d.attrs_changed,
                    make_pair(nid,
                              make_pair(attr_key(key), full_value)));
      }
    while (parser.symp(syms::marking))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        marking_t m;
        parse_marking(parser, m);
        safe_insert(d.markings_changed, make_pair(nid, m));
      }
  }

  namespace header
  {
    string const binary_roster_delta("\0x00\0x01 roster delta");
  };

  namespace tags
  {
    char const node_deleted = 0x01;
    char const node_renamed = 0x02;
    char const dir_added = 0x03;
    char const file_added = 0x04;
    char const delta_applied = 0x05;
    char const attr_cleared = 0x06;
    char const attr_changed = 0x07;
    char const marking_changed = 0x08;

    // these belong in roster.cc with encode_marking
    char const birth_mark = 0x22;
    char const path_mark = 0x23;
    char const content_mark = 0x24;
    char const attr_mark = 0x25;
  };

  class roster_delta_encoder
  {

  public:
    roster_delta_encoder(roster_delta_t const & d) :
      d(d) {}

    roster_delta encode()
    {
      encode_header(header::binary_roster_delta);

      for (roster_delta_t::nodes_deleted_t::const_iterator
             i = d.nodes_deleted.begin(); i != d.nodes_deleted.end(); ++i)
        {
          encode_tag(tags::node_deleted);
          encode_node_id(*i);
        }
      for (roster_delta_t::nodes_renamed_t::const_iterator
             i = d.nodes_renamed.begin(); i != d.nodes_renamed.end(); ++i)
        {
          encode_tag(tags::node_renamed);
          encode_node_id(i->first);
          encode_loc(i->second);
        }
      for (roster_delta_t::dirs_added_t::const_iterator
             i = d.dirs_added.begin(); i != d.dirs_added.end(); ++i)
        {
          encode_tag(tags::dir_added);
          encode_loc(i->first);
          encode_node_id(i->second);
        }
      for (roster_delta_t::files_added_t::const_iterator
             i = d.files_added.begin(); i != d.files_added.end(); ++i)
        {
          encode_tag(tags::file_added);
          encode_loc(i->first);
          encode_node_id(i->second.first);
          encode_file_id(i->second.second);
        }
      for (roster_delta_t::deltas_applied_t::const_iterator
             i = d.deltas_applied.begin(); i != d.deltas_applied.end(); ++i)
        {
          encode_tag(tags::delta_applied);
          encode_node_id(i->first);
          encode_file_id(i->second);
        }
      for (roster_delta_t::attrs_cleared_t::const_iterator
             i = d.attrs_cleared.begin(); i != d.attrs_cleared.end(); ++i)
        {
          encode_tag(tags::attr_cleared);
          encode_node_id(i->first);
          encode_string(i->second()); // attr key
        }
      for (roster_delta_t::attrs_changed_t::const_iterator
             i = d.attrs_changed.begin(); i != d.attrs_changed.end(); ++i)
        {
          encode_tag(tags::attr_changed);
          encode_node_id(i->first);
          encode_string(i->second.first()); // attr key
          encode_bool(i->second.second.first); // attr live flag
          encode_string(i->second.second.second()); // attr value
        }
      for (roster_delta_t::markings_changed_t::const_iterator
             i = d.markings_changed.begin(); i != d.markings_changed.end(); ++i)
        {
          encode_tag(tags::marking_changed);
          encode_node_id(i->first);
          encode_marking(i->second);
        }

      return roster_delta(bytes);
    }

  private:
    roster_delta_t const & d;
    string bytes;

    void encode_header(string const & header)
    {
      bytes += header;
    }

    void encode_tag(char const tag)
    {
      bytes += tag;
    }

    void encode_string(string const & s)
    {
      insert_datum_uleb128(s.size(), bytes);
      bytes += s;
    }

    void encode_bool(bool const b)
    {
      insert_datum_uleb128(b, bytes);
    }

    void encode_file_id(file_id const & fid)
    {
      I(fid.inner()().size() == 20);
      bytes += fid.inner()();
    }

    void encode_rev_id(revision_id const & rid)
    {
      I(rid.inner()().size() == 20);
      bytes += rid.inner()();
    }

    void encode_node_id(node_id const nid)
    {
      insert_datum_uleb128(nid, bytes);
    }

    void encode_loc(pair<node_id, path_component> const & loc)
    {
      encode_node_id(loc.first);
      encode_string(loc.second());
    }

    // this ultimately belongs in roster.cc
    void encode_marking(marking_t const & marking)
    {
      bool is_file = !marking.file_content.empty();

      I(!null_id(marking.birth_revision));
      encode_tag(tags::birth_mark);
      encode_rev_id(marking.birth_revision);

      for (set<revision_id>::const_iterator
             i = marking.parent_name.begin(); i != marking.parent_name.end(); ++i)
        {
          encode_tag(tags::path_mark);
          encode_rev_id(*i);
        }

      if (is_file)
        {
          for (set<revision_id>::const_iterator
                 i = marking.file_content.begin(); i != marking.file_content.end(); ++i)
            {
              encode_tag(tags::content_mark);
              encode_rev_id(*i);
            }
        }
      else
        I(marking.file_content.empty());

      for (map<attr_key, set<revision_id> >::const_iterator
             i = marking.attrs.begin(); i != marking.attrs.end(); ++i)
        {
          for (set<revision_id>::const_iterator
                 j = i->second.begin(); j != i->second.end(); ++j)
            {
              encode_tag(tags::attr_mark);
              encode_string(i->first());
              encode_rev_id(*j);
            }
        }
    }

  };

  class roster_delta_decoder
  {

  public:
    roster_delta_decoder(roster_delta const & del) :
      bytes(del.inner()()), pos(0), name("roster delta decoder") {}

    roster_delta_t decode()
    {
      if (!decode_header(header::binary_roster_delta))
        {
          // this is not a binary roster delta
          // fall back to the old basic_io parser
          basic_io::input_source src(bytes, "roster_delta");
          basic_io::tokenizer tok(src);
          basic_io::parser pars(tok);
          parse_roster_delta_t(pars, d);
          return d;
        }

      while (decode_tag(tags::node_deleted))
        {
          node_id nid = decode_node_id();
          safe_insert(d.nodes_deleted, nid);
        }
      while (decode_tag(tags::node_renamed))
        {
          node_id nid = decode_node_id();
          pair<node_id, path_component> loc = decode_loc();
          safe_insert(d.nodes_renamed, make_pair(nid, loc));
        }
      while (decode_tag(tags::dir_added))
        {
          pair<node_id, path_component> loc = decode_loc();
          node_id nid = decode_node_id();
          safe_insert(d.dirs_added, make_pair(loc, nid));
        }
      while (decode_tag(tags::file_added))
        {
          pair<node_id, path_component> loc = decode_loc();
          node_id nid = decode_node_id();
          file_id fid = decode_file_id();
          safe_insert(d.files_added,
                      make_pair(loc, make_pair(nid, fid)));
        }
      while (decode_tag(tags::delta_applied))
        {
          node_id nid = decode_node_id();
          file_id fid = decode_file_id();
          safe_insert(d.deltas_applied, make_pair(nid, fid));
        }
      while (decode_tag(tags::attr_cleared))
        {
          node_id nid = decode_node_id();
          string key = decode_string();
          safe_insert(d.attrs_cleared, make_pair(nid, attr_key(key)));
        }
      while (decode_tag(tags::attr_changed))
        {
          node_id nid = decode_node_id();
          string key = decode_string();
          bool live = decode_bool();
          string value = decode_string();
          pair<bool, attr_value> full_value(live, attr_value(value));
          safe_insert(d.attrs_changed,
                      make_pair(nid,
                                make_pair(attr_key(key), full_value)));
        }
      while (decode_tag(tags::marking_changed))
        {
          node_id nid = decode_node_id();
          marking_t m;
          decode_marking(m);
          safe_insert(d.markings_changed, make_pair(nid, m));
        }

      return d;
    }


  private:
    roster_delta_t d;
    string const & bytes;
    size_t pos;
    string const name;

    bool decode_header(string const & header)
    {
      I(pos == 0);
      if (bytes.size() < header.size())
        return false;
      return bytes.substr(0, header.size()) == header;
    }

    bool decode_tag(char const tag)
    {

      if (pos < bytes.size() && bytes[pos] == tag)
        {
          pos++;
          return true;
        }
      return false;
    }

    string decode_string()
    {
      size_t len = extract_datum_uleb128<size_t>(bytes, pos, name);
      I(pos + len <= bytes.size());
      string s = bytes.substr(pos, len);
      pos += len;
      return s;
    }

    bool decode_bool()
    {
      bool b = extract_datum_uleb128<bool>(bytes, pos, name);
      return b;
    }

    file_id decode_file_id()
    {
      I(pos + 20 <= bytes.size());
      file_id fid(bytes.substr(pos, 20));
      pos += 20;
      return fid;
    }

    revision_id decode_rev_id()
    {
      I(pos + 20 <= bytes.size());
      revision_id rid(bytes.substr(pos, 20));
      pos += 20;
      return rid;
    }

    node_id decode_node_id()
    {
      node_id nid = extract_datum_uleb128<node_id>(bytes, pos, name);
      return nid;
    }

    pair<node_id, path_component> decode_loc()
    {
      node_id nid = decode_node_id();
      string s = decode_string();
      return make_pair(nid, s);
    }

    // this ultimately belongs in roster.cc
    void decode_marking(marking_t & marking)
    {

      I(decode_tag(tags::birth_mark));
      marking.birth_revision = decode_rev_id();

      while (decode_tag(tags::path_mark))
        {
          revision_id rid = decode_rev_id();
          safe_insert(marking.parent_name, rid);
        }
      while (decode_tag(tags::content_mark))
        {
          revision_id rid = decode_rev_id();
          safe_insert(marking.file_content, rid);
        }
      while (decode_tag(tags::attr_mark))
        {
          attr_key key(decode_string());
          revision_id rid = decode_rev_id();
          safe_insert(marking.attrs[key], rid);
        }
    }

  };


} // end anonymous namespace

void
delta_rosters(roster_t const & from, marking_map const & from_markings,
              roster_t const & to, marking_map const & to_markings,
              roster_delta & del)
{
  MM(from);
  MM(from_markings);
  MM(to);
  MM(to_markings);
  roster_delta_t d;
  make_roster_delta_t(from, from_markings, to, to_markings, d);

  roster_delta_encoder encoder(d);
  del = encoder.encode();
}

static
void read_roster_delta(roster_delta const & del,
                       roster_delta_t & d)
{
  roster_delta_decoder decoder(del);
  d = decoder.decode();
}

void
apply_roster_delta(roster_delta const & del,
                   roster_t & roster, marking_map & markings)
{
  MM(del);
  MM(roster);
  MM(markings);

  roster_delta_t d;
  read_roster_delta(del, d);
  d.apply(roster, markings);
}

// Extract the marking for one node from the roster delta, or return false
// if they are not contained in that delta
bool
try_get_markings_from_roster_delta(roster_delta const & del,
                                   node_id const & nid,
                                   marking_t & markings)
{
  roster_delta_t d;
  read_roster_delta(del, d);

  std::map<node_id, marking_t>::iterator i = d.markings_changed.find(nid);
  if (i != d.markings_changed.end())
    {
      markings = i->second;
      return true;
    }
  else
    {
      return false;
    }
}

// Extract the content hash for one node from the roster delta -- if it is
// available.  If we know what the file_id was, we return true and set
// 'content' appropriately.  If we can prove that the file did not exist in
// this revision, we return true and set 'content' to a null id.  If we
// cannot determine anything about the file contents, then we return false
// -- in this case content is left undefined.
bool
try_get_content_from_roster_delta(roster_delta const & del,
                                  node_id const & nid,
                                  file_id & content)
{
  roster_delta_t d;
  read_roster_delta(del, d);

  roster_delta_t::deltas_applied_t::const_iterator i = d.deltas_applied.find(nid);
  if (i != d.deltas_applied.end())
    {
      content = i->second;
      return true;
    }

  // special case 1: the node was deleted, so we know for sure it's not
  // there anymore in this roster
  if (d.nodes_deleted.find(nid) != d.nodes_deleted.end())
    {
      content = file_id();
      return true;
    }

  // special case 2: the node was added, so we need to get the current
  // content hash from the add stanza
  for (roster_delta_t::files_added_t::const_iterator j = d.files_added.begin();
       j != d.files_added.end(); ++j)
    {
      if (j->second.first == nid)
        {
          content = j->second.second;
          return true;
        }
    }

  return false;
}

#ifdef BUILD_UNIT_TESTS

static void
spin(roster_t const & from, marking_map const & from_marking,
     roster_t const & to, marking_map const & to_marking)
{
  MM(from);
  MM(from_marking);
  MM(to);
  MM(to_marking);
  roster_delta del;
  MM(del);
  delta_rosters(from, from_marking, to, to_marking, del);

  roster_t tmp(from);
  MM(tmp);
  marking_map tmp_marking(from_marking);
  MM(tmp_marking);
  apply_roster_delta(del, tmp, tmp_marking);
  I(tmp == to);
  I(tmp_marking == to_marking);

  roster_delta del2;
  delta_rosters(from, from_marking, tmp, tmp_marking, del2);
  I(del == del2);
}

void test_roster_delta_on(roster_t const & a, marking_map const & a_marking,
                          roster_t const & b, marking_map const & b_marking)
{
  spin(a, a_marking, b, b_marking);
  spin(b, b_marking, a, a_marking);
}

#endif // BUILD_UNIT_TESTS


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
