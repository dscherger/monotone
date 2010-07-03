// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include "safe_map.hh"
#include <utility>
#include <iostream>
#include <iterator>

#include <boost/tuple/tuple.hpp>

#include "basic_io.hh"
#include "cert.hh"
#include "charset.hh"
#include "cmd.hh"
#include "roster.hh"
#include "database.hh"
#include "globish.hh"
#include "keys.hh"
#include "key_store.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab_cast.hh"
#include "app_state.hh"
#include "project.hh"
#include "vocab_cast.hh"
#include "work.hh"

using std::cout;
using std::make_pair;
using std::map;
using std::ostream_iterator;
using std::pair;
using std::set;
using std::sort;
using std::copy;
using std::string;
using std::vector;

CMD_GROUP(list, "list", "ls", CMD_REF(informative),
          N_("Shows database objects"),
          N_("This command is used to query information from the database.  "
             "It shows database objects, or the current workspace manifest, "
             "or known, unknown, intentionally ignored, missing, or "
             "changed-state files."));

namespace {
  // for 'ls certs' and 'ls tags'
  string format_key(key_identity_info const & info)
  {
    string out;

    hexenc<id> hexid;
    encode_hexenc(info.id.inner(), hexid);

    out += info.official_name();
    out += " (";
    out += hexid().substr(0, 10) + "...";
    if (info.given_name != info.official_name)
      {
        out += "; ";
        out += info.given_name();
      }
    out += ")";

    return out;
  }
  string format_key_for_ls_keys(key_identity_info const & info)
  {
    string out;

    hexenc<id> hexid;
    encode_hexenc(info.id.inner(), hexid);

    out += hexid();
    out += " ";
    out += info.official_name();
    if (info.given_name != info.official_name)
      {
        out += " (";
        out += info.given_name();
        out += ")";
      }

    return out;
  }
}

CMD(certs, "certs", "", CMD_REF(list), "ID",
    N_("Lists certificates attached to an identifier"),
    "",
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  project_t project(db);
  key_store keys(app);
  vector<cert> certs;

  transaction_guard guard(db, false);

  revision_id ident;
  complete(app.opts, app.lua,  project, idx(args, 0)(), ident);
  vector<cert> ts;
  project.get_revision_certs(ident, ts);

  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i));

  {
    set<key_id> checked;
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !db.public_key_exists(idx(certs, i).key))
          P(F("no public key '%s' found in database")
            % idx(certs, i).key);
        checked.insert(idx(certs, i).key);
      }
  }

  // Make the output deterministic; this is useful for the test suite, in
  // particular.
  sort(certs.begin(), certs.end());

  string str     = _("Key   : %s\n"
                     "Sig   : %s\n"
                     "Name  : %s\n"
                     "Value : %s\n");
  string extra_str = "      : %s\n";

  string::size_type colon_pos = str.find(':');

  if (colon_pos != string::npos)
    {
      string substr(str, 0, colon_pos);
      colon_pos = display_width(utf8(substr, origin::internal));
      extra_str = string(colon_pos, ' ') + ": %s\n";
    }

  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_status status = db.check_cert(idx(certs, i));
      cert_value tv = idx(certs, i).value;
      string washed;
      if (guess_binary(tv()))
        {
          washed = "<binary data>";
        }
      else
        {
          washed = tv();
        }

      string stat;
      switch (status)
        {
        case cert_ok:
          stat = _("ok");
          break;
        case cert_bad:
          stat = _("bad");
          break;
        case cert_unknown:
          stat = _("unknown");
          break;
        }

      vector<string> lines;
      split_into_lines(washed, lines);
      std::string value_first_line = lines.empty() ? "" : idx(lines, 0);

      key_identity_info identity;
      identity.id = idx(certs, i).key;
      project.complete_key_identity_from_id(keys, app.lua, identity);

      cout << string(guess_terminal_width(), '-') << '\n'
           << (i18n_format(str)
               % format_key(identity)
               % stat
               % idx(certs, i).name
               % value_first_line);

      for (size_t i = 1; i < lines.size(); ++i)
        cout << (i18n_format(extra_str) % idx(lines, i));
    }

  if (!certs.empty())
    cout << '\n';

  guard.commit();
}

