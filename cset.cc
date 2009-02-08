// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <set>

#include "basic_io.hh"
#include "cset.hh"
#include "sanity.hh"
#include "safe_map.hh"
#include "transforms.hh"

using std::set;
using std::map;
using std::pair;
using std::string;
using std::make_pair;

static void
check_normalized(cset const & cs)
{
  MM(cs);

  // normalize:
  //
  //   add_file foo@id1 + apply_delta id1->id2
  //   clear_attr foo:bar + set_attr foo:bar=baz
  //
  // possibly more?

  // no file appears in both the "added" list and the "patched" list
  {
    map<file_path, file_id>::const_iterator a = cs.files_added.begin();
    map<file_path, pair<file_id, file_id> >::const_iterator
      d = cs.deltas_applied.begin();
    while (a != cs.files_added.end() && d != cs.deltas_applied.end())
      {
        // SPEEDUP?: should this use lower_bound instead of ++?  it should
        // make this loop iterate about as many times as the shorter list,
        // rather the sum of the two lists...
        if (a->first < d->first)
          ++a;
        else if (d->first < a->first)
          ++d;
        else
          I(false);
      }
  }

  // no file+attr pair appears in both the "set" list and the "cleared" list
  {
    set<pair<file_path, attr_key> >::const_iterator c = cs.attrs_cleared.begin();
    map<pair<file_path, attr_key>, attr_value>::const_iterator
      s = cs.attrs_set.begin();
    while (c != cs.attrs_cleared.end() && s != cs.attrs_set.end())
      {
        if (*c < s->first)
          ++c;
        else if (s->first < *c)
          ++s;
        else
          I(false);
      }
  }
}

bool
cset::empty() const
{
  return
    nodes_deleted.empty()
    && dirs_added.empty()
    && files_added.empty()
    && nodes_renamed.empty()
    && deltas_applied.empty()
    && attrs_cleared.empty()
    && attrs_set.empty();
}

void
cset::clear()
{
  nodes_deleted.clear();
  dirs_added.clear();
  files_added.clear();
  nodes_renamed.clear();
  deltas_applied.clear();
  attrs_cleared.clear();
  attrs_set.clear();
}

struct
detach
{
  detach(file_path const & src)
    : src_path(src),
      reattach(false)
  {}

  detach(file_path const & src,
         file_path const & dst)
    : src_path(src),
      reattach(true),
      dst_path(dst)
  {}

  file_path src_path;
  bool reattach;
  file_path dst_path;

  bool operator<(struct detach const & other) const
  {
    // We sort detach operations bottom-up by src path
    // SPEEDUP?: simply sort by path.size() rather than full lexicographical
    // comparison?
    return other.src_path < src_path;
  }
};

struct
attach
{
  attach(node_id n,
         file_path const & p)
    : node(n), path(p)
  {}

  node_id node;
  file_path path;

  bool operator<(struct attach const & other) const
  {
    // We sort attach operations top-down by path
    // SPEEDUP?: simply sort by path.size() rather than full lexicographical
    // comparison?
    return path < other.path;
  }
};

void
cset::apply_to(editable_tree & t) const
{
  // SPEEDUP?: use vectors and sort them once, instead of maintaining sorted
  // sets?
  set<detach> detaches;
  set<attach> attaches;
  set<node_id> drops;

  MM(*this);

  check_normalized(*this);

  // Decompose all additions into a set of pending attachments to be
  // executed top-down. We might as well do this first, to be sure we
  // can form the new nodes -- such as in a filesystem -- before we do
  // anything else potentially destructive. This should all be
  // happening in a temp directory anyways.

  // NB: it's very important we do safe_insert's here, because our comparison
  // operator for attach and detach does not distinguish all nodes!  the nodes
  // that it does not distinguish are ones where we're attaching or detaching
  // repeatedly from the same place, so they're impossible anyway, but we need
  // to error out if someone tries to add them.

  for (set<file_path>::const_iterator i = dirs_added.begin();
       i != dirs_added.end(); ++i)
    safe_insert(attaches, attach(t.create_dir_node(), *i));

  for (map<file_path, file_id>::const_iterator i = files_added.begin();
       i != files_added.end(); ++i)
    safe_insert(attaches, attach(t.create_file_node(i->second), i->first));


  // Decompose all path deletion and the first-half of renamings on
  // existing paths into the set of pending detaches, to be executed
  // bottom-up.

  for (set<file_path>::const_iterator i = nodes_deleted.begin();
       i != nodes_deleted.end(); ++i)
    safe_insert(detaches, detach(*i));

  for (map<file_path, file_path>::const_iterator i = nodes_renamed.begin();
       i != nodes_renamed.end(); ++i)
    safe_insert(detaches, detach(i->first, i->second));


  // Execute all the detaches, rescheduling the results of each detach
  // for either attaching or dropping.

  for (set<detach>::const_iterator i = detaches.begin();
       i != detaches.end(); ++i)
    {
      node_id n = t.detach_node(i->src_path);
      if (i->reattach)
        safe_insert(attaches, attach(n, i->dst_path));
      else
        safe_insert(drops, n);
    }


  // Execute all the attaches.

  for (set<attach>::const_iterator i = attaches.begin(); i != attaches.end(); ++i)
    t.attach_node(i->node, i->path);


  // Execute all the drops.

  for (set<node_id>::const_iterator i = drops.begin(); i != drops.end(); ++i)
    t.drop_detached_node (*i);


  // Execute all the in-place edits
  for (map<file_path, pair<file_id, file_id> >::const_iterator i = deltas_applied.begin();
       i != deltas_applied.end(); ++i)
    t.apply_delta(i->first, i->second.first, i->second.second);

  for (set<pair<file_path, attr_key> >::const_iterator i = attrs_cleared.begin();
       i != attrs_cleared.end(); ++i)
    t.clear_attr(i->first, i->second);

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = attrs_set.begin();
       i != attrs_set.end(); ++i)
    t.set_attr(i->first.first, i->first.second, i->second);

  t.commit();
}

