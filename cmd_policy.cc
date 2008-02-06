// Copyright 2008 Timothy Brownawell <tbrownaw@prjek.net>
//
// This file is made available under the GNU GPL v2 or later.

#include "base.hh"

#include "app_state.hh"
#include "basic_io.hh"
#include "botan/botan.h"
#include "cmd.hh"
#include "dates.hh"
#include "file_io.hh"
#include "keys.hh"
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
    cache_user_key(opts, lua, keys, db);

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

    cert_revision_in_branch(rev_id, branch_uid(policy_uid), db, keys);
    cert_revision_changelog(rev_id, changelog, db, keys);
    cert_revision_date_time(rev_id, date, db, keys);
    cert_revision_author(rev_id, author, db, keys);


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
  N(args.size() == 1,
    F("Wrong argument count."));
  std::string project_name = idx(args, 0)();
  system_path project_dir = app.opts.conf_dir / "projects";
  system_path project_file = project_dir / path_component(project_name);

  require_path_is_directory(app.opts.conf_dir,
			    F("Cannot find configuration directory."),
			    F("Configuration directory is a file."));
  require_path_is_nonexistent(project_file,
			      F("You already have a project with that name."));
  mkdir_p(project_dir);

  cache_user_key(app.opts, app.lua, app.keys, app.db);
  std::set<rsa_keypair_id> admin_keys;
  admin_keys.insert(app.keys.signing_key);

  std::string policy_uid;
  data policy_spec;
  create_policy_branch(app.db, app.keys, app.lua, app.opts,
		       branch_prefix(project_name), admin_keys,
		       policy_uid, policy_spec);

  write_data(project_file, policy_spec, project_dir);
  P(F("Wrote project spec to %s") % project_file);
}

CMD(create_subpolicy, "create_subpolicy", "", CMD_REF(policy),
    N_("NAME"),
    N_("Create a policy for a new subtree of an existing project."),
    "",
    options::opts::none)
{
}
