#ifndef __CMD_HH__
#define __CMD_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "app_state.hh"
#include "commands.hh"
#include "constants.hh"
#include "options.hh"
#include "sanity.hh"

namespace commands
{
  extern const std::string hidden_group;
  const char * safe_gettext(const char * msgid);
  struct no_opts {};
  struct command_opts
  {
    std::set<int> opts;
    command_opts() {}
    command_opts & operator%(int o)
    { opts.insert(o); return *this; }
    command_opts & operator%(no_opts o)
    { return *this; }
    command_opts & operator%(command_opts const &o)
    { opts.insert(o.opts.begin(), o.opts.end()); return *this; }
  };
  extern const no_opts OPT_NONE;

  struct command
  {
    // NB: these strings are stored _un_translated, because we cannot
    // translate them until after main starts, by which time the
    // command objects have all been constructed.
    std::string name;
    std::string cmdgroup;
    std::string params_;
    std::string desc_;
    bool use_workspace_options;
    command_opts options;
    command(std::string const & n,
            std::string const & g,
            std::string const & p,
            std::string const & d,
            bool u,
            command_opts const & o);
    virtual ~command();
    virtual std::string params();
    virtual std::string desc();
    virtual void exec(app_state & app, 
                      std::vector<utf8> const & args) = 0;
  };
};

inline std::vector<file_path>
args_to_paths(std::vector<utf8> const & args)
{
  std::vector<file_path> paths;
  for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
    paths.push_back(file_path_external(*i));
  return paths;
}

std::string
get_stdin();

std::string
describe_revision(app_state & app,
                  revision_id const & id);

void
complete(app_state & app,
         std::string const & str,
         revision_id & completion,
         bool must_exist=true);

void
complete(app_state & app,
         std::string const & str,
         std::set<revision_id> & completion,
         bool must_exist=true);

template<typename ID>
static void
complete(app_state & app,
         std::string const & str,
         ID & completion)
{
  N(str.find_first_not_of(constants::legal_id_bytes) == std::string::npos,
    F("non-hex digits in id"));
  if (str.size() == constants::idlen)
    {
      completion = ID(str);
      return;
    }
  std::set<ID> completions;
  app.db.complete(str, completions);
  N(completions.size() != 0,
    F("partial id '%s' does not have an expansion") % str);
  if (completions.size() > 1)
    {
      std::string err = 
        (F("partial id '%s' has multiple ambiguous expansions:\n") 
         % str).str();
      for (typename std::set<ID>::const_iterator i = completions.begin();
            i != completions.end(); ++i)
        err += (i->inner()() + "\n");
      N(completions.size() == 1, i18n_format(err));
    }
  completion = *(completions.begin());
  P(F("expanded partial id '%s' to '%s'")
    % str % completion);
}

void
maybe_update_inodeprints(app_state & app);

void
notify_if_multiple_heads(app_state & app);

void
process_commit_message_args(bool & given,
                            std::string & log_message,
                            app_state & app);

#define CMD(C, group, params, desc, opts)                            \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, params, desc, true,             \
                          command_opts() % opts)                     \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)

// Use this for commands that want to define a params() function
// instead of having a static description. (Good for "automate"
// and possibly "list".)
#define CMD_PARAMS_FN(C, group, desc, opts)                        \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, "", desc, true,               \
                          command_opts() % opts)                     \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
    std::string params();                                            \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)

// Use this for commands that should specifically _not_ look for an
// _MTN dir and load options from it.

#define CMD_NO_WORKSPACE(C, group, params, desc, opts)               \
namespace commands {                                                 \
  struct cmd_ ## C : public command                                  \
  {                                                                  \
    cmd_ ## C() : command(#C, group, params, desc, false,            \
                          command_opts() % opts)                     \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      std::vector<utf8> const & args);               \
  };                                                                 \
  static cmd_ ## C C ## _cmd;                                        \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               std::vector<utf8> const & args)       \

#define ALIAS(C, realcommand)                                        \
CMD(C, realcommand##_cmd.cmdgroup, realcommand##_cmd.params_,         \
    realcommand##_cmd.desc_ + "\nAlias for " #realcommand,            \
    realcommand##_cmd.options)                                       \
{                                                                    \
  process(app, std::string(#realcommand), args);                     \
}

namespace automation {
  struct automate
  {
    std::string name;
    std::string params;
    automate(std::string const & n, std::string const & p);
    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output,
                     std::istream & input) const = 0;
    virtual ~automate();
  };
}

#define AUTOMATE(NAME, PARAMS)                                      \
namespace automation {                                              \
  struct auto_ ## NAME : public automate                            \
  {                                                                 \
    auto_ ## NAME () : automate(#NAME, PARAMS) {}                   \
    void run(std::vector<utf8> args, std::string const & help_name, \
                     app_state & app, std::ostream & output,        \
                     std::istream & input) const;                   \
    virtual ~auto_ ## NAME() {}                                     \
  };                                                                \
  static auto_ ## NAME NAME ## _auto;                               \
}                                                                   \
void automation::auto_ ## NAME :: run(std::vector<utf8> args,       \
                                      std::string const & help_name,\
                                      app_state & app,              \
                                      std::ostream & output,        \
                                      std::istream & input) const




// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
