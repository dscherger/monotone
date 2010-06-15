// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "constants.hh"
#include "vocab.hh"

#include "database.hh"
#include "options.hh"
#include "globish.hh"
#include "lexical_cast.hh"
#include "lua_hooks.hh"
#include "netsync.hh"
#include "network/connection_info.hh"
#include "uri.hh"
#include "vocab_cast.hh"

#include <vector>

using std::vector;
using std::string;
using boost::lexical_cast;

netsync_connection_info::netsync_connection_info(database & d, options const & o) :
  client(d, o)
{ }

netsync_connection_info::Client::Client(database & d, options const & o) :
  connection_successful(false),
  use_argv(false),
  connection_type(netsync_connection),
  input_stream(0),
  output_stream(0),
  db(d), opts(o)
{
  var_key default_server_key(var_domain("database"),
                             var_name("default-server"));
  var_key default_include_pattern_key(var_domain("database"),
                                      var_name("default-include-pattern"));
  var_key default_exclude_pattern_key(var_domain("database"),
                                      var_name("default-exclude-pattern"));

  if (db.var_exists(default_server_key))
  {
      var_value addr_value;
      db.get_var(default_server_key, addr_value);
      try
        {
          set_raw_uri(addr_value());
          L(FL("loaded default server address: %s") % addr_value());
        }
      catch (recoverable_failure & e)
        {
          W(F("ignoring invalid default server address '%s': %s")
            %  addr_value() % e.what());
        }
  }

  if (db.var_exists(default_include_pattern_key))
  {
    var_value pattern_value;
    db.get_var(default_include_pattern_key, pattern_value);
    try
      {
        include_pattern = globish(pattern_value(), origin::user);
        L(FL("loaded default branch include pattern: '%s'") % include_pattern);
      }
    catch (recoverable_failure & e)
      {
        W(F("ignoring invalid default branch include pattern '%s': %s")
          %  include_pattern % e.what());
      }

    if (db.var_exists(default_exclude_pattern_key))
      {
        db.get_var(default_exclude_pattern_key, pattern_value);
        try
          {
            exclude_pattern = globish(pattern_value(), origin::user);
            L(FL("loaded default branch exclude pattern: '%s'") % exclude_pattern);
          }
        catch (recoverable_failure & e)
          {
            W(F("ignoring invalid default branch exclude pattern '%s': %s")
              %  exclude_pattern % e.what());
          }
      }
  }
}

netsync_connection_info::Client::~Client()
{
  if (connection_successful)
    {
      var_key default_server_key(var_domain("database"),
                                 var_name("default-server"));
      var_key default_include_pattern_key(var_domain("database"),
                                          var_name("default-include-pattern"));
      var_key default_exclude_pattern_key(var_domain("database"),
                                          var_name("default-exclude-pattern"));

      var_key server_include(var_domain("server-include"),
                             var_name(uri.resource, origin::user));
      var_key server_exclude(var_domain("server-exclude"),
                             var_name(uri.resource, origin::user));

      // Maybe set the default values.
      if (!db.var_exists(default_server_key)
          || opts.set_default)
        {
          L(FL("setting default server to %s") % uri.resource);
          db.set_var(default_server_key,
                     var_value(uri.resource, origin::user));
        }
      if (!db.var_exists(default_include_pattern_key)
          || opts.set_default)
        {
          L(FL("setting default branch include pattern to '%s'") % include_pattern);
          db.set_var(default_include_pattern_key,
                     typecast_vocab<var_value>(include_pattern));
        }
      if (!db.var_exists(default_exclude_pattern_key)
          || opts.set_default)
        {
          L(FL("setting default branch exclude pattern to '%s'") % exclude_pattern);
          db.set_var(default_exclude_pattern_key,
                     typecast_vocab<var_value>(exclude_pattern));
        }
      if (!db.var_exists(server_include)
          || opts.set_default)
        {
          L(FL("setting default include pattern for server '%s' to '%s'")
            % uri.resource % include_pattern);
          db.set_var(server_include,
                     typecast_vocab<var_value>(include_pattern));
        }
      if (!db.var_exists(server_exclude)
          || opts.set_default)
        {
          L(FL("setting default exclude pattern for server '%s' to '%s'")
            % uri.resource % exclude_pattern);
          db.set_var(server_exclude,
                     typecast_vocab<var_value>(exclude_pattern));
        }
    }
}

