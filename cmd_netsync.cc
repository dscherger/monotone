#include "cmd.hh"

#include "netsync.hh"
#include "globish.hh"
#include "keys.hh"
#include "cert.hh"

#include <fstream>

using std::ifstream;
using std::ofstream;
using std::string;
using std::vector;

static const var_key default_server_key(var_domain("database"),
                                        var_name("default-server"));
static const var_key default_include_pattern_key(var_domain("database"),
                                                 var_name("default-include-pattern"));
static const var_key default_exclude_pattern_key(var_domain("database"),
                                                 var_name("default-exclude-pattern"));

static void
process_netsync_args(string const & name,
                     vector<utf8> const & args,
                     utf8 & addr,
                     utf8 & include_pattern, utf8 & exclude_pattern,
                     bool use_defaults,
                     bool serve_mode,
                     app_state & app)
{
  // handle host argument
  if (!serve_mode)
    {
      if (args.size() >= 1)
        {
          addr = idx(args, 0);
          if (use_defaults
              && (!app.db.var_exists(default_server_key) || app.set_default))
            {
              P(F("setting default server to %s") % addr);
              app.db.set_var(default_server_key, var_value(addr()));
            }
        }
      else
        {
          N(use_defaults, F("no hostname given"));
          N(app.db.var_exists(default_server_key),
            F("no server given and no default server set"));
          var_value addr_value;
          app.db.get_var(default_server_key, addr_value);
          addr = utf8(addr_value());
          L(FL("using default server address: %s") % addr);
        }
    }

  // handle include/exclude args
  if (serve_mode || (args.size() >= 2 || !app.exclude_patterns.empty()))
    {
      E(serve_mode || args.size() >= 2, F("no branch pattern given"));
      int pattern_offset = (serve_mode ? 0 : 1);
      vector<utf8> patterns(args.begin() + pattern_offset, args.end());
      combine_and_check_globish(patterns, include_pattern);
      combine_and_check_globish(app.exclude_patterns, exclude_pattern);
      if (use_defaults &&
          (!app.db.var_exists(default_include_pattern_key) || app.set_default))
        {
          P(F("setting default branch include pattern to '%s'") % include_pattern);
          app.db.set_var(default_include_pattern_key, var_value(include_pattern()));
        }
      if (use_defaults &&
          (!app.db.var_exists(default_exclude_pattern_key) || app.set_default))
        {
          P(F("setting default branch exclude pattern to '%s'") % exclude_pattern);
          app.db.set_var(default_exclude_pattern_key, var_value(exclude_pattern()));
        }
    }
  else
    {
      N(use_defaults, F("no branch pattern given"));
      N(app.db.var_exists(default_include_pattern_key),
        F("no branch pattern given and no default pattern set"));
      var_value pattern_value;
      app.db.get_var(default_include_pattern_key, pattern_value);
      include_pattern = utf8(pattern_value());
      L(FL("using default branch include pattern: '%s'") % include_pattern);
      if (app.db.var_exists(default_exclude_pattern_key))
        {
          app.db.get_var(default_exclude_pattern_key, pattern_value);
          exclude_pattern = utf8(pattern_value());
        }
      else
        exclude_pattern = utf8("");
      L(FL("excluding: %s") % exclude_pattern);
    }
}

CMD(push, N_("network"), N_("[ADDRESS[:PORTNUMBER] [PATTERN]]"),
    N_("push branches matching PATTERN to netsync server at ADDRESS"),
    OPT_SET_DEFAULT % OPT_EXCLUDE % OPT_KEY_TO_PUSH)
{
  utf8 addr, include_pattern, exclude_pattern;
  process_netsync_args(name, args, addr, include_pattern, exclude_pattern, true, false, app);

  rsa_keypair_id key;
  get_user_key(key, app);
  app.signing_key = key;

  run_netsync_protocol(client_voice, source_role, addr,
                       include_pattern, exclude_pattern, app);
}

CMD(pull, N_("network"), N_("[ADDRESS[:PORTNUMBER] [PATTERN]]"),
    N_("pull branches matching PATTERN from netsync server at ADDRESS"),
    OPT_SET_DEFAULT % OPT_EXCLUDE)
{
  utf8 addr, include_pattern, exclude_pattern;
  process_netsync_args(name, args, addr, include_pattern, exclude_pattern, true, false, app);

  if (app.signing_key() == "")
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication"));

  run_netsync_protocol(client_voice, sink_role, addr,
                       include_pattern, exclude_pattern, app);
}

CMD(sync, N_("network"), N_("[ADDRESS[:PORTNUMBER] [PATTERN]]"),
    N_("sync branches matching PATTERN with netsync server at ADDRESS"),
    OPT_SET_DEFAULT % OPT_EXCLUDE % OPT_KEY_TO_PUSH)
{
  utf8 addr, include_pattern, exclude_pattern;
  process_netsync_args(name, args, addr, include_pattern, exclude_pattern, true, false, app);

  rsa_keypair_id key;
  get_user_key(key, app);
  app.signing_key = key;

  run_netsync_protocol(client_voice, source_and_sink_role, addr,
                       include_pattern, exclude_pattern, app);
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
    E(file.is_open(), F("failed to create pid file '%s'") % path);
    file << get_process_id() << "\n";
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

CMD_NO_WORKSPACE(serve, N_("network"), N_("PATTERN ..."),
                 N_("serve the branches specified by PATTERNs to connecting clients"),
                 OPT_BIND % OPT_STDIO % OPT_NO_TRANSPORT_AUTH % OPT_PIDFILE % OPT_EXCLUDE)
{
  if (args.size() < 1)
    throw usage(name);

  pid_file pid(app.pidfile);

  if (app.use_transport_auth)
    {
      rsa_keypair_id key;
      get_user_key(key, app);
      app.signing_key = key;

      N(app.lua.hook_persist_phrase_ok(),
	F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
      require_password(key, app);
    }
  else
    {
      E(app.bind_stdio,
	F("The --no-transport-auth option is only permitted in combination with --stdio"));
    }

  app.db.ensure_open();

  utf8 dummy_addr, include_pattern, exclude_pattern;
  process_netsync_args(name, args, dummy_addr, include_pattern, exclude_pattern, false, true, app);
  run_netsync_protocol(server_voice, source_and_sink_role, app.bind_address,
                       include_pattern, exclude_pattern, app);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
