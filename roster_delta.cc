#include <set>
#include <map>

#include "roster_delta.hh"
#include "basic_io.hh"
#include "paths.hh"

namespace 
{

  struct roster_delta_t
  {
    typedef std::map<node_id> nodes_deleted_t;
    nodes_deleted_t nodes_deleted;
    typedef std::map<std::pair<node_id, path_component>, node_id> dirs_added_t;
    dirs_added_t dirs_added;
    typedef std::map<std::pair<node_id, path_component>, std::pair<node_id, file_id> > files_added_t;
    files_added_t files_added;
    typedef std::map<node_id, std::pair<node_id, path_component> > nodes_renamed_t;
    nodes_renamed_t nodes_renamed;
    typedef std::map<node_id, file_id> deltas_applied_t;
    deltas_applied_t deltas_applied;
    typedef std::set<std::pair<node_id, attr_key> > attrs_cleared_t;
    attrs_cleared_t attrs_cleared;
    typedef std::set<std::pair<node_id, std::pair<attr_key, std::pair<bool, attr_value> > > attrs_changed_t;
    attrs_changed_t attrs_changed;

    // nodes_deleted are automatically removed from the marking_map; these are
    // all markings that are new or changed
    typedef std::map<node_id, marking_t> markings_changed_t;
    markings_changed_t markings_changed;

    void
    apply(roster_t & roster, marking_map & markings) const;
  };

