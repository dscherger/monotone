// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "../base.hh"
#include "mtn_automate.hh"
#include <sanity.hh>
#include <basic_io.hh>
#include <constants.hh>
#include <safe_map.hh>
#include <fstream>
#include <set>
#include <transforms.hh>

using std::string;
using std::make_pair;
using std::pair;

void mtn_automate::check_interface_revision(std::string const& minimum)
{ std::string present=automate("interface_version");
  E(present>=minimum, origin::user,
      F("your monotone automate interface revision %s does not match the "
          "requirements %s") % present % minimum);
}

std::string mtn_automate::get_option(std::string const& name)
{
  return automate("get_option",std::vector<std::string>(1,name));
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
parse_path(basic_io::parser & parser, file_path & sp)
{
  std::string s;
  parser.str(s);
  sp=file_path_internal(s);
}

file_id mtn_automate::put_file(file_data const& d, file_id const& base)
{ std::vector<std::string> args;
  if (!null_id(base.inner())) 
    args.push_back(encode_hexenc(base.inner()(),origin::internal));
  args.push_back(d.inner()());
  return file_id(decode_hexenc(automate("put_file",args).substr(0,constants::idlen),origin::network),origin::network);
}

file_data mtn_automate::get_file(file_id const& fid)
{ std::vector<std::string> args;
  args.push_back(encode_hexenc(fid.inner()(),origin::internal));
  return file_data(automate("get_file",args),origin::network);
}

#include <piece_table.hh>

std::vector<revision_id> mtn_automate::get_revision_children(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(encode_hexenc(rid.inner()(),origin::internal));
  std::string children=automate("children",args);
  std::vector<revision_id> result;
  piece::piece_table lines;
  piece::index_deltatext(children,lines);
  result.reserve(children.size());
  for (piece::piece_table::const_iterator p=lines.begin();p!=lines.end();++p)
  { // L(FL("child '%s'") % (**p).substr(0,constants::idlen));
    result.push_back(revision_id(decode_hexenc((**p).substr(0,constants::idlen),origin::network),origin::network));
  }
  piece::reset();
  return result;
}

std::vector<revision_id> mtn_automate::get_revision_parents(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(encode_hexenc(rid.inner()(),origin::internal));
  std::string children=automate("parents",args);
  std::vector<revision_id> result;
  piece::piece_table lines;
  piece::index_deltatext(children,lines);
  result.reserve(children.size());
  for (piece::piece_table::const_iterator p=lines.begin();p!=lines.end();++p)
    result.push_back(revision_id(decode_hexenc((**p).substr(0,constants::idlen),origin::network),origin::network));
  piece::reset();
  return result;
}

std::vector<revision_id> mtn_automate::heads(std::string const& branch)
{ std::vector<std::string> args(1,branch);
  std::string heads=automate("heads",args);
  std::vector<revision_id> result;
  piece::piece_table lines;
  piece::index_deltatext(heads,lines);
  result.reserve(heads.size());
  for (piece::piece_table::const_iterator p=lines.begin();p!=lines.end();++p)
    result.push_back(revision_id(decode_hexenc((**p).substr(0,constants::idlen),origin::internal),origin::network));
  piece::reset();
  return result;
}

static void print_cset(basic_io::printer &printer, mtn_automate::cset const& cs)
{ for (mtn_automate::path_set::const_iterator i = cs.nodes_deleted.begin();
       i != cs.nodes_deleted.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::delete_node, *i);
      printer.print_stanza(st);
    }

  for (std::map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::rename_node, file_path(i->first));
      st.push_file_pair(syms::to, file_path(i->second));
      printer.print_stanza(st);
    }

  for (mtn_automate::path_set::const_iterator i = cs.dirs_added.begin();
       i != cs.dirs_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_dir, *i);
      printer.print_stanza(st);
    }

  for (std::map<file_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, i->first);
      st.push_hex_pair(syms::content, hexenc<id>(encode_hexenc(i->second.inner()(),origin::internal),origin::internal));
      printer.print_stanza(st);
    }

  for (std::map<file_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::patch, i->first);
      st.push_hex_pair(syms::from, hexenc<id>(encode_hexenc(i->second.first.inner()(),origin::internal),origin::internal));
      st.push_hex_pair(syms::to, hexenc<id>(encode_hexenc(i->second.second.inner()(),origin::internal),origin::internal));
      printer.print_stanza(st);
    }

  for (std::set<std::pair<file_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::clear, file_path(i->first));
      st.push_str_pair(syms::attr, i->second());
      printer.print_stanza(st);
    }

  for (std::map<std::pair<file_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::set, file_path(i->first.first));
      st.push_str_pair(syms::attr, i->first.second());
      st.push_str_pair(syms::value, i->second());
      printer.print_stanza(st);
    }
}

