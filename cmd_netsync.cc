// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2006 Timothy Brownawell <tbrownaw@gmail.com>
//               2010 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cmd.hh"

#include "automate_ostream_demuxed.hh"
#include "basic_io.hh"
#include "merge_content.hh"
#include "netsync.hh"
#include "network/connection_info.hh"
#include "file_io.hh"
#include "globish.hh"
#include "keys.hh"
#include "key_store.hh"
#include "cert.hh"
#include "revision.hh"
#include "uri.hh"
#include "vocab_cast.hh"
#include "platform-wrapped.hh"
#include "app_state.hh"
#include "maybe_workspace_updater.hh"
#include "project.hh"
#include "work.hh"
#include "database.hh"
#include "roster.hh"
#include "ui.hh"

#include <fstream>

using std::ifstream;
using std::ofstream;
using std::map;
using std::set;
using std::string;
using std::vector;

using boost::shared_ptr;

static void
extract_client_connection_info(options & opts,
                               project_t & project,
                               key_store & keys,
                               lua_hooks & lua,
                               connection_type type,
                               args_vector const & args,
                               shared_conn_info & info,
                               key_requiredness_flag key_requiredness = key_required)
{
  if (opts.remote_stdio_host_given)
    {
       netsync_connection_info::setup_from_uri(opts, project.db, lua, type,
                                               opts.remote_stdio_host, info);
    }
  else
    {
      if (args.size() == 1)
        {
          E(!opts.exclude_given, origin::user,
            F("cannot use --exclude in URL mode"));

          netsync_connection_info::setup_from_uri(opts, project.db, lua, type,
                                                  idx(args, 0), info);
        }
      else if (args.size() >= 2)
        {
          arg_type server = idx(args, 0);
          vector<arg_type> include;
          include.insert(include.begin(),
                                  args.begin() + 1,
                                  args.end());
          vector<arg_type> exclude = opts.exclude;

          netsync_connection_info::setup_from_server_and_pattern(opts, project.db,
                                                                 lua, type, server,
                                                                 include,
                                                                 exclude,
                                                                 info);
        }
      else
       {
         // if no argument has been given and the --remote_stdio_host
         // option has been left out, try to load the database defaults
         // at least
         netsync_connection_info::setup_default(opts, project.db,
                                                lua, type, info);
       }
    }

  opts.no_transport_auth =
    !lua.hook_use_transport_auth(info->client.get_uri());

  if (!opts.no_transport_auth)
    {
      cache_netsync_key(opts, project, keys, lua, info, key_requiredness);
    }
}

CMD_AUTOMATE_NO_STDIO(remote_stdio,
                      N_("[URL]\n[ADDRESS[:PORTNUMBER]]"),
                      N_("Opens an 'automate stdio' connection to a remote server"),
                      "",
                      options::opts::max_netsync_version |
                      options::opts::min_netsync_version |
                      options::opts::set_default)
{
  if (args.size() > 1)
    throw usage(execid);

  app.opts.non_interactive = true;

  if (app.opts.dbname.empty())
    {
      W(F("No database given; assuming '%s' database. This means that we can't\n"
          "verify the server key, because we have no record of what it should be.")
          % memory_db_identifier);
      app.opts.dbname_type = memory_db;
    }

  database db(app);
  key_store keys(app);
  project_t project(db);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 automate_connection, args, info);

  info->client.set_input_stream(std::cin);
  long packet_size = constants::default_stdio_packet_size;
  if (app.opts.automate_stdio_size_given)
    packet_size = app.opts.automate_stdio_size;
  automate_ostream os(output, packet_size);
  info->client.set_output_stream(os);

  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, source_and_sink_role, info,
                       connection_counts::create());
}

