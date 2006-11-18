// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtn_automate.hh"
#include <sanity.hh>
#include <basic_io.hh>
#include <constants.hh>
#include <safe_map.hh>

using std::string;
using std::make_pair;
using std::pair;

void mtn_automate::check_interface_revision(std::string const& minimum)
{ std::string present=automate("interface_version");
  N(present>=minimum,
      F("your monotone automate interface revision %s does not match the "
          "requirements %s") % present % minimum);
}

revision_id mtn_automate::find_newest_sync(std::string const& domain, std::string const& branch)
{ std::vector<std::string> args;
  args.push_back(domain);
  if (!branch.empty()) args.push_back(branch);
  std::string result=automate("find_newest_sync",args);
  return revision_id(result);
}

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
    
    // revision
    symbol const old_revision("old_revision");
    
    // roster symbols
    symbol const format_version("format_version");
//    symbol const old_revision("old_revision");
    symbol const new_manifest("new_manifest");
    
    symbol const dir("dir");
    symbol const file("file");
//    symbol const content("content");
//    symbol const attr("attr");

    // cmd_list symbols
    symbol const key("key");
    symbol const signature("signature");
    symbol const name("name");
//    symbol const value("value");
    symbol const trust("trust");
  }
}

static inline void
parse_path(basic_io::parser & parser, split_path & sp)
{
  std::string s;
  parser.str(s);
  file_path_internal(s).split(sp);
}

mtn_automate::sync_map_t mtn_automate::get_sync_info(revision_id const& rid, std::string const& domain)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  args.push_back(domain);
  std::string aresult=automate("get_sync_info",args);
  
  basic_io::input_source source(aresult,"automate get_sync_info result");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser parser(tokenizer);
  
  std::string t1,t2;
  split_path p1;
  sync_map_t result;
  while (parser.symp(syms::set))
  { 
    parser.sym();
    parse_path(parser, p1);
    parser.esym(syms::attr);
    parser.str(t1);
    pair<split_path, attr_key> new_pair(p1, t1);
    parser.esym(syms::value);
    parser.str(t2);
    safe_insert(result, make_pair(new_pair, attr_value(t2)));
  }
  return result;
}

file_id mtn_automate::put_file(data const& d, file_id const& base)
{ std::vector<std::string> args;
  if (!null_id(base.inner())) args.push_back(base.inner()());
  args.push_back(d());
  return file_id(automate("put_file",args));
}

std::string mtn_automate::get_file(file_id const& fid)
{ std::vector<std::string> args;
  args.push_back(fid.inner()());
  return automate("get_file",args);
}

#include <piece_table.hh>

std::vector<revision_id> mtn_automate::get_revision_children(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  std::string children=automate("children",args);
  std::vector<revision_id> result;
  piece::piece_table lines;
  piece::index_deltatext(children,lines);
  result.reserve(children.size());
  for (piece::piece_table::const_iterator p=lines.begin();p!=lines.end();++p)
  { // L(FL("child '%s'") % (**p).substr(0,constants::idlen));
    result.push_back(revision_id((**p).substr(0,constants::idlen)));
  }
  piece::reset();
  return result;
}

std::vector<revision_id> mtn_automate::get_revision_parents(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  std::string children=automate("parents",args);
  std::vector<revision_id> result;
  piece::piece_table lines;
  piece::index_deltatext(children,lines);
  result.reserve(children.size());
  for (piece::piece_table::const_iterator p=lines.begin();p!=lines.end();++p)
    result.push_back(revision_id((**p).substr(0,constants::idlen)));
  piece::reset();
  return result;
}

static void print_cset(basic_io::printer &printer, mtn_automate::cset const& cs)
{ for (path_set::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::delete_node, *i);
      printer.print_stanza(st);
    }

  for (path_set::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_dir, *i);
      printer.print_stanza(st);
    }

  for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, i->first);
      st.push_hex_pair(syms::content, i->second.inner());
      printer.print_stanza(st);
    }

  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::patch, i->first);
      st.push_hex_pair(syms::from, i->second.first.inner());
      st.push_hex_pair(syms::to, i->second.second.inner());
      printer.print_stanza(st);
    }
}