void
netsync_connection_info::Client::set_connection_successful()
{
  connection_successful = true;
}

std::istream &
netsync_connection_info::Client::get_input_stream() const
{
  I(input_stream);
  return *input_stream;
}

automate_ostream &
netsync_connection_info::Client::get_output_stream() const
{
  I(output_stream);
  return *output_stream;
}

void
netsync_connection_info::Client::set_input_stream(std::istream & is)
{
  input_stream = &is;
}

void
netsync_connection_info::Client::set_output_stream(automate_ostream & os)
{
  output_stream = &os;
}

Netxx::port_type
netsync_connection_info::Client::get_port() const
{
  std::size_t port = constants::netsync_default_port;
  if (!uri.port.empty())
    port = atoi(uri.port.c_str());
  return lexical_cast<Netxx::port_type>(port);
}

void
netsync_connection_info::Client::set_include_pattern(vector<arg_type> const & pat)
{
  // do not overwrite default patterns
  if (pat.size() == 0)
    return;

  for (vector<arg_type>::const_iterator p = pat.begin(); p != pat.end(); p++)
    {
      if ((*p)().find_first_of("'\"") != string::npos)
        {
          W(F("include branch pattern contains a quote character:\n"
              "%s") % (*p)());
        }
    }
  include_pattern = globish(pat);
}

globish
netsync_connection_info::Client::get_include_pattern() const
{
  return include_pattern;
}

void
netsync_connection_info::Client::set_exclude_pattern(vector<arg_type> const & pat)
{
  // do not overwrite default patterns
  if (pat.size() == 0)
    return;

  for (vector<arg_type>::const_iterator p = pat.begin(); p != pat.end(); p++)
    {
      if ((*p)().find_first_of("'\"") != string::npos)
        {
          W(F("exclude branch pattern contains a quote character:\n"
              "%s") % (*p)());
        }
    }
  exclude_pattern = globish(pat);
}

globish
netsync_connection_info::Client::get_exclude_pattern() const
{
  return exclude_pattern;
}

void
netsync_connection_info::Client::set_raw_uri(string const & raw_uri)
{
  parse_uri(raw_uri, uri, origin::user);

  var_key server_include(var_domain("server-include"),
                         var_name(uri.resource, origin::user));
  var_key server_exclude(var_domain("server-exclude"),
                         var_name(uri.resource, origin::user));

  if (db.var_exists(server_include))
    {
      var_value pattern_value;
      db.get_var(server_include, pattern_value);
      include_pattern = globish(pattern_value(), origin::user);
      L(FL("loaded default branch include pattern for resource %s: '%s'")
        % uri.resource % include_pattern);

      if (db.var_exists(server_exclude))
        {
          db.get_var(server_exclude, pattern_value);
          exclude_pattern = globish(pattern_value(), origin::user);
          L(FL("loaded default branch exclude pattern for resource %s: '%s'")
            % uri.resource % exclude_pattern);
        }
    }
}

uri_t
netsync_connection_info::Client::get_uri() const
{
  return uri;
}

void
netsync_connection_info::Client::set_connection_type(netsync_connection_info::conn_type type)
{
  connection_type = type;
}

netsync_connection_info::conn_type
netsync_connection_info::Client::get_connection_type() const
{
  return connection_type;
}

bool
netsync_connection_info::Client::get_use_argv() const
{
  return use_argv;
}

vector<string>
netsync_connection_info::Client::get_argv() const
{
  return argv;
}

void
netsync_connection_info::Client::maybe_set_argv(lua_hooks & lua)
{
  use_argv = lua.hook_get_netsync_connect_command(
    uri, include_pattern, exclude_pattern,
    global_sanity.debug_p(), argv
  );
}

void
netsync_connection_info::Client::ensure_completeness() const
{
  E(!uri.resource.empty(), origin::user,
    F("connection resource is empty and no default value could be loaded"));

  E(!include_pattern().empty(), origin::user,
    F("branch pattern is empty and no default value could be loaded"));
}