// shamelessly copied and adapted from option.cc
static void
parse_options_from_args(args_vector & args,
                        std::vector<std::pair<std::string, arg_type> > & opts)
{
  bool seen_dashdash = false;
  for (args_vector::size_type i = 0; i < args.size(); )
    {
      string name;
      arg_type arg;

      if (idx(args,i)() == "--" || seen_dashdash)
        {
          if (!seen_dashdash)
            {
              seen_dashdash = true;
            }
          ++i;
          continue;
        }
      else if (idx(args,i)().substr(0,2) == "--")
        {
          string::size_type equals = idx(args,i)().find('=');
          bool has_arg;
          if (equals == string::npos)
            {
              name = idx(args,i)().substr(2);
              has_arg = false;
            }
          else
            {
              name = idx(args,i)().substr(2, equals-2);
              has_arg = true;
            }

          if (has_arg)
            {
              arg = arg_type(idx(args,i)().substr(equals+1), origin::user);
            }
        }
      else if (idx(args,i)().substr(0,1) == "-")
        {
          name = idx(args,i)().substr(1,1);
          bool has_arg = idx(args,i)().size() > 2;

          if (has_arg)
            {
              arg = arg_type(idx(args,i)().substr(2), origin::user);
            }
        }
      else
        {
          ++i;
          continue;
        }

      opts.push_back(std::pair<std::string, arg_type>(name, arg));
      args.erase(args.begin() + i);
    }
}

CMD_AUTOMATE_NO_STDIO(remote,
                      N_("COMMAND [ARGS]"),
                      N_("Executes COMMAND on a remote server"),
                      "",
                      options::opts::remote_stdio_host |
                      options::opts::max_netsync_version |
                      options::opts::min_netsync_version |
                      options::opts::set_default)
{
  E(args.size() >= 1, origin::user,
    F("wrong argument count"));

  if (app.opts.dbname.empty())
    {
      W(F("No database given; assuming '%s' database. This means that we can't\n"
          "verify the server key, because we have no record of what it should be.")
          % memory_db_identifier);
      app.opts.dbname_type = memory_db;
    }

  database db(app);
  key_store keys(app);
  project_t project(db);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 automate_connection, args_vector(), info);

  args_vector cleaned_args(args);
  std::vector<std::pair<std::string, arg_type> > opts;
  parse_options_from_args(cleaned_args, opts);

  std::stringstream ss;
  if (opts.size() > 0)
    {
      ss << 'o';
      for (unsigned int i=0; i < opts.size(); ++i)
        {
          ss << opts.at(i).first.size()  << ':' << opts.at(i).first;
          ss << opts.at(i).second().size() << ':' << opts.at(i).second();
        }
      ss << 'e' << ' ';
    }

  ss << 'l';
  for (args_vector::size_type i=0; i<cleaned_args.size(); ++i)
  {
      std::string arg = idx(cleaned_args, i)();
      ss << arg.size() << ':' << arg;
  }
  ss << 'e';

  L(FL("stdio input: %s") % ss.str());

  long packet_size = constants::default_stdio_packet_size;
  if (app.opts.automate_stdio_size_given)
    packet_size = app.opts.automate_stdio_size;
  automate_ostream_demuxed os(output, std::cerr, packet_size);

  info->client.set_input_stream(ss);
  info->client.set_output_stream(os);

  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, source_and_sink_role, info,
                       connection_counts::create());

  E(os.get_error() == 0, origin::network,
    F("received remote error code %d") % os.get_error());
}

static void
print_dryrun_info_cmd(protocol_role role,
                      shared_conn_counts counts,
                      project_t & project)
{
  // print dryrun info for command line
  if (role != source_role)
    {
      if (counts->keys_in.can_have_more_than_min)
        {
          P(F("would receive %d revisions, %d certs, and at least %d keys")
            % counts->revs_in.min_count
            % counts->certs_in.min_count
            % counts->keys_in.min_count);
        }
      else
        {
          P(F("would receive %d revisions, %d certs, and %d keys")
            % counts->revs_in.min_count
            % counts->certs_in.min_count
            % counts->keys_in.min_count);
        }
    }
  if (role != sink_role)
    {
      P(F("would send %d certs and %d keys")
        % counts->certs_out.min_count
        % counts->keys_out.min_count);
      P(F("would send %d revisions:")
        % counts->revs_out.min_count);
      map<branch_name, int> branch_counts;
      for (vector<revision_id>::const_iterator i = counts->revs_out.items.begin();
           i != counts->revs_out.items.end(); ++i)
        {
          set<branch_name> my_branches;
          project.get_revision_branches(*i, my_branches);
          for(set<branch_name>::iterator b = my_branches.begin();
              b != my_branches.end(); ++b)
            {
              ++branch_counts[*b];
            }
        }
      for (map<branch_name, int>::iterator i = branch_counts.begin();
           i != branch_counts.end(); ++i)
        {
          P(F("%9d in branch %s") % i->second % i->first);
        }
    }
}