revision_id mtn_automate::put_revision(revision_id const& parent, cset const& changes)
{ basic_io::printer printer;
  basic_io::stanza format_stanza;
  format_stanza.push_str_pair(syms::format_version, "1");
  printer.print_stanza(format_stanza);
      
  basic_io::stanza manifest_stanza;
  manifest_stanza.push_hex_pair(syms::new_manifest, hexenc<id>("0000000000000000000000000000000000000001"));
  printer.print_stanza(manifest_stanza);

// changeset stanza  
  basic_io::stanza st;
  st.push_hex_pair(syms::old_revision, hexenc<id>(encode_hexenc(parent.inner()(),origin::internal),origin::internal));
  printer.print_stanza(st);
  print_cset(printer, changes);
  std::vector<std::string> args(1,printer.buf);
  return revision_id(decode_hexenc(automate("put_revision",args).substr(0,constants::idlen),origin::network),origin::network);
}

mtn_automate::manifest_map mtn_automate::get_manifest_of(revision_id const& rid)
{ std::vector<std::string> args(1,encode_hexenc(rid.inner()(),origin::internal));
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
          result[file_path_internal(pth)].first=file_id(decode_hexenc(content,origin::network),origin::network);
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
                    make_pair(attr_key(k,origin::network),attr_value(v,origin::network)));
        }
        
#if 0
      // Dormant attrs ??
      while(pa.symp(basic_io::syms::dormant_attr))
        {
          pa.sym();
          string k;
          pa.str(k);
          safe_insert(result[file_path_internal(pth)].second, 
                    make_pair(attr_key(k,origin::network),attr_value()));
        }
#endif
    }
  return result;
}

void mtn_automate::cert_revision(revision_id const& rid, std::string const& name, std::string const& value)
{ std::vector<std::string> args;
  args.push_back(encode_hexenc(rid.inner()(),origin::internal));
  args.push_back(name);
  args.push_back(value);
  automate("cert",args);
}

