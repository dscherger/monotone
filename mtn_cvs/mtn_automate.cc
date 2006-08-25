// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtn_automate.hh"
#include <sanity.hh>
#include <basic_io.hh>

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

std::string mtn_automate::get_sync_info(revision_id const& rid, std::string const& domain)
{ std::vector<std::string> args;
  args.push_back(rid.inner()());
  args.push_back(domain);
  return automate("get_sync_info",args);
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

static void print_cset(basic_io::printer printer, mtn_automate::cset const& cs)
{ typedef std::set<file_path> path_set;
  for (path_set::const_iterator i = cs.deleted.begin();
       i != cs.deleted.end(); ++i)
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

  for (std::map<file_path, file_id>::const_iterator i = cs.added.begin();
       i != cs.added.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::add_file, i->first);
      st.push_hex_pair(syms::content, i->second.inner());
      printer.print_stanza(st);
    }

  for (std::map<file_path, std::pair<file_id, file_id> >::const_iterator i = cs.changed.begin();
       i != cs.changed.end(); ++i)
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

mtn_automate::manifest mtn_automate::get_manifest_of(revision_id const& rid)
{ std::vector<std::string> args(1,rid.inner()());
  std::string aresult=automate("get_manifest_of",args);
  
  basic_io::input_source source(aresult,"automate get_manifest_of result");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser pa(tokenizer);

  manifest result;
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
          result[file_path_internal(pth)]=file_id(content);
        }
      else if (pa.symp(syms::dir))
        {
          pa.sym();
          pa.str(pth);
          result[file_path_internal(pth)] /*=file_id()*/;
        }
      else
        break;
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