CMD(duplicates, "duplicates", "", CMD_REF(list), "",
    N_("Lists duplicate files in the specified revision."
       " If no revision is specified, use the workspace"),
    "",
    options::opts::revision)
{
  if (!args.empty())
    throw usage(execid);

  revision_id rev_id;
  roster_t roster;
  database db(app);
  project_t project(db);

  E(app.opts.revision_selectors.size() <= 1, origin::user,
    F("more than one revision given"));

  if (app.opts.revision_selectors.empty())
    {
      workspace work(app);
      temp_node_id_source nis;

      work.get_current_roster_shape(db, nis, roster);
    }
  else
    {
      complete(app.opts, app.lua, project,
               idx(app.opts.revision_selectors, 0)(), rev_id);
      E(db.revision_exists(rev_id), origin::user,
        F("no revision %s found in database") % rev_id);
      db.get_roster(rev_id, roster);
    }

  // To find the duplicate files, we put all file_ids in a map
  // and count how many times they occur in the roster.
  //
  // Structure of file_id_map is following:
  //  first : file_id
  //  second :
  //    first : unsigned int
  //    second : file_paths (=vector<file_path>)
  typedef std::vector<file_path> file_paths;
  typedef std::pair<unsigned int, file_paths> file_count;
  typedef std::map<file_id, file_count> file_id_map;
  file_id_map file_map;

  node_map const & nodes = roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_t node = i->second;
      if (is_file_t(node))
        {
          file_t f = downcast_to_file_t(node);
          file_path p;
          roster.get_name(i->first, p);

          file_id_map::iterator iter = file_map.find(f->content);
          if (iter == file_map.end())
            {
              file_paths paths;
              paths.push_back(p);
              file_count fc(1, paths);
              file_map.insert(make_pair(f->content, fc));
            }
          else
            {
              iter->second.first++;
              iter->second.second.push_back(p);
            }
        }
    }

  string empty_checksum(40, ' ');
  for (file_id_map::const_iterator i = file_map.begin();
       i != file_map.end(); ++i)
    {
      if (i->second.first > 1)
        {
          bool first_print = true;
          for (file_paths::const_iterator j = i->second.second.begin();
               j != i->second.second.end(); ++j)
            {
              if (first_print)
                {
                  cout << i->first;
                  first_print = false;
                }
              else
                cout << empty_checksum;

              cout << " " << *j << '\n';
            }
        }
    }
}

struct key_location_info
{
  key_identity_info identity;
  vector<string> public_locations;
  vector<string> private_locations;
};
typedef map<key_id, key_location_info> key_map;

namespace {
  void get_key_list(database & db,
                    key_store & keys,
                    lua_hooks & lua,
                    project_t & project,
                    key_map & items)
  {
    items.clear();

    {
      vector<key_id> dbkeys;
      if (db.database_specified())
        {
          db.get_key_ids(dbkeys);
          for (vector<key_id>::iterator i = dbkeys.begin();
               i != dbkeys.end(); i++)
            {
              key_identity_info identity;
              identity.id = *i;
              project.complete_key_identity_from_id(lua, identity);
              items[*i].identity = identity;
              items[*i].public_locations.push_back("database");
            }
        }
    }
    {
      vector<key_id> kskeys;
      keys.get_key_ids(kskeys);
      for (vector<key_id>::iterator i = kskeys.begin();
           i != kskeys.end(); i++)
        {
          key_identity_info identity;
          identity.id = *i;
          project.complete_key_identity_from_id(keys, lua, identity);
          items[*i].identity = identity;
          items[*i].public_locations.push_back("keystore");
          items[*i].private_locations.push_back("keystore");
        }
    }
  }
}