namespace
{
  namespace syms
  {
    symbol const branch("branch");
    symbol const cert("cert");
    symbol const dryrun("dryrun");
    symbol const estimate("estimate");
    symbol const key("key");
    symbol const receive("receive");
    symbol const revision("revision");
    symbol const send("send");
    symbol const value("value");
  }
}

static void
print_dryrun_info_auto(protocol_role role,
                       shared_conn_counts counts,
                       project_t & project,
                       std::ostream & output)
{
  // print dry run info for automate session
  basic_io::printer pr;
  basic_io::stanza st;

  st.push_symbol(syms::dryrun);

  if (role != source_role)
    {
      // sink or sink_and_source; print sink info
      st.push_symbol(syms::receive);

      if (counts->keys_in.can_have_more_than_min)
        {
          st.push_symbol(syms::estimate);
        }

      st.push_str_pair(syms::revision,
                       boost::lexical_cast<string>(counts->revs_in.min_count));
      st.push_str_pair(syms::cert,
                       boost::lexical_cast<string>(counts->certs_in.min_count));
      st.push_str_pair(syms::key,
                       boost::lexical_cast<string>(counts->keys_in.min_count));
    }
  if (role != sink_role)
    {
      // source or sink_and_source; print source info
      st.push_symbol(syms::send);

      st.push_str_pair(syms::revision,
                       boost::lexical_cast<string>(counts->revs_out.items.size()));
      st.push_str_pair(syms::cert,
                       boost::lexical_cast<string>(counts->certs_out.min_count));
      st.push_str_pair(syms::key,
                       boost::lexical_cast<string>(counts->keys_out.min_count));
      map<branch_name, int> branch_counts;
      for (vector<revision_id>::const_iterator i = counts->revs_out.items.begin();
           i != counts->revs_out.items.end(); ++i)
        {
          set<branch_name> my_branches;
          project.get_revision_branches(*i, my_branches);
          for(set<branch_name>::iterator b = my_branches.begin();
              b != my_branches.end(); ++b)
            {
              ++branch_counts[*b];
            }
        }
      for (map<branch_name, int>::iterator i = branch_counts.begin();
           i != branch_counts.end(); ++i)
        {
          st.push_str_triple(syms::branch, i->first(), boost::lexical_cast<string>(i->second));
        }
    }
  pr.print_stanza(st);
  output.write(pr.buf.data(), pr.buf.size());
}

static void
print_cert(cert const & item,
           basic_io::printer & pr,
           bool print_rev)
{
  basic_io::stanza st;
  st.push_str_pair(syms::cert, item.name());
  st.push_str_pair(syms::value, item.value());
  st.push_binary_pair(syms::key, item.key.inner());
  if (print_rev)
    st.push_binary_pair(syms::revision, item.ident.inner());
  pr.print_stanza(st);
}

