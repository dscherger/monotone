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

#include <map>
#include <set>
#include <string>

#include "commands.hh"
#include "options.hh"
#include "sanity.hh"

class app_state;
class database;
struct workspace;

namespace commands
{
  class command
  {
  public:
    typedef std::set< utf8 > names_set;
    typedef std::set< command * > children_set;

  private:
    // NB: these strings are stored _un_translated, because we cannot
    // translate them until after main starts, by which time the
    // command objects have all been constructed.
    utf8 m_primary_name;
    names_set m_names;
    command * m_parent;
    bool m_hidden;
    utf8 m_params;
    utf8 m_abstract;
    utf8 m_desc;
    bool m_use_workspace_options;
    options::options_type m_opts;
    children_set m_children;

    std::map< command_id, command * >
      find_completions(utf8 const & prefix, command_id const & completed)
      const;
    command * find_child_by_name(utf8 const & name) const;

  public:
    command(std::string const & primary_name,
            std::string const & other_names,
            command * parent,
            bool hidden,
            std::string const & params,
            std::string const & abstract,
            std::string const & desc,
            bool use_workspace_options,
            options::options_type const & opts);

    virtual ~command(void);

    command_id ident(void) const;

    utf8 const & primary_name(void) const;
    names_set const & names(void) const;
    command * parent(void) const;
    bool hidden(void) const;
    virtual std::string params(void) const;
    virtual std::string abstract(void) const;
    virtual std::string desc(void) const;
    options::options_type const & opts(void) const;
    bool use_workspace_options(void) const;
    children_set & children(void);
    children_set const & children(void) const;
    bool is_leaf(void) const;

    bool operator<(command const & cmd) const;

    virtual void exec(app_state & app,
                      command_id const & execid,
                      args_vector const & args) = 0;

    bool has_name(utf8 const & name) const;
    command * find_command(command_id const & id);
    std::set< command_id >
      complete_command(command_id const & id,
                       command_id completed = command_id()) const;
  };

  class automate : public command
  {
    // This function is supposed to be called only after the requirements
    // for "automate" commands have been fulfilled.  This is done by the
    // "exec" function defined below, which implements code shared among
    // all automation commands.  Also, this is also needed by the "stdio"
    // automation, as it executes multiple of these commands sharing the
    // same initialization, hence the friend declaration.
    virtual void exec_from_automate(args_vector args,
                                    command_id const & execid,
                                    app_state & app,
                                    std::ostream & output) const = 0;
    friend class automate_stdio;

  public:
    automate(std::string const & name,
             std::string const & params,
             std::string const & abstract,
             std::string const & desc,
             options::options_type const & opts);

    void exec(app_state & app,
              command_id const & execid,
              args_vector const & args);
  };
};

inline std::vector<file_path>
args_to_paths(args_vector const & args)
{
  std::vector<file_path> paths;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      if (bookkeeping_path::external_string_is_bookkeeping_path(*i))
        W(F("ignored bookkeeping path '%s'") % *i);
      else 
        paths.push_back(file_path_external(*i));
    }
  // "it should not be the case that args were passed, but our paths set
  // ended up empty".  This test is because some commands have default
  // behavior for empty path sets -- in particular, it is the same as having
  // no restriction at all.  "mtn revert _MTN" turning into "mtn revert"
  // would be bad.  (Or substitute diff, etc.)
  N(!(!args.empty() && paths.empty()),
    F("all arguments given were bookkeeping paths; aborting"));
  return paths;
}

std::string
describe_revision(database & db,
                  revision_id const & id);

void
complete(database & db,
         std::string const & str,
         revision_id & completion,
         bool must_exist=true);

void
complete(database & db,
         std::string const & str,
         std::set<revision_id> & completion,
         bool must_exist=true);

void
notify_if_multiple_heads(database & db);

void
process_commit_message_args(bool & given,
                            utf8 & log_message,
                            app_state & app,
                            utf8 message_prefix = utf8(""));

#define CMD_FWD_DECL(C) \
namespace commands { \
  class cmd_ ## C; \
  extern cmd_ ## C C ## _cmd; \
}

#define CMD_REF(C) ((commands::command *)&(commands::C ## _cmd))

#define _CMD2(C, name, aliases, parent, hidden, params, abstract, desc, opts) \
namespace commands {                                                 \
  class cmd_ ## C : public command                                   \
  {                                                                  \
  public:                                                            \
    cmd_ ## C() : command(name, aliases, parent, hidden, params,     \
                          abstract, desc, true,                      \
                          options::options_type() | opts)            \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      command_id const & execid,                     \
                      args_vector const & args);                     \
  };                                                                 \
  cmd_ ## C C ## _cmd;                                               \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               command_id const & execid,            \
                               args_vector const & args)

#define CMD(C, name, aliases, parent, params, abstract, desc, opts) \
  _CMD2(C, name, aliases, parent, false, params, abstract, desc, opts)

#define CMD_HIDDEN(C, name, aliases, parent, params, abstract, desc, opts) \
  _CMD2(C, name, aliases, parent, true, params, abstract, desc, opts)

#define CMD_GROUP(C, name, aliases, parent, abstract, desc)          \
namespace commands {                                                 \
  class cmd_ ## C : public command                                   \
  {                                                                  \
  public:                                                            \
    cmd_ ## C() : command(name, aliases, parent, false, "", abstract,\
                          desc, true,                                \
                          options::options_type())                   \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      command_id const & execid,                     \
                      args_vector const & args);                     \
  };                                                                 \
  cmd_ ## C C ## _cmd;                                               \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               command_id const & execid,            \
                               args_vector const & args)             \
{                                                                    \
  I(false);                                                          \
}