CMD(keys, "keys", "", CMD_REF(list), "[PATTERN]",
    N_("Lists keys that match a pattern"),
    "",
    options::opts::none)
{
  if (args.size() > 1)
    throw usage(execid);

  database db(app);
  key_store keys(app);
  project_t project(db);

  key_map items;
  get_key_list(db, keys, app.lua, project, items);

  if (items.empty())
    {
      P(F("no keys found"));
    }

  key_map matched_items;
  if (args.size() == 1)
    {
      globish pattern(idx(args, 0)(), origin::user);
      for (key_map::iterator i = items.begin(); i != items.end(); ++i)
        {
          string const & alias = i->second.identity.official_name();
          if (pattern.matches(alias))
            {
              matched_items.insert(*i);
            }
        }
      if (matched_items.empty())
        {
          W(F("no keys found matching '%s'") % idx(args, 0)());
        }
    }
  else
    {
      matched_items = items;
    }

  bool have_keystore_only_key = false;
  // sort key => rendered line
  map<string, string> public_rendered;
  map<string, string> private_rendered;

  set<string> seen_aliases;
  set<string> duplicate_aliases;

  for (key_map::iterator i = matched_items.begin();
       i != matched_items.end(); ++i)
    {
      key_identity_info const & identity = i->second.identity;
      string const & alias = i->second.identity.official_name();
      vector<string> const & public_locations = i->second.public_locations;
      vector<string> const & private_locations = i->second.private_locations;

      if (seen_aliases.find(alias) != seen_aliases.end())
        {
          duplicate_aliases.insert(alias);
        }
      seen_aliases.insert(alias);

      string rendered_basic = format_key_for_ls_keys(identity);
      string sort_key = alias + identity.id.inner()();

      if (!public_locations.empty())
        {
          string rendered = rendered_basic;
          if (public_locations.size() == 1 &&
              idx(public_locations, 0) == "keystore")
            {
              have_keystore_only_key = true;
              rendered += "   (*)";
            }
          public_rendered.insert(make_pair(sort_key, rendered));
        }
      if (!private_locations.empty())
        {
          private_rendered.insert(make_pair(sort_key, rendered_basic));
        }
    }

  if (!public_rendered.empty())
    {
      cout << "\n[public keys]\n";
      for (map<string, string>::iterator i = public_rendered.begin();
           i != public_rendered.end(); ++i)
        {
          cout << i->second << "\n";
        }
      if (have_keystore_only_key)
        {
          cout << (F("(*) - only in %s/")
                   % keys.get_key_dir()) << '\n';
        }
      cout << "\n";
    }
  if (!private_rendered.empty())
    {
      cout << "\n[private keys]\n";
      for (map<string, string>::iterator i = private_rendered.begin();
           i != private_rendered.end(); ++i)
        {
          cout << i->second << "\n";
        }
      cout << "\n";
    }

  if (!duplicate_aliases.empty())
    {
      W(F("Some key names refer to multiple keys"));
      for (set<string>::iterator i = duplicate_aliases.begin();
           i != duplicate_aliases.end(); i++)
        {
          P(F("Duplicate Key: %s") % *i);
        }
    }

}

CMD(branches, "branches", "", CMD_REF(list), "[PATTERN]",
    N_("Lists branches in the database that match a pattern"),
    "",
    options::opts::exclude)
{
  globish inc("*", origin::internal);
  if (args.size() == 1)
    inc = globish(idx(args,0)(), origin::user);
  else if (args.size() > 1)
    throw usage(execid);

  database db(app);
  project_t project(db);
  globish exc(app.opts.exclude_patterns);
  set<branch_name> names;
  project.get_branch_list(inc, names, !app.opts.ignore_suspend_certs);

  for (set<branch_name>::const_iterator i = names.begin();
       i != names.end(); ++i)
    if (!exc.matches((*i)()) && !app.lua.hook_ignore_branch(*i))
      cout << *i << '\n';
}