static void
print_info_auto(protocol_role role,
                shared_conn_counts counts,
                project_t & project,
                std::ostream & output)
{
  // print info for automate session
  basic_io::printer pr;

  if (role != source_role)
    {
      // sink or sink_and_source; print sink info

      // Sort received certs into those associated with a received revision, and
      // others.
      vector<cert> unattached_certs;
      map<revision_id, vector<cert> > rev_certs;

      for (vector<revision_id>::const_iterator i = counts->revs_in.items.begin();
           i != counts->revs_in.items.end(); ++i)
        rev_certs.insert(make_pair(*i, vector<cert>()));
      for (vector<cert>::const_iterator i = counts->certs_in.items.begin();
           i != counts->certs_in.items.end(); ++i)
        {
          map<revision_id, vector<cert> >::iterator j;
          j = rev_certs.find(revision_id(i->ident));
          if (j == rev_certs.end())
            unattached_certs.push_back(*i);
          else
            j->second.push_back(*i);
        }

      if (rev_certs.size() > 0)
        {
          {
            basic_io::stanza st;
            st.push_str_pair(syms::receive, syms::revision);
            pr.print_stanza(st);
          }

          {
            for (map<revision_id, vector<cert> >::const_iterator i = rev_certs.begin();
                 i != rev_certs.end(); ++i)
              {
                basic_io::stanza st;
                st.push_binary_pair(syms::revision, i->first.inner());
                pr.print_stanza(st);

                for (vector<cert>::const_iterator j = i->second.begin();
                     j != i->second.end(); ++j)
                  {
                    print_cert(*j, pr, false);
                  }
              }
          }
        }

      if (unattached_certs.size() > 0)
        {
          {
            basic_io::stanza st;
            st.push_str_pair(syms::receive, syms::cert);
            pr.print_stanza(st);
          }

          for (vector<cert>::const_iterator i = unattached_certs.begin();
               i != unattached_certs.end(); ++i)
            {
              print_cert(*i, pr, true);
            }
        }

      if (counts->keys_in.items.size() > 0)
        {
          {
            basic_io::stanza st;
            st.push_str_pair(syms::receive, syms::key);
            pr.print_stanza(st);
          }

          {
            basic_io::stanza st;
            for (vector<key_id>::const_iterator i = counts->keys_in.items.begin();
                 i != counts->keys_in.items.end(); ++i)
              {
                st.push_binary_pair(syms::key, i->inner());
              }
            pr.print_stanza(st);
          }
        }
    }

  if (role != sink_role)
    {
      // source or sink_and_source; print source info

      // Sort sent certs into those associated with a sent revision, and
      // others.
      vector<cert> unattached_certs;
      map<revision_id, vector<cert> > rev_certs;

      for (vector<revision_id>::const_iterator i = counts->revs_out.items.begin();
           i != counts->revs_out.items.end(); ++i)
        rev_certs.insert(make_pair(*i, vector<cert>()));
      for (vector<cert>::const_iterator i = counts->certs_out.items.begin();
           i != counts->certs_out.items.end(); ++i)
        {
          map<revision_id, vector<cert> >::iterator j;
          j = rev_certs.find(revision_id(i->ident));
          if (j == rev_certs.end())
            unattached_certs.push_back(*i);
          else
            j->second.push_back(*i);
        }

      if (rev_certs.size() > 0)
        {
          {
            basic_io::stanza st;
            st.push_str_pair(syms::send, syms::revision);
            pr.print_stanza(st);
          }

          {
            for (map<revision_id, vector<cert> >::const_iterator i = rev_certs.begin();
                 i != rev_certs.end(); ++i)
              {
                basic_io::stanza st;
                st.push_binary_pair(syms::revision, i->first.inner());
                pr.print_stanza(st);

                for (vector<cert>::const_iterator j = i->second.begin();
                     j != i->second.end(); ++j)
                  {
                    print_cert(*j, pr, false);
                  }
              }
          }
        }

      if (unattached_certs.size() > 0)
        {
          {
            basic_io::stanza st;
            st.push_str_pair(syms::send, syms::cert);
            pr.print_stanza(st);
          }

          for (vector<cert>::const_iterator i = unattached_certs.begin();
               i != unattached_certs.end(); ++i)
            {
              print_cert(*i, pr, true);
            }
        }

      if (counts->keys_out.items.size() > 0)
        {
          {
            basic_io::stanza st;
            st.push_str_pair(syms::receive, syms::key);
            pr.print_stanza(st);
          }

          {
            basic_io::stanza st;
            for (vector<key_id>::const_iterator i = counts->keys_out.items.begin();
                 i != counts->keys_out.items.end(); ++i)
              {
                st.push_binary_pair(syms::key, i->inner());
              }
            pr.print_stanza(st);
          }
        }
    }

  output.write(pr.buf.data(), pr.buf.size());
}