void
netsync_connection_info::parse_includes_excludes_from_query(string const & query,
                                                            vector<arg_type> & includes,
                                                            vector<arg_type> & excludes)
{
  includes.clear();
  excludes.clear();

  char const separator = ',';
  char const negate = '-';

  string::size_type begin = 0;
  string::size_type end = query.find(separator);
  while (begin < query.size())
    {
      std::string item = query.substr(begin, end);
      if (end == string::npos)
        begin = end;
      else
        {
          begin = end+1;
          if (begin < query.size())
            end = query.find(separator, begin);
        }

      bool is_exclude = false;
      if (item.size() >= 1 && item.at(0) == negate)
        {
          is_exclude = true;
          item.erase(0, 1);
        }
      if (is_exclude)
        excludes.push_back(arg_type(urldecode(item, origin::user), origin::user));
      else
        includes.push_back(arg_type(urldecode(item, origin::user), origin::user));
    }
}

void
netsync_connection_info::setup_default(options const & opts,
                                       database & db,
                                       lua_hooks & lua,
                                       shared_conn_info & info)
{
  info.reset(new netsync_connection_info(db, opts));

  info->client.ensure_completeness();
  info->client.maybe_set_argv(lua);
}

void
netsync_connection_info::setup_from_sync_request(options const & opts,
                                                 database & db,
                                                 lua_hooks & lua,
                                                 server_initiated_sync_request const & request,
                                                 shared_conn_info & info)
{
  info.reset(new netsync_connection_info(db, opts));

  info->client.set_raw_uri(request.address);

  bool include_exclude_given = !request.include.empty() ||
                               !request.exclude.empty();
  bool query_exists = !info->client.uri.query.empty();

  E(!(include_exclude_given && query_exists), origin::user,
    F("include / exclude pattern was given both as part of the URL "
      "and as a separate argument."));

  vector<arg_type> includes, excludes;

  if (include_exclude_given)
  {
    if (!request.include.empty())
      {
        includes.push_back(arg_type(request.include, origin::user));
        if (!request.exclude.empty())
          {
            excludes.push_back(arg_type(request.exclude, origin::user));
          }
      }
  }
  else
  {
    parse_includes_excludes_from_query(info->client.uri.query,
                                       includes,
                                       excludes);
  }

  info->client.set_include_pattern(includes);
  info->client.set_exclude_pattern(excludes);

  info->client.ensure_completeness();
  info->client.maybe_set_argv(lua);
}

void
netsync_connection_info::setup_from_uri(options const & opts,
                                        database & db,
                                        lua_hooks & lua,
                                        arg_type const & uri,
                                        shared_conn_info & info)
{
  info.reset(new netsync_connection_info(db, opts));

  info->client.set_raw_uri(uri());

  vector<arg_type> includes, excludes;
  parse_includes_excludes_from_query(info->client.uri.query,
                                     includes, excludes);

  if (includes.size() == 0)
    {
      W(F("no branch pattern found in URI, will try to use "
          "suitable database defaults if available"));
    }
  else
    {
      info->client.set_include_pattern(includes);
      info->client.set_exclude_pattern(excludes);
    }

  info->client.ensure_completeness();
  info->client.maybe_set_argv(lua);
}

void
netsync_connection_info::setup_from_server_and_pattern(options const & opts,
                                                       database & db,
                                                       lua_hooks & lua,
                                                       arg_type const & host,
                                                       vector<arg_type> const & includes,
                                                       vector<arg_type> const & excludes,
                                                       shared_conn_info & info)
{
  info.reset(new netsync_connection_info(db, opts));

  info->client.set_raw_uri("mtn://" + host());
  info->client.set_include_pattern(includes);
  info->client.set_exclude_pattern(excludes);

  info->client.ensure_completeness();
  info->client.maybe_set_argv(lua);
}

void
netsync_connection_info::setup_for_serve(options const & opts,
                                         database & db,
                                         lua_hooks & lua,
                                         shared_conn_info & info)
{
  info.reset(new netsync_connection_info(db, opts));
  info->server.addrs = opts.bind_uris;

  if (opts.use_transport_auth)
    {
      E(lua.hook_persist_phrase_ok(), origin::user,
        F("need permission to store persistent passphrase "
          "(see hook persist_phrase_ok())"));

      // the uri as well as the include / exclude pattern are
      // not used directly for serve, but need to be configured
      // in order to let keys::cache_netsync_key() call the
      // get_netsync_key() hook properly
      if (!opts.bind_uris.empty())
        info->client.set_raw_uri((*opts.bind_uris.begin())());

      info->client.include_pattern = globish("*", origin::internal);
      info->client.exclude_pattern = globish("", origin::internal);
    }
  else if (!opts.bind_stdio)
    W(F("The --no-transport-auth option is usually only used "
        "in combination with --stdio"));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