  void
  roster_delta_t::apply(roster_t & roster, marking_map & markings) const
  {
    // detach everything that should be detached
    for (nodes_deleted_t i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      roster.detach_node(*i);
    for (nodes_renamed_t::const_iterator i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
      roster.detach_node(i->first);
    // delete the delete-able things
    for (nodes_deleted_t::const_iterator i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      roster.drop_detached_node(*i);
    // add the new things
    for (dirs_added_t::const_iterator i = dirs_added.begin(); i != dirs_added.end(); ++i)
      roster.create_dir_node(i->second);
    for (files_added_t::const_iterator i = files_added.begin(); i != files_added.end(); ++i)
      roster.create_file_node(i->second.second, i->second.first);
    // attach everything
    for (dirs_added_t::const_iterator i = dirs_added.begin(); i != dirs_added.end(); ++i)
      roster.attach_node(i->second, i->first.first, i->first.second);
    for (files_added_t::const_iterator i = files_added.begin(); i != files_added.end(); ++i)
      roster.attach_node(i->second.second, i->first.first, i->first.second);
    for (nodes_renamed_t::const_iterator i = nodes_renamed.begin(); i != nodes_renamed.end(); ++i)
      roster.attach_node(i->first, i->second.first, i->second.second);

    // roight, all that tricky tree-rearranging done, just have to do some
    // individual node edits now
    for (deltas_applied_t::const_iterator i = deltas_applied.begin(); i != deltas_applied.end(); ++i)
      roster.set_delta(i->first, i->second);
    for (attrs_cleared_t::const_iterator i = attrs_cleared.begin(); i != attrs_cleared.end(); ++i)
      roster._attr(i->first, i->second.first, i->second.second);
    for (attrs_changed_t::const_iterator i = attrs_changed.begin(); i != attrs_changed.end(); ++i)
      roster.set_attr(i->first, i->second.first, i->second.second);

    // and the markings
    for (nodes_deleted_t::const_iterator i = nodes_deleted.begin(); i != nodes_deleted.end(); ++i)
      safe_erase(markings, *i);
    for (markings_changed_t::const_iterator i = markings_changed.begin(); i != markings_changed.end(); ++i)
      {
        marking_map::iterator j = markings.find(i->first);
        I(j != markings.end());
        (*j) = i->second;
      }
  }

  void
  delta_only_in_to(node_t new_n, roster_delta_t & d)
  {
    node_id nid = new_n->self;
    std::pair<node_id, path_component> new_loc(new_n->parent, new_n->name);

    if (is_dir_t(new_n))
      safe_insert(d.dirs_added, std::make_pair(new_loc, nid));
    else
      {
        file_id const & content = downcast_to_file_t(new_n)->content;
        safe_insert(d.dirs_added, std::make_pair(new_loc,
                                                 std::make_pair(nid, content)));
      }
    for (full_attr_map_t::const_iterator i = new_n->attrs.begin();
         i != new_n->attrs.end(); ++i)
      safe_insert(d.attrs_changed, std::make_pair(nid, *i));
  }

  void
  delta_in_both(node_t old_n, node_t new_n, roster_delta_t & d)
  {
    I(old_n->self == new_n->self);
    node_id nid = old_n->self;
    // rename?
    {
      std::pair<node_id, path_component> old_loc(old_n->parent, old_n->name);
      std::pair<node_id, path_component> new_loc(new_n->parent, new_n->name);
      if (old_loc != new_loc)
        safe_insert(d.nodes_renamed, std::make_pair(nid, new_loc));
    }
    // delta?
    if (is_file_t(old_n))
      {
        file_id const & old_content = downcast_to_file_t(old_n)->content;
        file_id const & new_content = downcast_to_file_t(new_n)->content;
        if (old_content != new_content)
          safe_insert(d.deltas_applied, std::make_pair(nid, new_content));
      }
    // attrs?
    {
      parallel::iter<full_attr_map_t> i(old_n->attrs(), new_n->attrs());
      MM(i);
      while (i.next())
        {
          switch (i.state())
            {
            case parallel::invalid:
              I(false);

            case parallel::in_left:
              safe_insert(d.attrs_cleared, std::make_pair(nid, i.left_key()));
              break;

            case parallel::in_right:
              safe_insert(d.attrs_changed, std::make_pair(nid, i.right_value()));
              break;

            case parallel::in_both:
              if (i.left_data() != i.right_data())
                safe_insert(d.attrs_changed, std::make_pair(nid, i.right_value()));
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
    MM(d);
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
              delta_only_in_to(i.right_data(), d);
              break;
            
            case parallel::in_both:
              // moved/patched/attribute changes
              delta_in_both(i.left_data(), i.right_data(), d);
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
              if (i.left_data() != i.right_data())
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

  void
  push_nid(symbol const & sym, node_id nid, basic_io::stanza & st)
  {
    st.push_str_pair(sym, lexical_cast<std::string>(nid));
  }

  static void
  push_loc(std::pair<node_id, path_component> const & loc,
           basic_io::stanza & st)
  {
    st.push_str_triple(syms::location,
                       lexical_cast<std::string>(loc.first),
                       loc.second());
  }

  void
  print_roster_delta_t(basic_io::printer & printer,
                       basic_delta & d)
  {
    for (nodes_deleted_t i = d.nodes_deleted.begin(); i != d.nodes_deleted.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::deleted, *i, st);
        printer.print_stanza(st);
      }
    for (nodes_renamed_t::const_iterator i = d.nodes_renamed.begin(); i != d.nodes_renamed.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::rename, i->first, st);
        push_loc(i->second, st);
        printer.print_stanza(st);
      }
    for (dirs_added_t::const_iterator i = d.dirs_added.begin(); i != d.dirs_added.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::add_dir, i->second, st);
        push_loc(i->first, st);
        printer.print_stanza(st);
      }
    for (files_added_t::const_iterator i = d.files_added.begin(); i != d.files_added.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::add_file, i->second.first, st);
        push_loc(i->first, st);
        push_hex_pair(syms::content, i->second.second, st);
        printer.print_stanza(st);
      }
    for (deltas_applied_t::const_iterator i = d.deltas_applied.begin(); i != d.deltas_applied.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::delta, i->first, st);
        st.push_hex_pair(syms::content, i->second);
        printer.print_stanza(st);
      }
    for (attrs_cleared_t::const_iterator i = d.attrs_cleared.begin(); i != d.attrs_cleared.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::attr_cleared, i->first, st);
        st.push_str_pair(syms::atr, i->second());
        printer.print_stanza(st);
      }
    for (attrs_changed_t::const_iterator i = d.attrs_changed.begin(); i != d.attrs_changed.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::attr_changed, i->first, st);
        st.push_str_pair(syms::atr, i->second.first());
        st.push_str_triple(syms::value,
                           lexical_cast<std::string>(i->second.second.first),
                           i->second.second.second());
        printer.print_stanza(st);
      }
    for (markings_changed_t::const_iterator i = d.markings_changed.begin(); i != d.markings_changed.end(); ++i)
      {
        basic_io::stanza st;
        push_nid(syms::marking, i->first, st);
        // ...this second argument is a bit odd...
        push_marking(st, !i->second.file_content.empty(), i->second);
        printer.print_stanza(st);
      }
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
            std::pair<node_id, path_component> & loc)
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
    string t1, t2;
    MM(t1);
    MM(t2);
  