////////////////////////////////////////////////////////////////////
//   I/O routines
////////////////////////////////////////////////////////////////////

namespace
{
  namespace syms
  {
    // cset symbols
    symbol const delete_node("delete");
    symbol const rename_node("rename");
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
  }
}

void
print_cset(basic_io::printer & printer,
           cset const & cs)
{
  for (set<file_path>::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::delete_node, *i);
      printer.print_stanza(st);
    }

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::rename_node, i->first);
      st.push_file_pair(syms::to, i->second);
      printer.print_stanza(st);
    }

  for (set<file_path>::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_dir, *i);
      printer.print_stanza(st);
    }

  for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, i->first);
      st.push_binary_pair(syms::content, i->second.inner());
      printer.print_stanza(st);
    }

  for (map<file_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::patch, i->first);
      st.push_binary_pair(syms::from, i->second.first.inner());
      st.push_binary_pair(syms::to, i->second.second.inner());
      printer.print_stanza(st);
    }

  for (set<pair<file_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::clear, i->first);
      st.push_str_pair(syms::attr, i->second());
      printer.print_stanza(st);
    }

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::set, i->first.first);
      st.push_str_pair(syms::attr, i->first.second());
      st.push_str_pair(syms::value, i->second());
      printer.print_stanza(st);
    }
}


static inline void
parse_path(basic_io::parser & parser, file_path & sp)
{
  string s;
  parser.str(s);
  sp = file_path_internal(s);
}

void
parse_cset(basic_io::parser & parser,
           cset & cs)
{
  cs.clear();
  string t1, t2;
  MM(t1);
  MM(t2);
  file_path p1, p2;
  MM(p1);
  MM(p2);

  file_path prev_path;
  MM(prev_path);
  pair<file_path, attr_key> prev_pair;
  MM(prev_pair.first);
  MM(prev_pair.second);

  // we make use of the fact that a valid file_path is never empty
  prev_path.clear();
  while (parser.symp(syms::delete_node))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      safe_insert(cs.nodes_deleted, p1);
    }

  prev_path.clear();
  while (parser.symp(syms::rename_node))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      parser.esym(syms::to);
      parse_path(parser, p2);
      safe_insert(cs.nodes_renamed, make_pair(p1, p2));
    }

  prev_path.clear();
  while (parser.symp(syms::add_dir))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      safe_insert(cs.dirs_added, p1);
    }

  prev_path.clear();
  while (parser.symp(syms::add_file))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      parser.esym(syms::content);
      parser.hex(t1);
      safe_insert(cs.files_added,
                  make_pair(p1, decode_hexenc_as<file_id>(t1, parser.tok.in.made_from)));
    }

  prev_path.clear();
  while (parser.symp(syms::patch))
    {
      parser.sym();
      parse_path(parser, p1);
      I(prev_path.empty() || prev_path < p1);
      prev_path = p1;
      parser.esym(syms::from);
      parser.hex(t1);
      parser.esym(syms::to);
      parser.hex(t2);
      safe_insert(cs.deltas_applied,
                  make_pair(p1, make_pair(decode_hexenc_as<file_id>(t1, parser.tok.in.made_from),
                                          decode_hexenc_as<file_id>(t2, parser.tok.in.made_from))));
    }

  prev_pair.first.clear();
  while (parser.symp(syms::clear))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<file_path, attr_key> new_pair(p1, attr_key(t1, parser.tok.in.made_from));
      I(prev_pair.first.empty() || new_pair > prev_pair);
      prev_pair = new_pair;
      safe_insert(cs.attrs_cleared, new_pair);
    }

  prev_pair.first.clear();
  while (parser.symp(syms::set))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<file_path, attr_key> new_pair(p1, attr_key(t1, parser.tok.in.made_from));
      I(prev_pair.first.empty() || new_pair > prev_pair);
      prev_pair = new_pair;
      parser.esym(syms::value);
      parser.str(t2);
      safe_insert(cs.attrs_set, make_pair(new_pair, attr_value(t2, parser.tok.in.made_from)));
    }
}

void
write_cset(cset const & cs, data & dat)
{
  basic_io::printer pr;
  print_cset(pr, cs);
  dat = data(pr.buf, origin::internal);
}

void
read_cset(data const & dat, cset & cs)
{
  MM(dat);
  MM(cs);
  basic_io::input_source src(dat(), "cset");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  parse_cset(pars, cs);
  I(src.lookahead == EOF);
}

template <> void
dump(cset const & cs, string & out)
{
  data dat;
  write_cset(cs, dat);
  out = dat();
}




// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