CMD(push, "push", "", CMD_REF(network),
    N_("[URL]\n[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Pushes branches to a netsync server"),
    N_("This will push all branches that match the pattern given in PATTERN "
       "to the netsync server at the address ADDRESS."),
    options::opts::max_netsync_version | options::opts::min_netsync_version |
    options::opts::set_default | options::opts::exclude |
    options::opts::keys_to_push | options::opts::dryrun)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 netsync_connection, args, info);

  shared_conn_counts counts = connection_counts::create();
  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, source_role, info, counts);
  if (app.opts.dryrun)
    print_dryrun_info_cmd(source_role, counts, project);
}

CMD_AUTOMATE(push, N_("[URL]\n[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
             N_("Pushes branches to a netsync server"),
             "",
             options::opts::max_netsync_version |
             options::opts::min_netsync_version |
             options::opts::set_default | options::opts::exclude |
             options::opts::keys_to_push | options::opts::dryrun)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 netsync_connection, args, info);

  shared_conn_counts counts = connection_counts::create();
  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, source_role, info, counts);
  if (app.opts.dryrun)
    print_dryrun_info_auto(source_role, counts, project, output);
  else
    print_info_auto(source_role, counts, project, output);
}

CMD(pull, "pull", "", CMD_REF(network),
    N_("[URL]\n[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Pulls branches from a netsync server"),
    N_("This pulls all branches that match the pattern given in PATTERN "
       "from the netsync server at the address ADDRESS."),
    options::opts::max_netsync_version | options::opts::min_netsync_version |
    options::opts::set_default | options::opts::exclude |
    options::opts::auto_update | options::opts::dryrun)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  maybe_workspace_updater updater(app, project);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 netsync_connection, args, info, key_optional);

  if (!keys.have_signing_key())
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  shared_conn_counts counts = connection_counts::create();
  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, sink_role, info, counts);

  updater.maybe_do_update();
  if (app.opts.dryrun)
    print_dryrun_info_cmd(sink_role, counts, project);
}

CMD_AUTOMATE(pull, N_("[URL]\n[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
             N_("Pulls branches from a netsync server"),
             "",
             options::opts::max_netsync_version |
             options::opts::min_netsync_version |
             options::opts::set_default | options::opts::exclude |
             options::opts::dryrun)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 netsync_connection, args, info, key_optional);

  shared_conn_counts counts = connection_counts::create();
  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, sink_role, info, counts);
  if (app.opts.dryrun)
    print_dryrun_info_auto(sink_role, counts, project, output);
  else
    print_info_auto(sink_role, counts, project, output);
}

CMD(sync, "sync", "", CMD_REF(network),
    N_("[URL]\n[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
    N_("Synchronizes branches with a netsync server"),
    N_("This synchronizes branches that match the pattern given in PATTERN "
       "with the netsync server at the address ADDRESS."),
    options::opts::max_netsync_version | options::opts::min_netsync_version |
    options::opts::set_default | options::opts::exclude |
    options::opts::keys_to_push | options::opts::auto_update |
    options::opts::dryrun)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  maybe_workspace_updater updater(app, project);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 netsync_connection, args, info);

  if (app.opts.set_default && workspace::found)
    {
      // Write workspace options, including key; this is the simplest way to
      // fix a "found multiple keys" error reported by sync.
      workspace::set_options(app.opts, app.lua);
    }

  shared_conn_counts counts = connection_counts::create();
  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, source_and_sink_role, info, counts);

  updater.maybe_do_update();
  if (app.opts.dryrun)
    print_dryrun_info_cmd(source_and_sink_role, counts, project);
}

CMD_AUTOMATE(sync, N_("[URL]\n[ADDRESS[:PORTNUMBER] [PATTERN ...]]"),
             N_("Synchronizes branches with a netsync server"),
             "",
             options::opts::max_netsync_version | options::opts::min_netsync_version |
             options::opts::set_default | options::opts::exclude |
             options::opts::keys_to_push | options::opts::dryrun)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  shared_conn_info info;
  extract_client_connection_info(app.opts, project, keys, app.lua,
                                 netsync_connection, args, info);

  if (app.opts.set_default && workspace::found)
  {
    // Write workspace options, including key; this is the simplest way to
    // fix a "found multiple keys" error reported by sync.
    workspace::set_options(app.opts, app.lua);
  }

  shared_conn_counts counts = connection_counts::create();
  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, source_and_sink_role, info, counts);
  if (app.opts.dryrun)
    print_dryrun_info_auto(source_and_sink_role, counts, project, output);
  else
    print_info_auto(source_and_sink_role, counts, project, output);
}

