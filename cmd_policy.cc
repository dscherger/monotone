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

namespace basic_io
{
  namespace syms
  {
    symbol const branch_uid("branch_uid");
    symbol const committer("committer");
  }
}

namespace {
  std::string
  generate_uid()
  {
    // FIXME: I'm sure there's a better way to do this.
    std::string when = date_t::now().as_iso_8601_extended();
    char buf[20];
    Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte*>(buf), 20);
    return when + "--" + encode_hexenc(std::string(buf, 20));
  }

  void
  write_branch_policy(data & dat,
                      std::string const & branch_uid,
                      std::set<rsa_keypair_id> const & committers)
  {
    basic_io::printer printer;
    basic_io::stanza st;
    st.push_str_pair(basic_io::syms::branch_uid, branch_uid);
    for (std::set<rsa_keypair_id>::const_iterator i = committers.begin();
	 i != committers.end(); ++i)
      {
        st.push_str_pair(basic_io::syms::committer, (*i)());
      }
    printer.print_stanza(st);
    dat = data(printer.buf);
  }
  void
  write_branch_policy(data & dat,
                      std::string const & branch_uid,
                      rsa_keypair_id const & committer)
  {
    std::set<rsa_keypair_id> committers;
    committers.insert(committer);
    write_branch_policy(dat, branch_uid, committers);
  }

  // Generate a revision with a single file, branches/__policy__,
  // and put it in a new branch which is referenced by that file.
  void
  create_policy_branch(database & db, key_store & keys,
                       lua_hooks & lua, options & opts,
                       branch_prefix const & policy_name,
                       std::set<rsa_keypair_id> const & administrators,
                       std::string & policy_uid, data & spec)
  {
    cache_user_key(opts, lua, db, keys);

    policy_uid = generate_uid();
    transaction_guard guard(db);


    // spec file
    file_id spec_id;
    write_branch_policy(spec, policy_uid, administrators);
    file_data spec_data(spec);
    calculate_ident(spec_data, spec_id);

    cset cs;
    cs.dirs_added.insert(file_path_internal(""));
    cs.dirs_added.insert(file_path_internal("branches"));
    cs.files_added.insert(std::make_pair(file_path_internal("branches/__policy__"),
                                         spec_id));
    roster_t old_roster;
    revision_t rev;
    make_revision(revision_id(), old_roster, cs, rev);
    revision_id rev_id;
    calculate_ident(rev, rev_id);
    revision_data rdat;
    write_revision(rev, rdat);


    // write to the db
    if (!db.file_version_exists(spec_id))
      {
        db.put_file(spec_id, spec_data);
      }
    db.put_revision(rev_id, rdat);


    // add certs
    // Do not use project_t::put_standard_certs here, we don't want the
    // branch name to be translated!
    date_t date;
    if (opts.date_given)
      date = opts.date;
    else
      date = date_t::now();

    std::string author = opts.author();
    if (author.empty())
      {
        if (!lua.hook_get_author(branch_name(policy_name() + ".__policy__"),
                                 keys.signing_key, author))
          author = keys.signing_key();
      }
    utf8 changelog(N_("Create new policy branch."));

    cert_revision_in_branch(db, keys, rev_id, branch_uid(policy_uid));
    cert_revision_changelog(db, keys, rev_id, changelog);
    cert_revision_date_time(db, keys, rev_id, date);
    cert_revision_author(db, keys, rev_id, author);


    guard.commit();
    P(F("Created new policy branch with id '%s'") % policy_uid);
  }
}

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
  create_policy_branch(db, keys, app.lua, app.opts,
                       branch_prefix(project_name), admin_keys,
                       policy_uid, policy_spec);

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

  std::set<revision_id> policy_heads;
  get_branch_heads(policy_policy, false, db, policy_heads, NULL);
  E(policy_heads.size() == 1,
    F("Parent policy branch has %n heads, should have 1") % policy_heads.size());

  revision_id policy_old_rev_id(*policy_heads.begin());

  roster_t policy_old_roster;
  db.get_roster(policy_old_rev_id, policy_old_roster);
  file_path delegation_dir = file_path_internal("delegations");
  file_path delegation_file = delegation_dir / path_component(subprefix);
  E(!policy_old_roster.has_node(delegation_file),
    F("A policy for %s already exists.") % prefix);

  cset policy_changes;

  if (!policy_old_roster.has_node(delegation_dir))
    {
      policy_changes.dirs_added.insert(delegation_dir);
    }

  transaction_guard guard(db);

  cache_user_key(app.opts, app.lua, db, keys);
  std::set<rsa_keypair_id> admin_keys;
  admin_keys.insert(keys.signing_key);

  std::string child_uid;
  data child_spec;
  // Create the new policy branch.
  create_policy_branch(db, keys, app.lua, app.opts,
                       prefix, admin_keys, child_uid, child_spec);
  file_id child_spec_id;
  file_data child_file_dat(child_spec);
  calculate_ident(child_file_dat, child_spec_id);

  // Delegate to it in the parent policy branch.
  policy_changes.files_added.insert(std::make_pair(delegation_file,
                                                   child_spec_id));

  revision_t policy_new_revision;
  make_revision(policy_old_rev_id,
                policy_old_roster,
                policy_changes,
                policy_new_revision);

  {
    revision_id rev_id;
    calculate_ident(policy_new_revision, rev_id);
    revision_data rdat;
    write_revision(policy_new_revision, rdat);


    // write to the db
    if (!db.file_version_exists(child_spec_id))
      {
        db.put_file(child_spec_id, child_file_dat);
      }
    db.put_revision(rev_id, rdat);


    // add certs
    // Do not use project_t::put_standard_certs here, we don't want the
    // branch name to be translated!
    date_t date;
    if (app.opts.date_given)
      date = app.opts.date;
    else
      date = date_t::now();

    std::string author = app.opts.author();
    if (author.empty())
      {
        if (!app.lua.hook_get_author(branch_name(prefix() + ".__policy__"),
                                 keys.signing_key, author))
          author = keys.signing_key();
      }
    utf8 changelog(N_("Create new policy branch."));

    cert_revision_in_branch(db, keys, rev_id,
                            branch_uid(policy_policy.branch_cert_value));
    cert_revision_changelog(db, keys, rev_id, changelog);
    cert_revision_date_time(db, keys, rev_id, date);
    cert_revision_author(db, keys, rev_id, author);
  }

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


  std::set<revision_id> policy_heads;
  get_branch_heads(parent_policy, false, db, policy_heads, NULL);
  N(policy_heads.size() == 1,
    F("Parent policy branch has %n heads, should have 1") % policy_heads.size());
  revision_id policy_old_rev_id(*policy_heads.begin());
  roster_t policy_old_roster;
  db.get_roster(policy_old_rev_id, policy_old_roster);
  file_path branch_dir = file_path_internal("branches");
  file_path branch_file = branch_dir / path_component(relative_name);
  N(!policy_old_roster.has_node(branch_file),
    F("A branch %s already exists under policy %s")
    % relative_name % parent_prefix);

  cset policy_changes;
  if (!policy_old_roster.has_node(branch_dir))
    {
      policy_changes.dirs_added.insert(branch_dir);
    }

  cache_user_key(app.opts, app.lua, db, keys);
  std::set<rsa_keypair_id> admin_keys;
  admin_keys.insert(keys.signing_key);

  std::string branch_uid = generate_uid();
  data branch_spec;
  write_branch_policy(branch_spec, branch_uid, admin_keys);


  file_id branch_spec_id;
  file_data branch_spec_dat(branch_spec);
  calculate_ident(branch_spec_dat, branch_spec_id);

  policy_changes.files_added.insert(std::make_pair(branch_file, branch_spec_id));

  revision_t policy_new_revision;
  make_revision(policy_old_rev_id,
                policy_old_roster,
                policy_changes,
                policy_new_revision);

  revision_id rev_id;
  calculate_ident(policy_new_revision, rev_id);
  revision_data rdat;
  write_revision(policy_new_revision, rdat);


  transaction_guard guard(db);

  if (!db.file_version_exists(branch_spec_id))
    db.put_file(branch_spec_id, branch_spec_dat);
  db.put_revision(rev_id, rdat);

  // project_t::put_standard_certs would translate the branch name,
  // which wouldn't work
  date_t date;
  if (app.opts.date_given)
    date = app.opts.date;
  else
    date = date_t::now();

  std::string author = app.opts.author();
  if (author.empty())
    {
      if (!app.lua.hook_get_author(branch_name(parent_prefix() + ".__policy"),
                                   keys.signing_key, author))
        author = keys.signing_key();
    }
  utf8 changelog(N_("Declare new branch."));

  cert_revision_in_branch(db, keys, rev_id,
                          parent_policy.branch_cert_value);
  cert_revision_changelog(db, keys, rev_id, changelog);
  cert_revision_date_time(db, keys, rev_id, date);
  cert_revision_author(db, keys, rev_id, author);

  guard.commit();
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