revision_id mtn_automate::put_revision(revision_id const& parent, cset const& changes)
{ basic_io::printer printer;
  basic_io::stanza st;
  st.push_hex_pair(syms::old_revision, parent.inner());
  printer.print_stanza(st);
  print_cset(printer, changes);
  std::vector<std::string> args(1,printer.buf);
  return revision_id(automate("put_revision",args));
}

mtn_automate::manifest_map mtn_automate::get_manifest_of(revision_id const& rid)
{ std::vector<std::string> args(1,rid.inner()());
  std::string aresult=automate("get_manifest_of",args);
  
  basic_io::input_source source(aresult,"automate get_manifest_of result");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser pa(tokenizer);

  manifest_map result;
  // like roster_t::parse_from
  {
    pa.esym(syms::format_version);
    std::string vers;
    pa.str(vers);
    I(vers == "1");
  }

  while(pa.symp())
    {
      std::string pth, ident, rev;

      if (pa.symp(syms::file))
        {
          std::string content;
          pa.sym();
          pa.str(pth);
          pa.esym(syms::content);
          pa.hex(content);
          result[file_path_internal(pth)].first=file_id(content);
        }
      else if (pa.symp(syms::dir))
        {
          pa.sym();
          pa.str(pth);
          result[file_path_internal(pth)] /*=file_id()*/;
        }
      else
        break;
      
      // Non-dormant attrs
      while(pa.symp(basic_io::syms::attr))
        {
          pa.sym();
          std::string k, v;
          pa.str(k);
          pa.str(v);
          safe_insert(result[file_path_internal(pth)].second, 
                    make_pair(attr_key(k),attr_value(v)));
        }
        
      // Dormant attrs
      while(pa.symp(basic_io::syms::dormant_attr))
        {
          pa.sym();
          string k;
          pa.str(k);
          safe_insert(result[file_path_internal(pth)].second, 
                    make_pair(attr_key(k),attr_value()));
        }
    }
  return result;
}

void mtn_automate::cert_revision(revision_id const& rid, std::string const& name, std::string const& value)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  args.push_back(name);
  args.push_back(value);
  automate("cert",args);
}

std::vector<mtn_automate::certificate> mtn_automate::get_revision_certs(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  std::string aresult=automate("certs",args);
  
  basic_io::input_source source(aresult,"automate get_revision_certs result");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser pa(tokenizer);
  
  std::vector<certificate> result;
  
  while (pa.symp())
  { certificate cert;
  
    I(pa.symp(syms::key));
    pa.sym();
    pa.str(cert.key);
  
    I(pa.symp(syms::signature));
    pa.sym();
    std::string sign;
    pa.str(sign);
    if (sign=="ok") cert.signature=certificate::ok;
    else if (sign=="bad") cert.signature=certificate::bad;
    else cert.signature=certificate::unknown;

    I(pa.symp(syms::name));
    pa.sym();
    pa.str(cert.name);

    I(pa.symp(syms::value));
    pa.sym();
    pa.str(cert.value);

    I(pa.symp(syms::trust));
    pa.sym();
    std::string trust;
    pa.str(trust);
    cert.trusted= trust=="trusted";
    
    result.push_back(cert);
  }
  return result;
}