std::vector<mtn_automate::certificate> mtn_automate::get_revision_certs(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(encode_hexenc(rid.inner()(),origin::internal));
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

std::vector<mtn_automate::certificate> mtn_automate::get_revision_certs(revision_id const& rid, cert_name const& name)
{ std::vector<mtn_automate::certificate> result=get_revision_certs(rid);
  for (std::vector<mtn_automate::certificate>::iterator i=result.begin();i!=result.end();)
  { if (i->name!=name()) i=result.erase(i);
    else ++i;
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
  file_path p1, p2;
  MM(p1);
  MM(p2);

//  file_path prev_path;
//  MM(prev_path);
//  pair<file_path, attr_key> prev_pair;
//  MM(prev_pair.first);
//  MM(prev_pair.second);

  // we make use of the fact that a valid file_path is never empty
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
      safe_insert(cs.files_added, make_pair(p1, file_id(decode_hexenc(t1,origin::network),origin::network)));
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
                  make_pair(p1, make_pair(file_id(decode_hexenc(t1,origin::network),origin::network), 
                                file_id(decode_hexenc(t2,origin::network),origin::network))));
    }

//  prev_pair.first.clear();
  while (parser.symp(syms::clear))
    {
      parser.sym();
      parse_path(parser, p1);
      parser.esym(syms::attr);
      parser.str(t1);
      pair<file_path, attr_key> new_pair(p1, attr_key(t1,origin::network));
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
      pair<file_path, attr_key> new_pair(p1, attr_key(t1,origin::network));
//      I(prev_pair.first.empty() || new_pair > prev_pair);
//      prev_pair = new_pair;
      parser.esym(syms::value);
      parser.str(t2);
      safe_insert(cs.attrs_set, make_pair(new_pair, attr_value(t2,origin::network)));
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
  old_rev = revision_id(decode_hexenc(tmp,origin::network),origin::network);

  parse_cset(parser, *cs);

  es.insert(make_pair(old_rev, cs));
}

mtn_automate::revision_t mtn_automate::get_revision(revision_id const& rid)
{ std::vector<std::string> args;
  args.push_back(encode_hexenc(rid.inner()(),origin::internal));
  std::string aresult=automate("get_revision",args);
  
  basic_io::input_source source(aresult,"automate get_revision result");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser parser(tokenizer);

  revision_t result;

// that's from parse_revision
  string tmp;
  parser.esym(syms::format_version);
  parser.str(tmp);
  E(tmp == "1", origin::workspace,
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

#if 0
template <> void
dump(file_path const& fp, string & out)
{ out=fp.as_internal();
}
#endif

static std::string print_sync_info(mtn_automate::sync_map_t const& data)
{ basic_io::printer printer;
  for (mtn_automate::sync_map_t::const_iterator i = data.begin(); i != data.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::set, file_path(i->first.first));
      st.push_str_pair(syms::attr, i->first.second());
      st.push_str_pair(syms::value, i->second());
      printer.print_stanza(st);
    }
  return printer.buf;
}

// needed by find_newest_sync: check whether a revision has up to date synch information
static const char *const sync_prefix="x-sync-attr-";
typedef std::map<std::pair<file_path, attr_key>, attr_value> sync_map_t;

static bool begins_with(const std::string &s, const std::string &sub)
{ std::string::size_type len=sub.size();
  if (s.size()<len) return false;
  return !s.compare(0,len,sub);
}

bool mtn_automate::is_synchronized(revision_id const& rid, 
                      revision_t const& rev, std::string const& domain)
{
  std::string prefix=domain+":";
  // merge nodes should never have up to date sync attributes
  if (rev.edges.size()==1)
  {
    L(FL("is_synch: rev %s testing changeset\n") % rid);
    boost::shared_ptr<mtn_automate::cset> cs=rev.edges.begin()->second;
    for (std::map<std::pair<file_path, attr_key>, attr_value>::const_iterator i=cs->attrs_set.begin();
          i!=cs->attrs_set.end();++i)
    { if (begins_with(i->first.second(),prefix)) // TODO: omit some attribute changes (repository, keyword, directory)
        return true;
    }
  }
  
  // look for a certificate
  std::vector<certificate> certs;
  certs=get_revision_certs(rid,cert_name(sync_prefix+domain,origin::internal));
  return !certs.empty();
}

// Name: find_newest_sync
// Arguments:
//   sync-domain
//   branch (optional)
// Added in: 3.2
// Purpose:
//   Get the newest revision which has a sync certificate
//   (or changed sync attributes)
// Output format:
//   revision ID
// Error conditions:
//   a runtime exception is thrown if no synchronized revisions are found 
//   in this domain
revision_id mtn_automate::find_newest_sync(std::string const& domain, std::string const& branch)
{ /* if workspace exists use it to determine branch (and starting revision?)
     traverse tree upwards to find a synced revision, 
     then traverse tree downwards to find newest revision 
     
     this assumes a linear and connected synch graph (which is true for CVS,
       but might not appropriate for different RCSs)
   */

  std::vector<revision_id> heads;
  heads=mtn_automate::heads(branch);
  revision_t rev;
  revision_id rid;
  
  while (!heads.empty())
  {
    rid = *heads.begin();
    L(FL("find_newest_sync: testing node %s") % rid);
    rev=get_revision(rid);
    heads.erase(heads.begin());
    // is there a more efficient way than to create a revision_t object?
    if (is_synchronized(rid,rev,domain))
      break;
    for (edge_map::const_iterator e = rev.edges.begin();
                     e != rev.edges.end(); ++e)
    { 
      if (!null_id(e->first))
        heads.push_back(e->first);
    }
    E(!heads.empty(), origin::user, F("no synchronized revision found in branch %s for domain %s")
        % branch % domain);
  }

  std::vector<revision_id> children;
continue_outer:
  if (null_id(rid.inner())) return rid;
  L(FL("find_newest_sync: testing children of %s") % rid);
  children=get_revision_children(rid);
  for (std::vector<revision_id>::const_iterator i=children.begin(); 
          i!=children.end(); ++i)
  {
    rev=get_revision(*i);
    if (is_synchronized(*i,rev,domain))
    { 
      rid=*i;
      goto continue_outer;
    }
  }
  return rid;
}

static void parse_attributes(std::string const& in, sync_map_t& result)
{
  basic_io::input_source source(in,"parse_attributes");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser parser(tokenizer);
  
  std::string t1, t2;
  file_path p1;
  while (parser.symp(syms::clear))
  {
    parser.sym();
    parse_path(parser, p1);
    parser.esym(syms::attr);
    parser.str(t1);
    pair<file_path, attr_key> new_pair(p1, attr_key(t1,origin::network));
    safe_erase(result, new_pair);
  }
  while (parser.symp(syms::set))
  { 
    parser.sym();
    parse_path(parser, p1);
    parser.esym(syms::attr);
    parser.str(t1);
    pair<file_path, attr_key> new_pair(p1, attr_key(t1,origin::network));
    parser.esym(syms::value);
    parser.str(t2);
    safe_insert(result, make_pair(new_pair, attr_value(t2,origin::network)));
  }
}

sync_map_t mtn_automate::get_sync_info(revision_id const& rid, string const& domain, int &depth)
{
  /* sync information is initially coded in DOMAIN: prefixed attributes
     if information needs to be changed after commit then it gets 
     (base_revision_id+xdiff).gz encoded in certificates
     
     SPECIAL CASE of no parent: certificate is (40*' '+plain_data).gz encoded
   */
  sync_map_t result;
  
  L(FL("get_sync_info: checking revision certificates %s") % rid);
  std::vector<certificate> certs;
  certs=get_revision_certs(rid,cert_name(sync_prefix+domain,origin::internal));
  I(certs.size()<=1); // FIXME: what to do with multiple certs ...
  if (certs.size()==1) 
  { std::string decomp_cert_val=xform<Botan::Gzip_Decompression>(idx(certs,0).value,origin::network);
    I(decomp_cert_val.size()>constants::idlen+1);
    I(decomp_cert_val[constants::idlen]=='\n');
    if (decomp_cert_val[0]!=' ')
    { revision_id old_rid=revision_id(decomp_cert_val.substr(0,constants::idlen),origin::network);
      result=get_sync_info(old_rid,domain,depth);
      ++depth;
    }
    else depth=0;
    parse_attributes(decomp_cert_val.substr(constants::idlen+1),result);
    return result;
  }
  
  revision_t rev=get_revision(rid);
  if (rev.edges.size()==1)
  { 
    L(FL("get_sync_info: checking revision attributes %s") % rid);
    manifest_map m=get_manifest_of(rid);
    std::string prefix=domain+":";
    for (manifest_map::const_iterator i = m.begin();
       i != m.end(); ++i)
    {
      for (attr_map_t::const_iterator j = i->second.second.begin();
           j != i->second.second.end(); ++j)
      {
        if (begins_with(j->first(),prefix))
        { 
          result[std::make_pair(i->first,j->first)]=j->second;
          // else W(F("undefined value of %s %s\n") % sp % j->first());
        }
      }
    }
    depth=0;
  }
  E(!result.empty(), origin::user, F("no sync cerficate found in revision %s for domain %s")
        % rid % domain);
  return result;
}

// Name: get_sync_info
// Arguments:
//   revision
//   sync-domain
// Purpose:
//   Get the sync information for a given revision
mtn_automate::sync_map_t mtn_automate::get_sync_info(revision_id const& rid, std::string const& domain)
{ 
  int dummy=0;
  return get_sync_info(rid,domain,dummy);
}

// Name: put_sync_info
// Arguments:
//   revision
//   sync-domain
//   data
// Purpose:
//   Set the sync information for a given revision
void mtn_automate::put_sync_info(revision_id const& rid, std::string const& domain, sync_map_t const& newinfo)
{ 
  revision_t rev=get_revision(rid);
  
  static const int max_indirection_nest=30;
  std::string new_data=print_sync_info(newinfo);

  for (edge_map::const_iterator e = rev.edges.begin();
                     e != rev.edges.end(); ++e)
  { 
    if (null_id(e->first)) continue;
    try
    {
      int depth=0; 
      sync_map_t oldinfo=get_sync_info(e->first,domain,depth);
      if (depth>=max_indirection_nest) continue; // do not nest deeper
      
      basic_io::printer printer;
      for (sync_map_t::const_iterator o=oldinfo.begin(),n=newinfo.begin();
            o!=oldinfo.end() && n!=newinfo.end();)
      { if (n==newinfo.end() || o->first<n->first
            /*|| (o->first==n->first && o->second!=n->second)*/)
        { basic_io::stanza st;
          st.push_file_pair(syms::clear, file_path(o->first.first));
          st.push_str_pair(syms::attr, encode_hexenc(o->first.second(),origin::internal));
          printer.print_stanza(st);
          if (o->first==n->first) ++n;
          ++o;
        }
        else ++n;
      }
      for (sync_map_t::const_iterator o=oldinfo.begin(),n=newinfo.begin();
            o!=oldinfo.end() && n!=newinfo.end();)
      { if (o==oldinfo.end() || o->first>n->first
            || (o->first==n->first && o->second!=n->second))
        { basic_io::stanza st;
          st.push_file_pair(syms::set, file_path(n->first.first));
          st.push_str_pair(syms::attr, encode_hexenc(n->first.second(),origin::internal));
          st.push_str_pair(syms::value, encode_hexenc(n->second(),origin::internal));
          printer.print_stanza(st);
          if (o->first==n->first) ++o;
          ++n;
        }
        else ++o;
      }
      if (printer.buf.size()>=new_data.size()) continue; // look for a shorter form
      
      I(e->first.inner()().size()==constants::idlen);
      std::string cv=xform<Botan::Gzip_Compression>(encode_hexenc(e->first.inner()(),origin::internal)+"\n"+printer.buf,origin::internal);
      cert_revision(rid,sync_prefix+domain,cv);
      L(FL("sync info encoded as delta from %s") % e->first);
      return;
    }
    catch (recoverable_failure &er) {}
    catch (unrecoverable_failure &er) {}
    catch (std::runtime_error &er) {}
  }
  std::string cv=xform<Botan::Gzip_Compression>(string(constants::idlen,' ')+"\n"+new_data,origin::internal);
  cert_revision(rid,sync_prefix+domain,cv);
  L(FL("sync info attached to %s") % rid);
}

