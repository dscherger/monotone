// Copyright 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This file is made available under the GNU GPL v2 or later.

#include "base.hh"

#include <iostream>

#include "app_state.hh"
#include "basic_io.hh"
#include "botan/botan.h"
#include "cmd.hh"
#include "dates.hh"
#include "editable_policy.hh"
#include "file_io.hh"
#include "keys.hh"
#include "key_store.hh"
#include "policy.hh"
#include "project.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"

CMD_GROUP(policy, "policy", "", CMD_REF(__root__),
          N_("Commands that deal with policy branches."),
          "");

using boost::shared_ptr;
using std::string;

CMD(create_project, "create_project", "", CMD_REF(policy),
    N_("NAME"),
    N_("Bootstrap creation of a new project."),
    N_(""),
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  key_store keys(app);

  std::string project_name = idx(args, 0)();
  system_path project_dir = app.opts.conf_dir / "projects";
  system_path project_file = project_dir / path_component(project_name);

  require_path_is_directory(app.opts.conf_dir,
                            F("Cannot find configuration directory."),
                            F("Configuration directory is a file."));
  require_path_is_nonexistent(project_file,
                              F("You already have a project with that name."));
  mkdir_p(project_dir);

  cache_user_key(app.opts, app.lua, db, keys);
  std::set<rsa_keypair_id> admin_keys;
  admin_keys.insert(keys.signing_key);

  std::string policy_uid;
  data policy_spec;
  editable_policy ep(db, admin_keys);
  ep.get_branch("__policy__")->write(policy_spec);
  ep.commit(keys, app.lua, utf8(N_("Create new policy branch")));

  write_data(project_file, policy_spec, project_dir);
  P(F("Wrote project spec to %s") % project_file);
}

CMD(create_subpolicy, "create_subpolicy", "", CMD_REF(policy),
    N_("BRANCH_PREFIX"),
    N_("Create a policy for a new subtree of an existing project."),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);
  branch_prefix prefix(idx(args, 0)());
  branch_name name(prefix() + ".__policy__");

  branch_policy policy_policy;
  branch_prefix parent_prefix;
  E(project.get_policy_branch_policy_of(name, policy_policy, parent_prefix),
    F("Cannot find parent policy for %s") % prefix);
  P(F("Parent policy: %s") % parent_prefix);

  std::string subprefix;
  E(prefix() != parent_prefix(),
    F("A policy for %s already exists.") % prefix);
  I(prefix().find(parent_prefix() + ".") == 0);
  subprefix = prefix().substr(parent_prefix().size()+1);

  cache_user_key(app.opts, app.lua, db, keys);
  std::set<rsa_keypair_id> admin_keys;
  admin_keys.insert(keys.signing_key);


  editable_policy parent(db, policy_policy);
  editable_policy child(db, admin_keys);
  shared_ptr<editable_policy::delegation>
    del = parent.get_delegation(subprefix, true);
  del->uid = child.uid;
  del->committers = admin_keys;

  transaction_guard guard(db);
  child.commit(keys, app.lua, utf8(N_("Create new policy branch")));
  parent.commit(keys, app.lua, utf8(N_("Add new delegation")));
  
  guard.commit();
}

CMD(create_branch, "create_branch", "", CMD_REF(policy),
    N_("NAME"),
    N_("Create a new branch, attached to the nearest subpolicy."),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);
  branch_name branch(idx(args, 0)());

  branch_policy parent_policy;
  branch_prefix parent_prefix;
  E(project.get_policy_branch_policy_of(branch, parent_policy, parent_prefix),
    F("Cannot find a parent policy for %s") % branch);
  P(F("Parent policy: %s") % parent_prefix);

  I(branch().find(parent_prefix()) == 0);
  std::string relative_name = branch().substr(parent_prefix().size());
  if (relative_name.size() > 0 && relative_name[0] == '.')
    relative_name.erase(0, 1);
  if (relative_name.empty())
    relative_name = "__main__";

  cache_user_key(app.opts, app.lua, db, keys);
  std::set<rsa_keypair_id> admin_keys;
  admin_keys.insert(keys.signing_key);


  editable_policy parent(db, parent_policy);
  shared_ptr<editable_policy::branch>
    br = parent.get_branch(relative_name);
  N(!br,  F("A branch %s already exists under policy %s")
    % relative_name % parent_prefix);
  br = parent.get_branch(relative_name, true);

  parent.commit(keys, app.lua, utf8(N_("Declare new branch")));
}

CMD_FWD_DECL(list);

void list_policy(project_t const & proj, branch_prefix const & prefix, bool recursive)
{
  std::cout<<prefix<<"\n";
  if (recursive)
    {
      std::set<branch_prefix> subpolicies;
      proj.get_subpolicies(prefix, subpolicies);
      for (std::set<branch_prefix>::const_iterator i = subpolicies.begin();
           i != subpolicies.end(); ++i)
        {
          list_policy(proj, *i, recursive);
        }
    }
}

CMD(policies, "policies", "", CMD_REF(list),
    N_("PREFIX [...]"),
    N_("List policies"),
    N_("List subpolicies of the given policy prefixes, or toplevel projects\n"
       "if no arguments are provided."),
    options::opts::recursive)
{
  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);

  if (args.empty())
    {
      std::set<branch_prefix> subpolicies;
      project.get_subpolicies(branch_prefix(), subpolicies);
      for (std::set<branch_prefix>::const_iterator i = subpolicies.begin();
           i != subpolicies.end(); ++i)
        {
          list_policy(project, *i, app.opts.recursive);
        }
    }
  else
    {
      for (args_vector::const_iterator i = args.begin();
           i != args.end(); ++i)
        {
          branch_name const bn((*i)());
          branch_prefix const bp((*i)());
          N(project.policy_exists(bp),
            F("Policy %s does not exist.") % *i);
          list_policy(project, bp, app.opts.recursive);
        }
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
