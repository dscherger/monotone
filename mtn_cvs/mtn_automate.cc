// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtn_automate.hh"
#include <sanity.hh>

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