static void
parse_cset(basic_io::parser & parser,
           mtn_automate::cset & cs)
{
//  cs.clear();
  string t1, t2;
  MM(t1);
  MM(t2);
  split_path p1, p2;
  MM(p1);
  MM(p2);

//  split_path prev_path;
//  MM(prev_path);
//  pair<split_path, attr_key> prev_pair;
//  MM(prev_pair.first);
//  MM(prev_pair.second);

  // we make use of the fact that a valid split_path is never empty
//  prev_path.clear();
  while (parser.symp(syms::delete_node))
    {
      parser.sym();
      parse_path(parser, p1);
//      I(prev_path.empty() || p1 > prev_path);
//      prev_path = p1;
      safe_insert(cs.nodes_deleted, p1);
    }

//  prev_path.clear();
  while (parser.symp(syms::rename_node))
    {
      parser.sym();
      parse_path(parser, p1);
//      I(prev_path.empty() || p1 > prev_path);
//      prev_path = p1;
      parser.esym(syms::to);
      parse_path(parser, p2);
      safe_insert(cs.nodes_renamed, make_pair(p1, p2));
    }

//  prev_path.clear();
  while (parser.symp(syms::add_dir))
    {
      parser.sym();
      parse_path(parser, p1);
//      I(prev_path.empty() || p1 > prev_path);
//      prev_path = p1;
      safe_insert(cs.dirs_added, p1);
    }

//  prev_path.clear();
  while (parser.symp(syms::add_file))
    {
      parser.sym();
      parse_path(parser, p1);
//      I(prev_path.empty() || p1 > prev_path);
//      prev_path = p1;
      parser.esym(syms::content);
      parser.hex(t1);
      safe_insert(cs.files_added, make_pair(p1, file_id(t1)));
    }

//  prev_path.clear();
  while (parser.symp(syms::patch))
    {
      parser.sym();
      parse_path(parser, p1);
//      I(prev_path.empty() || p1 > prev_path);
//      prev_path = p1;
      parser.esym(syms::from);
      parser.hex(t1);
      parser.esym(syms::to);
      parser.hex(t2);
      safe_insert(cs.deltas_applied,
                  make_pair(p1, make_pair(file_id(t1), file_id(t2))));
    }

//  prev_pair.first.clear();
  while (parser.symp(syms::clear))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<split_path, attr_key> new_pair(p1, t1);
//      I(prev_pair.first.empty() || new_pair > prev_pair);
//      prev_pair = new_pair;
      safe_insert(cs.attrs_cleared, new_pair);
    }

//  prev_pair.first.clear();
  while (parser.symp(syms::set))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<split_path, attr_key> new_pair(p1, t1);
//      I(prev_pair.first.empty() || new_pair > prev_pair);
//      prev_pair = new_pair;
      parser.esym(syms::value);
      parser.str(t2);
      safe_insert(cs.attrs_set, make_pair(new_pair, attr_value(t2)));
    }
}

static void
parse_edge(basic_io::parser & parser,
           mtn_automate::edge_map & es)
{
  boost::shared_ptr<mtn_automate::cset> cs(new mtn_automate::cset());
//  MM(cs);
  revision_id old_rev;
  string tmp;

  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = revision_id(tmp);

  parse_cset(parser, *cs);

  es.insert(make_pair(old_rev, cs));
}

mtn_automate::revision_t mtn_automate::get_revision(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  std::string aresult=automate("get_revision",args);
  
  basic_io::input_source source(aresult,"automate get_revision result");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser parser(tokenizer);

  revision_t result;

// that's from parse_revision
  string tmp;
  parser.esym(syms::format_version);
  parser.str(tmp);
  E(tmp == "1",
    F("encountered a revision with unknown format, version '%s'\n"
      "I only know how to understand the version '1' format\n"
      "a newer version of mtn_cvs is required to complete this operation")
    % tmp);
  parser.esym(syms::new_manifest);
  parser.hex(tmp);
//  rev.new_manifest = manifest_id(tmp);
  while (parser.symp(syms::old_revision))
    parse_edge(parser, result.edges);

  return result;
}

template <> void
dump(file_path const& fp, string & out)
{ out=fp.as_internal();
}

void mtn_automate::put_sync_info(revision_id const& rid, std::string const& domain, sync_map_t const& data)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  args.push_back(domain);
  basic_io::printer printer;
  for (sync_map_t::const_iterator i = data.begin(); i != data.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::set, file_path(i->first.first));
      st.push_str_pair(syms::attr, i->first.second());
      st.push_str_pair(syms::value, i->second());
      printer.print_stanza(st);
    }
  args.push_back(printer.buf);
  automate("put_sync_info",args);
}
