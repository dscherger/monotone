// Copyright 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This file is made available under the GNU GPL v2 or later.

#include "base.hh"

#include <iostream>

#include "app_state.hh"
#include "basic_io.hh"
#include "botan/botan.h"
#include "cmd.hh"
#include "database.hh"
#include "dates.hh"
//#include "editable_policy.hh"
#include "file_io.hh"
#include "keys.hh"
#include "key_store.hh"
//#include "policy.hh"
#include "policies/base_policy.hh"
#include "policies/editable_policy.hh"
#include "policies/policy_branch.hh"
#include "project.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"
#include "vocab_cast.hh"

CMD_GROUP(policy, "policy", "", CMD_REF(__root__),
          N_("Commands that deal with policy branches."),
          "");

using boost::shared_ptr;
using std::string;

inline static external_key_name
key_id_to_external_name(key_id const & ident)
{
  return external_key_name(encode_hexenc(ident.inner()(),
                                         origin::internal),
                           origin::internal);
}

CMD(create_project, "create_project", "", CMD_REF(policy),
    N_("NAME"),
    N_("Bootstrap creation of a new project."),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  key_store keys(app);
  project_t project(db, app.lua, app.opts);

  std::string project_name = idx(args, 0)();
  system_path project_dir = app.opts.conf_dir / "projects";
  system_path project_file = project_dir / path_component(project_name, origin::user);

  require_path_is_directory(app.opts.conf_dir,
                            F("Cannot find configuration directory."),
                            F("Configuration directory is a file."));
  require_path_is_nonexistent(project_file,
                              F("You already have a project with that name."));
  mkdir_p(project_dir);

  cache_user_key(app.opts, app.lua, db, keys, project);


  std::set<external_key_name> signers;
  signers.insert(key_id_to_external_name(keys.signing_key));
  policies::policy_branch br = policies::policy_branch::new_branch(signers);
  policies::editable_policy ep(br.create_initial_revision());
  br.commit(ep, utf8(N_("Create new policy branch")));

  policies::editable_policy bp(project.get_base_policy());

  bp.set_delegation(project_name, policies::delegation(br.get_spec()));

  policies::base_policy::write(bp);
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
  branch_name prefix = typecast_vocab<branch_name>(idx(args, 0));
  branch_name name = prefix;
  name.append(branch_name("__policy__", origin::internal));

  governing_policy_info gov;
  project.find_governing_policy(name(), gov);

  policies::policy_branch parent_branch(gov.governing_policy_parent->get_delegation(gov.delegation_to_governing_policy));

  policies::editable_policy parent(*parent_branch.begin());

  policies::policy_branch new_policy_branch(policy_branch::new_branch());
  policies::policy new_policy(new_policy_branch.create_initial_revision());

  parent.set_delegation(name, new_policy.get_delegation("__policy__"));

  transaction_guard guard(db);
  new_policy_branch.commit(new_policy, utf8(""));
  parent_branch.commit(parent, utf8("Add delegation to new child policy"));
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
  branch_name branch = typecast_vocab<branch_name>(idx(args, 0));

  editable_policy parent;
  branch_name parent_prefix;
  E(project.get_policy_branch_policy_of(branch, parent, parent_prefix),
    origin::user,
    F("Cannot find a parent policy for %s") % branch);
  P(F("Parent policy: %s") % parent_prefix);

  I(branch().find(parent_prefix()) == 0);
  std::string relative_name = branch().substr(parent_prefix().size());
  if (relative_name.size() > 0 && relative_name[0] == '.')
    relative_name.erase(0, 1);
  if (relative_name.empty())
    relative_name = "__main__";

  cache_user_key(app.opts, app.lua, db, keys, project);
  std::set<external_key_name> admin_keys;
  {
    key_identity_info ident;
    ident.id = keys.signing_key;
    project.complete_key_identity(keys, app.lua, ident);
    admin_keys.insert(typecast_vocab<external_key_name>(ident.official_name));
  }


  shared_ptr<editable_policy::branch>
    br = parent.get_branch(relative_name);
  E(!br, origin::user,
    F("A branch %s already exists under policy %s")
    % relative_name % parent_prefix);
  br = parent.get_branch(relative_name, true);
  br->committers = admin_keys;

  project_t p = project_t::empty_project(db);
  parent.commit(p, keys, utf8(N_("Declare new branch")));
}

CMD_FWD_DECL(list);

void list_policy(project_t const & proj, branch_name const & prefix, bool recursive)
{
  std::cout<<prefix<<"\n";
  if (recursive)
    {
      std::set<branch_name> subpolicies;
      proj.get_subpolicies(prefix, subpolicies);
      for (std::set<branch_name>::const_iterator i = subpolicies.begin();
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
      std::set<branch_name> subpolicies;
      project.get_subpolicies(branch_name(), subpolicies);
      for (std::set<branch_name>::const_iterator i = subpolicies.begin();
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
          branch_name const bp = typecast_vocab<branch_name>((*i));
          E(project.policy_exists(bp), origin::user,
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