CMD(epochs, "epochs", "", CMD_REF(list), "[BRANCH [...]]",
    N_("Lists the current epoch of branches that match a pattern"),
    "",
    options::opts::none)
{
  database db(app);
  map<branch_name, epoch_data> epochs;
  db.get_epochs(epochs);

  if (args.empty())
    {
      for (map<branch_name, epoch_data>::const_iterator
             i = epochs.begin();
           i != epochs.end(); ++i)
        {
          cout << encode_hexenc(i->second.inner()(),
                                i->second.inner().made_from)
               << ' ' << i->first << '\n';
        }
    }
  else
    {
      for (args_vector::const_iterator i = args.begin();
           i != args.end();
           ++i)
        {
          map<branch_name, epoch_data>::const_iterator j =
            epochs.find(typecast_vocab<branch_name>((*i)));
          E(j != epochs.end(), origin::user, F("no epoch for branch %s") % *i);
          cout << encode_hexenc(j->second.inner()(),
                                j->second.inner().made_from)
               << ' ' << j->first << '\n';
        }
    }
}

CMD(tags, "tags", "", CMD_REF(list), "[PATTERN]",
    N_("Lists all tags in the database"),
    "",
    options::opts::exclude)
{
  globish inc("*", origin::internal);
  if (args.size() == 1)
    inc = globish(idx(args,0)(), origin::user);
  else if (args.size() > 1)
    throw usage(execid);

  database db(app);
  set<tag_t> tags;
  project_t project(db);
  cert_name branch = branch_cert_name;

  project.get_tags(tags);

  for (set<tag_t>::const_iterator i = tags.begin(); i != tags.end(); ++i)
    {
      key_identity_info identity;
      identity.id = i->key;
      project.complete_key_identity_from_id(app.lua, identity);

      vector<cert> certs;
      project.get_revision_certs(i->ident, certs);

      globish exc(app.opts.exclude_patterns);

      if (inc.matches(i->name()) && !exc.matches(i->name()))
        {
          hexenc<id> hexid;
          encode_hexenc(i->ident.inner(), hexid);

          cout << i->name << ' ' << hexid().substr(0,10) << "... ";

          for (vector<cert>::const_iterator c = certs.begin();
               c != certs.end(); ++c)
            {
              if (c->name == branch)
                {
                  cout << c->value << ' ';
                }
            }

          cout << format_key(identity)  << '\n';
        }
    }
}

CMD(vars, "vars", "", CMD_REF(list), "[DOMAIN]",
    N_("Lists variables in the whole database or a domain"),
    "",
    options::opts::none)
{
  bool filterp;
  var_domain filter;
  if (args.empty())
    {
      filterp = false;
    }
  else if (args.size() == 1)
    {
      filterp = true;
      filter = typecast_vocab<var_domain>(idx(args, 0));
    }
  else
    throw usage(execid);

  database db(app);
  map<var_key, var_value> vars;
  db.get_vars(vars);
  for (map<var_key, var_value>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      if (filterp && !(i->first.first == filter))
        continue;
      cout << i->first.first << ": "
           << i->first.second << ' '
           << i->second << '\n';
    }
}