CMD_NO_WORKSPACE(clone, "clone", "", CMD_REF(network),
                 N_("URL [DIRECTORY]\nHOST[:PORTNUMBER] BRANCH [DIRECTORY]"),
                 N_("Checks out a revision from a remote database into a directory"),
                 N_("If a revision is given, that's the one that will be checked out.  "
                    "Otherwise, it will be the head of the branch supplied.  "
                    "If no directory is given, the branch name will be used as directory"),
                 options::opts::max_netsync_version | options::opts::min_netsync_version |
                 options::opts::revision | options::opts::branch)
{

  bool url_arg = (args.size() == 1 || args.size() == 2) &&
                 idx(args, 0)().find("://") != string::npos;

  bool host_branch_arg = (args.size() == 2 || args.size() == 3) &&
                         idx(args, 0)().find("://") == string::npos;

  bool no_ambigious_revision = app.opts.revision.size() < 2;

  if (!(no_ambigious_revision && (url_arg || host_branch_arg)))
    throw usage(execid);

  E(url_arg || (host_branch_arg && !app.opts.branch_given), origin::user,
    F("the --branch option is only valid with an URI to clone"));

  // we create the database before anything else, but we
  // do not clean newly created databases up if the clone fails
  // (and I think this is correct, because if the pull fails later
  // on due to some network error, the use does not have to start
  // again from the beginning)
  database_path_helper helper(app.lua);
  helper.maybe_set_default_alias(app.opts);

  database db(app);
  project_t project(db);
  key_store keys(app);

  db.create_if_not_exists();
  db.ensure_open();

  shared_conn_info info;
  arg_type server = idx(args, 0);
  arg_type workspace_arg;

   if (url_arg)
    {
      E(!app.opts.exclude_given, origin::user,
        F("cannot use --exclude in URL mode"));

      netsync_connection_info::setup_from_uri(app.opts, project.db, app.lua,
                                              netsync_connection, server, info);
      if (args.size() == 2)
        workspace_arg = idx(args, 1);
    }
  else
    {
      vector<arg_type> include;
      include.push_back(idx(args, 1));
      netsync_connection_info::setup_from_server_and_pattern(app.opts, project.db,
                                                             app.lua,
                                                             netsync_connection,
                                                             server, include,
                                                             app.opts.exclude,
                                                             info);
      if (args.size() == 3)
        workspace_arg = idx(args, 2);
    }

  if (app.opts.branch().empty())
    {
      globish include_pattern = info->client.get_include_pattern();
      E(!include_pattern().empty() && !include_pattern.contains_meta_chars(),
        origin::user, F("you must specify an unambiguous branch to clone"));
      app.opts.branch = branch_name(include_pattern(), origin::user);
    }

  I(!app.opts.branch().empty());

  app.opts.no_transport_auth =
    !app.lua.hook_use_transport_auth(info->client.get_uri());

  if (!app.opts.no_transport_auth)
    {
      cache_netsync_key(app.opts, project, keys, app.lua, info, key_optional);
    }

  bool target_is_current_dir = false;
  system_path workspace_dir;
  if (workspace_arg().empty())
    {
      // No checkout dir specified, use branch name for dir.
      workspace_dir = system_path(app.opts.branch(), origin::user);
    }
  else
    {
      target_is_current_dir =
        workspace_arg == utf8(".");
      workspace_dir = system_path(workspace_arg);
    }

  if (!target_is_current_dir)
    {
      require_path_is_nonexistent
        (workspace_dir,
         F("clone destination directory '%s' already exists")
         % workspace_dir);
    }

  system_path _MTN_dir = workspace_dir / path_component("_MTN");

  require_path_is_nonexistent
    (_MTN_dir, F("bookkeeping directory already exists in '%s'")
     % workspace_dir);

  directory_cleanup_helper remove_on_fail(
    target_is_current_dir ? _MTN_dir : workspace_dir
  );

  // remember the initial working dir so that relative file://
  // db URIs will work
  system_path start_dir(get_current_working_dir(), origin::system);

  workspace::create_workspace(app.opts, app.lua, workspace_dir);

  if (!keys.have_signing_key())
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  // make sure we're back in the original dir so that file: URIs work
  change_current_working_dir(start_dir);

  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       client_voice, sink_role, info,
                       connection_counts::create());

  change_current_working_dir(workspace_dir);

  transaction_guard guard(db, false);

  revision_id ident;
  if (app.opts.revision.empty())
    {
      set<revision_id> heads;
      project.get_branch_heads(app.opts.branch, heads,
                               app.opts.ignore_suspend_certs);
      E(!heads.empty(), origin::user,
        F("branch '%s' is empty") % app.opts.branch);
      if (heads.size() > 1)
        {
          P(F("branch %s has multiple heads:") % app.opts.branch);
          for (set<revision_id>::const_iterator i = heads.begin(); i != heads.end(); ++i)
            P(i18n_format("  %s")
              % describe_revision(app.opts, app.lua, project, *i));
          P(F("choose one with '%s clone -r<id> URL'") % prog_name);
          E(false, origin::user, F("branch %s has multiple heads") % app.opts.branch);
        }
      ident = *(heads.begin());
    }
  else if (app.opts.revision.size() == 1)
    {
      // use specified revision
      complete(app.opts, app.lua, project, idx(app.opts.revision, 0)(), ident);

      E(project.revision_is_in_branch(ident, app.opts.branch),
        origin::user,
        F("revision %s is not a member of branch %s")
          % ident % app.opts.branch);
    }

  roster_t empty_roster, current_roster;

  L(FL("checking out revision %s to directory %s")
    % ident % workspace_dir);
  db.get_roster(ident, current_roster);

  workspace work(app);
  revision_t workrev;
  make_revision_for_workspace(ident, cset(), workrev);
  work.put_work_rev(workrev);

  cset checkout;
  make_cset(empty_roster, current_roster, checkout);

  content_merge_checkout_adaptor wca(db);
  work.perform_content_update(empty_roster, current_roster, checkout, wca, false);

  work.maybe_update_inodeprints(db);
  guard.commit();
  remove_on_fail.commit();
}