// Use this for commands that should specifically _not_ look for an
// _MTN dir and load options from it.

#define CMD_NO_WORKSPACE(C, name, aliases, parent, params, abstract, \
                         desc, opts)                                 \
namespace commands {                                                 \
  class cmd_ ## C : public command                                   \
  {                                                                  \
  public:                                                            \
    cmd_ ## C() : command(name, aliases, parent, false, params,      \
                          abstract, desc, false,                     \
                          options::options_type() | opts)            \
    {}                                                               \
    virtual void exec(app_state & app,                               \
                      command_id const & execid,                     \
                      args_vector const & args);                     \
  };                                                                 \
  cmd_ ## C C ## _cmd;                                               \
}                                                                    \
void commands::cmd_ ## C::exec(app_state & app,                      \
                               command_id const & execid,            \
                               args_vector const & args)             \

// TODO: 'abstract' and 'desc' should be refactored so that the
// command definition allows the description of input/output format,
// error conditions, version when added, etc.  'desc' can later be
// automatically built from these.
#define CMD_AUTOMATE(C, params, abstract, desc, opts)                \
namespace commands {                                                 \
  class automate_ ## C : public automate                             \
  {                                                                  \
    void exec_from_automate(args_vector args,                        \
                            command_id const & execid,               \
                            app_state & app,                         \
                            std::ostream & output) const;            \
  public:                                                            \
    automate_ ## C() : automate(#C, params, abstract, desc,          \
                                options::options_type() | opts)      \
  public:


  struct automate_with_database
    : public automate
  {
    automate_with_database(std::string const & n, std::string const & p,
                           options::options_type const & o);

  public:
    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     database & db,
                     std::ostream & output) const = 0;

    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output) const;
  };

  struct automate_with_workspace
    : public automate
  {
    automate_with_workspace(std::string const & n, std::string const & p,
                            options::options_type const & o);

  public:
    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     workspace & work,
                     std::ostream & output) const = 0;

    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output) const;
  };

  struct automate_with_nothing
    : public automate
  {
    automate_with_nothing(std::string const & n, std::string const & p,
                          options::options_type const & o);

  public:
    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     std::ostream & output) const = 0;

    virtual void run(std::vector<utf8> args,
                     std::string const & help_name,
                     app_state & app,
                     std::ostream & output) const;
  };

    {}                                                               \
  };                                                                 \
  automate_ ## C C ## _automate;                                     \
}                                                                    \
void commands::automate_ ## C :: exec_from_automate                  \
  (args_vector args,                                                 \
   command_id const & execid,                                        \
   app_state & app,                                                  \
   std::ostream & output) const

CMD_FWD_DECL(__root__);
namespace automation {                                              \
  struct auto_ ## NAME : public automate_with_database              \
  {                                                                 \
    auto_ ## NAME ()                                                \
      : automate_with_database(#NAME, PARAMS,                       \
                                options::options_type() | OPTIONS)  \
    {}                                                              \
    void run(std::vector<utf8> args, std::string const & help_name, \
                       database & db, std::ostream & output) const; \
    virtual ~auto_ ## NAME() {}                                     \
  };                                                                \
  static auto_ ## NAME NAME ## _auto;                               \
}                                                                   \
void automation::auto_ ## NAME :: run(std::vector<utf8> args,       \
                                      std::string const & help_name,\
                                      database & db,                \
                                      std::ostream & output) const

CMD_FWD_DECL(automation);
CMD_FWD_DECL(database);
CMD_FWD_DECL(debug);
CMD_FWD_DECL(informative);
CMD_FWD_DECL(key_and_cert);
CMD_FWD_DECL(network);
CMD_FWD_DECL(packet_io);
CMD_FWD_DECL(rcs);
CMD_FWD_DECL(review);
CMD_FWD_DECL(tree);
CMD_FWD_DECL(variables);
CMD_FWD_DECL(workspace);

#define AUTOMATE_WITH_WORKSPACE(NAME, PARAMS, OPTIONS)              \
namespace automation {                                              \
  struct auto_ ## NAME : public automate_with_workspace             \
  {                                                                 \
    auto_ ## NAME ()                                                \
      : automate_with_workspace(#NAME, PARAMS,                      \
                                options::options_type() | OPTIONS)  \
    {}                                                              \
    void run(std::vector<utf8> args, std::string const & help_name, \
                    workspace & work, std::ostream & output) const; \
    virtual ~auto_ ## NAME() {}                                     \
  };                                                                \
  static auto_ ## NAME NAME ## _auto;                               \
}                                                                   \
void automation::auto_ ## NAME :: run(std::vector<utf8> args,       \
                                      std::string const & help_name,\
                                      workspace & work,             \
                                      std::ostream & output) const

#define AUTOMATE_WITH_NOTHING(NAME, PARAMS, OPTIONS)                \
namespace automation {                                              \
  struct auto_ ## NAME : public automate_with_nothing               \
  {                                                                 \
    auto_ ## NAME ()                                                \
      : automate_with_nothing(#NAME, PARAMS,                        \
                                options::options_type() | OPTIONS)  \
    {}                                                              \
    void run(std::vector<utf8> args, std::string const & help_name, \
                                      std::ostream & output) const; \
    virtual ~auto_ ## NAME() {}                                     \
  };                                                                \
  static auto_ ## NAME NAME ## _auto;                               \
}                                                                   \
void automation::auto_ ## NAME :: run(std::vector<utf8> args,       \
                                      std::string const & help_name,\
                                      std::ostream & output) const

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