CMD(databases, "databases", "dbs", CMD_REF(list), "",
    N_("Lists managed databases and their known workspaces"),
    "",
    options::opts::none)
{
  vector<system_path> search_paths, files, dirs;

  E(app.lua.hook_get_default_database_locations(search_paths), origin::database,
    F("could not query default database locations"));

  database_path_helper helper(app.lua);

  for (vector<system_path>::const_iterator i = search_paths.begin();
       i != search_paths.end(); ++i)
    {
      system_path search_path(*i);

      fill_path_vec<system_path> fill_files(search_path, files, false);
      fill_path_vec<system_path> fill_dirs(search_path, dirs, true);
      read_directory(search_path, fill_files, fill_dirs);

      for (vector<system_path>::const_iterator j = files.begin();
           j != files.end(); ++j)
        {
          system_path db_path(*j);

          // a little optimization, so we don't scan and open every file
          string p = db_path.as_internal();
          if (p.size() < 4 || p.substr(p.size() - 4) != ".mtn")
            {
              L(FL("ignoring file '%s'") % db_path);
              continue;
            }

          options opts;
          opts.dbname_type = unmanaged_db;
          opts.dbname = db_path;
          opts.dbname_given = true;

          database db(opts, app.lua);

          try
            {
              db.ensure_open();
            }
          catch (recoverable_failure & f)
            {
              L(FL("could not open '%s': %s") % db_path % f.what());
              continue;
            }

          string managed_path = db_path.as_internal().substr(
              search_path.as_internal().size() + 1
          );
          cout << F(":%s (in %s):") % managed_path % search_path << '\n';

          bool has_valid_workspaces = false;

          vector<system_path> workspaces;
          db.get_registered_workspaces(workspaces);

          for (vector<system_path>::const_iterator k = workspaces.begin();
               k != workspaces.end(); ++k)
            {
              system_path workspace_path(*k);
              if (!directory_exists(workspace_path / bookkeeping_root_component))
                {
                  L(FL("ignoring missing workspace '%s'") % workspace_path);
                  continue;
                }

              options workspace_opts;
              workspace::get_options(workspace_path, workspace_opts);

              system_path workspace_db_path;
              helper.get_database_path(workspace_opts, workspace_db_path);

              if (workspace_db_path != db_path)
                {
                  L(FL("ignoring workspace '%s', expected database %s, "
                       "but has %s configured in _MTN/options")
                      % workspace_path % db_path % workspace_db_path);
                  continue;
                }

              has_valid_workspaces = true;

              string workspace_branch = workspace_opts.branch();
              if (!workspace_opts.branch_given)
                workspace_branch = _("<no branch set>");

              cout << F("\t%s (in %s)") % workspace_branch % workspace_path << '\n';
            }

            if (!has_valid_workspaces)
              cout << F("\tno known valid workspaces") << '\n';
        }
    }
}

CMD(known, "known", "", CMD_REF(list), "",
    N_("Lists workspace files that belong to the current branch"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);

  roster_t new_roster;
  temp_node_id_source nis;
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        new_roster, ignored_file(work));

  // to be printed sorted
  vector<file_path> print_paths;

  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster.is_root(nid)
          && mask.includes(new_roster, nid))
        {
          file_path p;
          new_roster.get_name(nid, p);
          print_paths.push_back(p);
        }
    }

  sort(print_paths.begin(), print_paths.end());
  copy(print_paths.begin(), print_paths.end(),
       ostream_iterator<file_path>(cout, "\n"));
}

CMD(unknown, "unknown", "ignored", CMD_REF(list), "",
    N_("Lists workspace files that do not belong to the current branch"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);

  vector<file_path> roots = args_to_paths(args);
  path_restriction mask(roots, args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth, ignored_file(work));
  set<file_path> unknown, ignored;

  // if no starting paths have been specified use the workspace root
  if (roots.empty())
    roots.push_back(file_path());

  work.find_unknown_and_ignored(db, mask, roots, unknown, ignored);

  utf8 const & realname = execid[execid.size() - 1];
  if (realname() == "ignored")
    copy(ignored.begin(), ignored.end(),
         ostream_iterator<file_path>(cout, "\n"));
  else
    {
      I(realname() == "unknown");
      copy(unknown.begin(), unknown.end(),
           ostream_iterator<file_path>(cout, "\n"));
    }
}