    while (parser.symp(syms::deleted))
      {
        parser.sym();
        safe_insert(d.nodes_deleted(parse_nid(parser)));
      }
    while (parser.symp(syms::rename))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        std::pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.nodes_renamed, std::make_pair(nid, loc));
      }
    while (parser.symp(syms::add_dir))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        std::pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.dirs_added, std::make_pair(loc, nid));
      }
    while (parser.symp(syms::add_file))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        std::pair<node_id, path_component> loc;
        parse_loc(parser, loc);
        safe_insert(d.dirs_added, std::make_pair(loc, nid));
        parser.esym(syms::content);
        string s;
        parser.hex(s);
        safe_insert(d.files_added,
                    std::make_pair(loc, std::make_pair(nid, file_id(s))));
      }
    while (parser.symp(syms::delta))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        parser.esym(syms::content);
        string s;
        parser.hex(s);
        safe_insert(d.deltas_applied, std::make_pair(nid, file_id(s)));
      }
    while (parser.symp(syms::attr_cleared))
      {
        node_id nid = parse_nid(parser);
        parser.esym(syms::attr);
        string key;
        parser.str(key);
        safe_insert(d.attrs_cleared, std::make_pair(nid, attr_key(key)));
      }
    while (parser.symp(syms::attr_changed))
      {
        node_id nid = parse_nid(parser);
        parser.esym(syms::attr);
        string key;
        parser.str(key);
        parser.esym(syms::value);
        string value_bool, value_value;
        parser.str(value_bool);
        parser.str(value_value);
        safe_insert(d.attrs_changed,
                    std::make_pair(nid,
                                   std::make_pair(attr_key(key),
                                                  std::make_pair(std::lexical_cast<bool>(value_bool),
                                                                 attr_value(value_value)))));
      }
    while (parser.symp(syms::marking))
      {
        parser.sym();
        node_id nid = parse_nid(parser);
        marking_t m;
        parse_marking(parser, m);
        safe_insert(d.markings_changed, std::make_pair(nid, m));
      }
  }
  
} // end anonymous namespace

void
delta_rosters(roster_t const & from, marking_map const & from_markings,
              roster_t const & to, marking_map const & to_markings,
              roster_delta & del)
{
  roster_delta_t d;
  make_roster_delta_t(from, from_markings, to, to_markings, d);
  basic_io::printer printer;
  print_roster_delta_t(printer, d);
  del = roster_delta(printer.buf);
}

void
apply_roster_delta(roster_delta const & del,
                   roster_t & roster, marking_map & markings)
{
  MM(d);
  MM(roster);
  MM(markings);

  basic_io::input_source src(del.inner()(), "roster_delta");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  roster_delta_t d;
  parse_roster_delta_t(pars, d);
  d.apply(roster, markings);
}