struct pid_file
{
  explicit pid_file(system_path const & p)
    : path(p)
  {
    if (path.empty())
      return;
    require_path_is_nonexistent(path, F("pid file '%s' already exists") % path);
    file.open(path.as_external().c_str());
    E(file.is_open(), origin::system, F("failed to create pid file '%s'") % path);
    file << get_process_id() << '\n';
    file.flush();
  }

  ~pid_file()
  {
    if (path.empty())
      return;
    pid_t pid;
    ifstream(path.as_external().c_str()) >> pid;
    if (pid == get_process_id()) {
      file.close();
      delete_file(path);
    }
  }

private:
  ofstream file;
  system_path path;
};

CMD_NO_WORKSPACE(serve, "serve", "", CMD_REF(network), "",
                 N_("Serves the database to connecting clients"),
                 "",
                 options::opts::max_netsync_version |
                 options::opts::min_netsync_version |
                 options::opts::pidfile |
                 options::opts::bind_opts)
{
  if (!args.empty())
    throw usage(execid);

  database db(app);
  key_store keys(app);
  project_t project(db);
  pid_file pid(app.opts.pidfile);

  db.ensure_open();

  shared_conn_info info;
  netsync_connection_info::setup_for_serve(app.opts, project.db, app.lua, info);

  if (!app.opts.no_transport_auth)
    {
      cache_netsync_key(app.opts, project, keys, app.lua, info, key_required);
    }

  run_netsync_protocol(app, app.opts, app.lua, project, keys,
                       server_voice, source_and_sink_role, info,
                       connection_counts::create());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