CMD(missing, "missing", "", CMD_REF(list), "",
    N_("Lists files that belong to the branch but are not in the workspace"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);
  temp_node_id_source nis;
  roster_t current_roster_shape;
  work.get_current_roster_shape(db, nis, current_roster_shape);
  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        current_roster_shape, ignored_file(work));

  set<file_path> missing;
  work.find_missing(current_roster_shape, mask, missing);

  copy(missing.begin(), missing.end(),
       ostream_iterator<file_path>(cout, "\n"));
}


CMD(changed, "changed", "", CMD_REF(list), "",
    N_("Lists files that have changed with respect to the current revision"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);

  parent_map parents;
  roster_t new_roster;
  temp_node_id_source nis;
  work.get_current_roster_shape(db, nis, new_roster);
  work.update_current_roster_from_filesystem(new_roster);

  work.get_parent_rosters(db, parents);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        parents, new_roster, ignored_file(work));

  revision_t rrev;
  make_restricted_revision(parents, new_roster, mask, rrev);

  // to be printed sorted, with duplicates removed
  set<file_path> print_paths;

  for (edge_map::const_iterator i = rrev.edges.begin();
       i != rrev.edges.end(); i++)
    {
      set<node_id> nodes;
      roster_t const & old_roster
        = *safe_get(parents, edge_old_revision(i)).first;
      select_nodes_modified_by_cset(edge_changes(i),
                                    old_roster, new_roster, nodes);

      for (set<node_id>::const_iterator i = nodes.begin(); i != nodes.end();
           ++i)
        {
          file_path p;
          if (new_roster.has_node(*i))
            new_roster.get_name(*i, p);
          else
            old_roster.get_name(*i, p);
          print_paths.insert(p);
        }
    }

  copy(print_paths.begin(), print_paths.end(),
       ostream_iterator<file_path>(cout, "\n"));
}

namespace
{
  namespace syms
  {
    // certs
    symbol const key("key");
    symbol const signature("signature");
    symbol const name("name");
    symbol const value("value");
    symbol const trust("trust");

    // keys
    symbol const hash("hash");
    symbol const given_name("given_name");
    symbol const local_name("local_name");
    symbol const public_location("public_location");
    symbol const private_location("private_location");
  }
};

// Name: keys
// Arguments: none
// Added in: 1.1
// Changed in: 10.0
// Purpose: Prints all keys in the keystore, and if a database is given
//   also all keys in the database, in basic_io format.
// Output format: For each key, a basic_io stanza is printed. The items in
//   the stanza are:
//     name - the key identifier
//     hash - the hash of the key
//     public_location - where the public half of the key is stored
//     private_location - where the private half of the key is stored
//   The *_location items may have multiple values, as shown below
//   for public_location.
//   If the private key does not exist, then the private_hash and
//   private_location items will be absent.
//
// Sample output:
//               name "tbrownaw@gmail.com"
//               hash [475055ec71ad48f5dfaf875b0fea597b5cbbee64]
//    public_location "database" "keystore"
//   private_location "keystore"
//
//              name "njs@pobox.com"
//              hash [de84b575d5e47254393eba49dce9dc4db98ed42d]
//   public_location "database"
//
//               name "foo@bar.com"
//               hash [7b6ce0bd83240438e7a8c7c207d8654881b763f6]
//    public_location "keystore"
//   private_location "keystore"
//
// Error conditions: None.
CMD_AUTOMATE(keys, "",
             N_("Lists all keys in the keystore"),
             "",
             options::opts::none)
{
  E(args.empty(), origin::user,
    F("no arguments needed"));

  database db(app);
  key_store keys(app);
  project_t project(db);

  key_map items;
  get_key_list(db, keys, app.lua, project, items);

  basic_io::printer prt;
  for (key_map::iterator i = items.begin(); i != items.end(); ++i)
    {
      basic_io::stanza stz;
      stz.push_binary_pair(syms::hash, i->first.inner());
      stz.push_str_pair(syms::given_name, i->second.identity.given_name());
      stz.push_str_pair(syms::local_name, i->second.identity.official_name());
      stz.push_str_multi(syms::public_location, i->second.public_locations);
      if (!i->second.private_locations.empty())
        stz.push_str_multi(syms::private_location, i->second.private_locations);
      prt.print_stanza(stz);
    }
  output.write(prt.buf.data(), prt.buf.size());
}

// Name: certs
// Arguments:
//   1: a revision id
// Added in: 1.0
// Purpose: Prints all certificates associated with the given revision
//   ID. Each certificate is contained in a basic IO stanza. For each
//   certificate, the following values are provided:
//
//   'key' : a string indicating the key used to sign this certificate.
//   'signature': a string indicating the status of the signature.
//   Possible values of this string are:
//     'ok'        : the signature is correct
//     'bad'       : the signature is invalid
//     'unknown'   : signature was made with an unknown key
//   'name' : the name of this certificate
//   'value' : the value of this certificate
//   'trust' : is this certificate trusted by the defined trust metric
//   Possible values of this string are:
//     'trusted'   : this certificate is trusted
//     'untrusted' : this certificate is not trusted
//
// Output format: All stanzas are formatted by basic_io. Stanzas are
// seperated by a blank line. Values will be escaped, '\' -> '\\' and
// '"' -> '\"'.
//
// Error conditions: If a certificate is signed with an unknown public
// key, a warning message is printed to stderr. If the revision
// specified is unknown or invalid prints an error message to stderr
// and exits with status 1.
CMD_AUTOMATE(certs, N_("REV"),
             N_("Prints all certificates attached to a revision"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  project_t project(db);

  vector<cert> certs;

  transaction_guard guard(db, false);

  hexenc<id> hrid(idx(args, 0)(), origin::user);
  revision_id rid(decode_hexenc_as<revision_id>(hrid(), origin::user));

  E(db.revision_exists(rid), origin::user,
    F("no such revision '%s'") % hrid);

  vector<cert> ts;
  // FIXME_PROJECTS: after projects are implemented,
  // use the db version instead if no project is specified.
  project.get_revision_certs(rid, ts);

  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i));

  {
    set<key_id> checked;
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !db.public_key_exists(idx(certs, i).key))
          W(F("no public key '%s' found in database")
            % idx(certs, i).key);
        checked.insert(idx(certs, i).key);
      }
  }

  // Make the output deterministic; this is useful for the test suite,
  // in particular.
  sort(certs.begin(), certs.end());

  basic_io::printer pr;

  for (size_t i = 0; i < certs.size(); ++i)
    {
      basic_io::stanza st;
      cert_status status = db.check_cert(idx(certs, i));
      cert_value tv = idx(certs, i).value;
      cert_name name = idx(certs, i).name;
      set<key_identity_info> signers;

      key_identity_info identity;
      identity.id = idx(certs, i).key;
      project.complete_key_identity_from_id(app.lua, identity);
      signers.insert(identity);

      bool trusted =
        app.lua.hook_get_revision_cert_trust(signers, rid.inner(),
                                             name, tv);

      hexenc<id> keyid_enc;
      encode_hexenc(identity.id.inner(), keyid_enc);
      st.push_hex_pair(syms::key, keyid_enc);

      string stat;
      switch (status)
        {
        case cert_ok:
          stat = "ok";
          break;
        case cert_bad:
          stat = "bad";
          break;
        case cert_unknown:
          stat = "unknown";
          break;
        }
      st.push_str_pair(syms::signature, stat);

      st.push_str_pair(syms::name, name());
      st.push_str_pair(syms::value, tv());
      st.push_str_pair(syms::trust, (trusted ? "trusted" : "untrusted"));

      pr.print_stanza(st);
    }
  output.write(pr.buf.data(), pr.buf.size());

  guard.commit();
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
