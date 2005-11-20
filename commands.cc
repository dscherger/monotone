// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/tokenizer.hpp>

#include "commands.hh"
#include "constants.hh"

#include "app_state.hh"
#include "automate.hh"
#include "basic_io.hh"
#include "cert.hh"
#include "database_check.hh"
#include "diff_patch.hh"
#include "file_io.hh"
#include "keys.hh"
#include "manifest.hh"
#include "netsync.hh"
#include "packet.hh"
#include "rcs_import.hh"
#include "restrictions.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "update.hh"
#include "vocab.hh"
#include "work.hh"
#include "automate.hh"
#include "inodeprint.hh"
#include "platform.hh"
#include "selectors.hh"
#include "annotate.hh"
#include "options.hh"
#include "globish.hh"
#include "paths.hh"
#include "merge.hh"
#include "roster_merge.hh"
#include "roster.hh"

//
// this file defines the task-oriented "top level" commands which can be
// issued as part of a monotone command line. the command line can only
// have one such command on it, followed by a vector of strings which are its
// arguments. all --options will be processed by the main program *before*
// calling a command
//
// we might expose this blunt command interface to scripting someday. but
// not today.

namespace commands 
{
  struct command;
  bool operator<(command const & self, command const & other);
};

namespace std
{
  template <>
  struct greater<commands::command *>
  {
    bool operator()(commands::command const * a, commands::command const * b)
    {
      return *a < *b;
    }
  };
};

namespace commands 
{
  using namespace std;

  struct command; 

  static map<string,command *> cmds;

  struct no_opts {};

  struct command_opts
  {
    set<int> opts;
    command_opts() {}
    command_opts & operator%(int o)
    { opts.insert(o); return *this; }
    command_opts & operator%(no_opts o)
    { return *this; }
    command_opts & operator%(command_opts const &o)
    { opts.insert(o.opts.begin(), o.opts.end()); return *this; }
  };

  struct command 
  {
    // NB: these strings are stred _un_translated
    // because we cannot translate them until after main starts, by which time
    // the command objects have all been constructed.
    string name;
    string cmdgroup;
    string params;
    string desc;
    command_opts options;
    command(string const & n,
            string const & g,
            string const & p,
            string const & d,
            command_opts const & o)
      : name(n), cmdgroup(g), params(p), desc(d), options(o)
    { cmds[n] = this; }
    virtual ~command() {}
    virtual void exec(app_state & app, vector<utf8> const & args) = 0;
  };

  bool operator<(command const & self, command const & other)
  {
    // *twitch*
    return ((std::string(_(self.cmdgroup.c_str())) < std::string(_(other.cmdgroup.c_str())))
            || ((self.cmdgroup == other.cmdgroup)
                && (std::string(_(self.name.c_str())) < (std::string(_(other.name.c_str()))))));
  }


  string complete_command(string const & cmd) 
  {
    if (cmd.length() == 0 || cmds.find(cmd) != cmds.end()) return cmd;

    L(F("expanding command '%s'\n") % cmd);

    vector<string> matched;

    for (map<string,command *>::const_iterator i = cmds.begin();
         i != cmds.end(); ++i)
      {
        if (cmd.length() < i->first.length())
          {
            string prefix(i->first, 0, cmd.length());
            if (cmd == prefix) matched.push_back(i->first);
          }
      }

    if (matched.size() == 1) 
      {
      string completed = *matched.begin();
      L(F("expanded command to '%s'\n") %  completed);  
      return completed;
      }
    else if (matched.size() > 1) 
      {
      string err = (F("command '%s' has multiple ambiguous expansions:\n") % cmd).str();
      for (vector<string>::iterator i = matched.begin();
           i != matched.end(); ++i)
        err += (*i + "\n");
      W(boost::format(err));
    }

    return cmd;
  }

  const char * safe_gettext(const char * msgid)
  {
    if (strlen(msgid) == 0)
      return msgid;

    return _(msgid);
  }

  void explain_usage(string const & cmd, ostream & out)
  {
    map<string,command *>::const_iterator i;

    // try to get help on a specific command

    i = cmds.find(cmd);

    if (i != cmds.end())
      {
        string params = safe_gettext(i->second->params.c_str());
        vector<string> lines;
        split_into_lines(params, lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "     " << i->second->name << " " << *j << endl;
        split_into_lines(safe_gettext(i->second->desc.c_str()), lines);
        for (vector<string>::const_iterator j = lines.begin();
             j != lines.end(); ++j)
          out << "       " << *j << endl;
        out << endl;
        return;
      }

    vector<command *> sorted;
    out << _("commands:") << endl;
    for (i = cmds.begin(); i != cmds.end(); ++i)
      {
        sorted.push_back(i->second);
      }
  
    sort(sorted.begin(), sorted.end(), std::greater<command *>());

    string curr_group;
    size_t col = 0;
    size_t col2 = 0;
    for (size_t i = 0; i < sorted.size(); ++i)
      {
        size_t cmp = display_width(utf8(safe_gettext(idx(sorted, i)->cmdgroup.c_str())));
        col2 = col2 > cmp ? col2 : cmp;
      }

    for (size_t i = 0; i < sorted.size(); ++i)
      {
        if (idx(sorted, i)->cmdgroup != curr_group)
          {
            curr_group = idx(sorted, i)->cmdgroup;
            out << endl;
            out << "  " << safe_gettext(idx(sorted, i)->cmdgroup.c_str());
            col = display_width(utf8(safe_gettext(idx(sorted, i)->cmdgroup.c_str()))) + 2;
            while (col++ < (col2 + 3))
              out << ' ';
          }
        out << " " << idx(sorted, i)->name;
        col += idx(sorted, i)->name.size() + 1;
        if (col >= 70)
          {
            out << endl;
            col = 0;
            while (col++ < (col2 + 3))
              out << ' ';
          }
      }
    out << endl << endl;
  }

  int process(app_state & app, string const & cmd, vector<utf8> const & args)
  {
    if (cmds.find(cmd) != cmds.end())
      {
        L(F("executing command '%s'\n") % cmd);
        cmds[cmd]->exec(app, args);
        return 0;
      }
    else
      {
        ui.inform(F("unknown command '%s'\n") % cmd);
        return 1;
      }
  }

  set<int> command_options(string const & cmd)
  {
    if (cmds.find(cmd) != cmds.end())
      {
        return cmds[cmd]->options.opts;
      }
    else
      {
        return set<int>();
      }
  }

static const no_opts OPT_NONE = no_opts();

#define CMD(C, group, params, desc, opts)                            \
struct cmd_ ## C : public command                                    \
{                                                                    \
  cmd_ ## C() : command(#C, group, params, desc,                     \
                        command_opts() % opts)                       \
  {}                                                                 \
  virtual void exec(app_state & app,                                 \
                    vector<utf8> const & args);                      \
};                                                                   \
static cmd_ ## C C ## _cmd;                                          \
void cmd_ ## C::exec(app_state & app,                                \
                     vector<utf8> const & args)                      \

#define ALIAS(C, realcommand)                                        \
CMD(C, realcommand##_cmd.cmdgroup, realcommand##_cmd.params,         \
    realcommand##_cmd.desc + "\nAlias for " #realcommand,            \
    realcommand##_cmd.options)                                       \
{                                                                    \
  process(app, string(#realcommand), args);                          \
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
    file << get_process_id();
    file.flush();
  }

  ~pid_file()
  {
    if (path.empty())
      return;
    pid_t pid;
    std::ifstream(path.as_external().c_str()) >> pid;
    if (pid == get_process_id()) {
      file.close();
      delete_file(path);
    }
  }

private:
  std::ofstream file;
  system_path path;
};


CMD(help, N_("informative"), N_("command [ARGS...]"), N_("display command help"), OPT_NONE)
{
        if (args.size() < 1)
                throw usage("");

        string full_cmd = complete_command(idx(args, 0)());
        if (cmds.find(full_cmd) == cmds.end())
                throw usage("");

        throw usage(full_cmd);
}

static void
maybe_update_inodeprints(app_state & app)
{
  if (!in_inodeprints_mode())
    return;
  inodeprint_map ipm_new;
  revision_set rev;
  roster_t old_roster, new_roster;
  get_unrestricted_working_revision_and_rosters(app, rev, 
                                                old_roster, 
                                                new_roster);
  
  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      node_id nid = i->first;
      if (old_roster.has_node(nid))
        {
          node_t old_node = old_roster.get_node(nid);
          if (is_file_t(old_node))
            {
              node_t new_node = i->second;
              I(is_file_t(new_node));

              file_t old_file = downcast_to_file_t(old_node);
              file_t new_file = downcast_to_file_t(new_node);

              if (new_file->content == old_file->content)
                {
                  split_path sp;
                  new_roster.get_name(nid, sp);
                  file_path fp(sp);                  
                  hexenc<inodeprint> ip;
                  if (inodeprint_file(fp, ip))
                    ipm_new.insert(inodeprint_entry(fp, ip));
                }
            }
        }
    }
  data dat;
  write_inodeprint_map(ipm_new, dat);
  write_inodeprints(dat);
}

static string 
get_stdin()
{
  char buf[constants::bufsz];
  string tmp;
  while(cin)
    {
      cin.read(buf, constants::bufsz);
      tmp.append(buf, cin.gcount());
    }
  return tmp;
}

static void 
get_log_message(revision_set const & cs, 
                app_state & app,
                string & log_message)
{
  string commentary;
  data summary, user_log_message;
  write_revision_set(cs, summary);
  read_user_log(user_log_message);
  commentary += "----------------------------------------------------------------------\n";
  commentary += _("Enter a description of this change.\n"
                  "Lines beginning with `MT:' are removed automatically.\n");
  commentary += "\n";
  commentary += summary();
  commentary += "----------------------------------------------------------------------\n";

  N(app.lua.hook_edit_comment(commentary, user_log_message(), log_message),
    F("edit of log message failed"));
}

static void
notify_if_multiple_heads(app_state & app) {
  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  if (heads.size() > 1) {
    std::string prefixedline;
    prefix_lines_with(_("note: "),
                      _("branch '%s' has multiple heads\n"
                        "perhaps consider 'monotone merge'"),
                      prefixedline);
    P(boost::format(prefixedline) % app.branch_name);
  }
}

static string
describe_revision(app_state & app, revision_id const & id)
{
  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);

  string description;

  description += id.inner()();

  // append authors and date of this revision
  vector< revision<cert> > tmp;
  app.db.get_revision_certs(id, author_name, tmp);
  erase_bogus_certs(tmp, app);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      description += " ";
      description += tv();
    }
  app.db.get_revision_certs(id, date_name, tmp);
  erase_bogus_certs(tmp, app);
  for (vector< revision<cert> >::const_iterator i = tmp.begin();
       i != tmp.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      description += " ";
      description += tv();
    }

  return description;
}

static void 
complete(app_state & app, 
         string const & str,
         revision_id & completion,
         bool must_exist=true)
{
  // This copies the start of selectors::parse_selector().to avoid
  // getting a log when there's no expansion happening...:
  //
  // this rule should always be enabled, even if the user specifies
  // --norc: if you provide a revision id, you get a revision id.
  if (str.find_first_not_of(constants::legal_id_bytes) == string::npos
      && str.size() == constants::idlen)
    {
      completion = revision_id(str);
      if (must_exist)
        N(app.db.revision_exists(completion),
          F("no such revision '%s'") % completion);
      return;
    }

  vector<pair<selectors::selector_type, string> >
    sels(selectors::parse_selector(str, app));

  P(F("expanding selection '%s'\n") % str);

  // we jam through an "empty" selection on sel_ident type
  set<string> completions;
  selectors::selector_type ty = selectors::sel_ident;
  selectors::complete_selector("", sels, ty, completions, app);

  N(completions.size() != 0,
    F("no match for selection '%s'") % str);
  if (completions.size() > 1)
    {
      string err = (F("selection '%s' has multiple ambiguous expansions: \n") % str).str();
      for (set<string>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += (describe_revision(app, revision_id(*i)) + "\n");
      N(completions.size() == 1, boost::format(err));
    }
  completion = revision_id(*(completions.begin()));  
  P(F("expanded to '%s'\n") %  completion);  
}


template<typename ID>
static void 
complete(app_state & app, 
         string const & str,
         ID & completion)
{
  N(str.find_first_not_of(constants::legal_id_bytes) == string::npos,
    F("non-hex digits in id"));
  if (str.size() == constants::idlen)
    {
      completion = ID(str);
      return;
    }
  set<ID> completions;
  app.db.complete(str, completions);
  N(completions.size() != 0,
    F("partial id '%s' does not have an expansion") % str);
  if (completions.size() > 1)
    {
      string err = (F("partial id '%s' has multiple ambiguous expansions:\n") % str).str();
      for (typename set<ID>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        err += (i->inner()() + "\n");
      N(completions.size() == 1, boost::format(err));
    }
  completion = *(completions.begin());  
  P(F("expanded partial id '%s' to '%s'\n")
    % str % completion);
}

static void 
ls_certs(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 1)
    throw usage(name);

  vector<cert> certs;
  
  transaction_guard guard(app.db);
  
  revision_id ident;
  complete(app, idx(args, 0)(), ident);
  vector< revision<cert> > ts;
  app.db.get_revision_certs(ident, ts);
  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    set<rsa_keypair_id> checked;      
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !app.db.public_key_exists(idx(certs, i).key))
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
      colon_pos = display_width(substr);
      extra_str = string(colon_pos, ' ') + ": %s\n";
    }

  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_status status = check_cert(app, idx(certs, i));
      cert_value tv;      
      decode_base64(idx(certs, i).value, tv);
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
      I(lines.size() > 0);

      cout << std::string(guess_terminal_width(), '-') << '\n'
           << boost::format(str)
        % idx(certs, i).key()
        % stat
        % idx(certs, i).name()
        % idx(lines, 0);
      
      for (size_t i = 1; i < lines.size(); ++i)
        cout << boost::format(extra_str) % idx(lines, i);
    }  

  if (certs.size() > 0)
    cout << endl;

  guard.commit();
}

static void
ls_keys(string const & name, app_state & app, vector<utf8> const & args)
{
  vector<rsa_keypair_id> pubs;
  vector<rsa_keypair_id> privkeys;
  std::string pattern;
  if (args.size() == 1)
    pattern = idx(args, 0)();
  else if (args.size() > 1)
    throw usage(name);

  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      app.db.get_key_ids(pattern, pubs);
      guard.commit();
    }
  app.keys.get_key_ids(pattern, privkeys);

  // true if it is in the database, false otherwise
  map<rsa_keypair_id, bool> pubkeys;
  for (vector<rsa_keypair_id>::const_iterator i = pubs.begin();
       i != pubs.end(); i++)
    pubkeys[*i] = true;
  
  bool all_in_db = true;
  for (vector<rsa_keypair_id>::const_iterator i = privkeys.begin();
       i != privkeys.end(); i++)
    {
      if (pubkeys.find(*i) == pubkeys.end())
        {
          pubkeys[*i] = false;
          all_in_db = false;
        }
    }

  if (pubkeys.size() > 0)
    {
      cout << endl << "[public keys]" << endl;
      for (map<rsa_keypair_id, bool>::iterator i = pubkeys.begin();
           i != pubkeys.end(); i++)
        {
          base64<rsa_pub_key> pub_encoded;
          hexenc<id> hash_code;
          rsa_keypair_id keyid = i->first;
          bool indb = i->second;

          if (indb)
            app.db.get_key(keyid, pub_encoded); 
          else
            {
              keypair kp;
              app.keys.get_key_pair(keyid, kp);
              pub_encoded = kp.pub;
            }
          key_hash_code(keyid, pub_encoded, hash_code);
          if (indb)
            cout << hash_code << " " << keyid << endl;
          else
            cout << hash_code << " " << keyid << "   (*)" << endl;
        }
      if (!all_in_db)
        cout << F("(*) - only in %s/") % app.keys.get_key_dir() << endl;
      cout << endl;
    }

  if (privkeys.size() > 0)
    {
      cout << endl << "[private keys]" << endl;
      for (vector<rsa_keypair_id>::iterator i = privkeys.begin();
           i != privkeys.end(); i++)
        {
          keypair kp;
          hexenc<id> hash_code;
          app.keys.get_key_pair(*i, kp); 
          key_hash_code(*i, kp.priv, hash_code);
          cout << hash_code << " " << *i << endl;
        }
      cout << endl;
    }

  if (pubkeys.size() == 0 &&
      privkeys.size() == 0)
    {
      if (args.size() == 0)
        P(F("no keys found\n"));
      else
        W(F("no keys found matching '%s'\n") % idx(args, 0)());
    }
}

// Deletes a revision from the local database.  This can be used to 'undo' a
// changed revision from a local database without leaving (much of) a trace.
static void
kill_rev_locally(app_state& app, std::string const& id)
{
  revision_id ident;
  complete(app, id, ident);
  N(app.db.revision_exists(ident),
    F("no such revision '%s'") % ident);

  //check that the revision does not have any children
  set<revision_id> children;
  app.db.get_revision_children(ident, children);
  N(!children.size(),
    F("revision %s already has children. We cannot kill it.") % ident);

  app.db.delete_existing_rev_and_certs(ident);
}

// The changes_summary structure holds a list all of files and directories
// affected in a revision, and is useful in the 'log' command to print this
// information easily.  It has to be constructed from all cset objects
// that belong to a revision.

struct
changes_summary
{
  cset cs;
  changes_summary(void);
  void add_change_set(cset const & cs);
  void print(std::ostream & os, size_t max_cols) const;
};

changes_summary::changes_summary(void) 
{
}

void
changes_summary::add_change_set(cset const & c)
{
  if (cs.empty())
    return;

  // FIXME: not sure whether it matters for an informal summary
  // object like this, but the pre-state names in deletes and renames
  // are not really sensible to union; they refer to different trees
  // so mixing them up in a single set is potentially ambiguous.

  copy(c.nodes_deleted.begin(), c.nodes_deleted.end(), 
       inserter(cs.nodes_deleted, cs.nodes_deleted.begin()));

  copy(c.files_added.begin(), c.files_added.end(), 
       inserter(cs.files_added, cs.files_added.begin()));

  copy(c.dirs_added.begin(), c.dirs_added.end(), 
       inserter(cs.dirs_added, cs.dirs_added.begin()));

  copy(c.nodes_renamed.begin(), c.nodes_renamed.end(), 
       inserter(cs.nodes_renamed, cs.nodes_renamed.begin()));

  copy(c.deltas_applied.begin(), c.deltas_applied.end(), 
       inserter(cs.deltas_applied, cs.deltas_applied.begin()));

  copy(c.attrs_cleared.begin(), c.attrs_cleared.end(), 
       inserter(cs.attrs_cleared, cs.attrs_cleared.begin()));

  copy(c.attrs_set.begin(), c.attrs_set.end(), 
       inserter(cs.attrs_set, cs.attrs_set.begin()));
}

static void 
print_indented_set(std::ostream & os, 
                   path_set const & s,
                   size_t max_cols)
{
  size_t cols = 8;
  os << "       ";
  for (path_set::const_iterator i = s.begin();
       i != s.end(); i++)
    {
      const std::string str = boost::lexical_cast<std::string>(file_path(*i));
      if (cols > 8 && cols + str.size() + 1 >= max_cols)
        {
          cols = 8;
          os << endl << "       "; 
        }
      os << " " << str;
      cols += str.size() + 1;
    }
  os << endl;
}

void
changes_summary::print(std::ostream & os, size_t max_cols) const
{

  if (! cs.nodes_deleted.empty())
    {
      os << "Deleted entries:" << endl;
      print_indented_set(os, cs.nodes_deleted, max_cols);
    }
  
  if (! cs.nodes_renamed.empty())
    {
      os << "Renamed entries:" << endl;
      for (std::map<split_path, split_path>::const_iterator
           i = cs.nodes_renamed.begin();
           i != cs.nodes_renamed.end(); i++)
        os << "        " << file_path(i->first) << " to " << file_path(i->second) << endl;
    }

  if (! cs.files_added.empty())
    {
      path_set tmp;
      for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
           i != cs.files_added.end(); ++i)
        tmp.insert(i->first);
      os << "Added files:" << endl;
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.dirs_added.empty())
    {
      os << "Added directories:" << endl;
      print_indented_set(os, cs.dirs_added, max_cols);
    }

  if (! cs.deltas_applied.empty())
    {
      path_set tmp;
      for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
           i != cs.deltas_applied.end(); ++i)
        tmp.insert(i->first);
      os << "Modified files:" << endl;
      print_indented_set(os, tmp, max_cols);
    }

  if (! cs.attrs_set.empty() || ! cs.attrs_cleared.empty())
    {
      path_set tmp;
      for (std::set<std::pair<split_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
           i != cs.attrs_cleared.end(); ++i)
        tmp.insert(i->first);

      for (std::map<std::pair<split_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
           i != cs.attrs_set.end(); ++i)
        tmp.insert(i->first.first);

      os << "Modified attrs:" << endl;
      print_indented_set(os, tmp, max_cols);
    }
}


CMD(genkey, N_("key and cert"), N_("KEYID"), N_("generate an RSA key-pair"), OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);
  bool exists = app.keys.key_pair_exists(ident);
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      exists = exists || app.db.public_key_exists(ident);
      guard.commit();
    }

  N(!exists, F("key '%s' already exists") % ident);
  
  keypair kp;
  P(F("generating key-pair '%s'") % ident);
  generate_key_pair(app.lua, ident, kp);
  P(F("storing key-pair '%s' in %s/") % ident % app.keys.get_key_dir());
  app.keys.put_key_pair(ident, kp);
}

CMD(dropkey, N_("key and cert"), N_("KEYID"), N_("drop a public and private key"), OPT_NONE)
{
  bool key_deleted = false;
  
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident(idx(args, 0)());
  bool checked_db = false;
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db);
      if (app.db.public_key_exists(ident))
        {
          P(F("dropping public key '%s' from database\n") % ident);
          app.db.delete_public_key(ident);
          key_deleted = true;
        }
      guard.commit();
      checked_db = true;
    }

  if (app.keys.key_pair_exists(ident))
    {
      P(F("dropping key pair '%s' from keystore\n") % ident);
      app.keys.delete_key(ident);
      key_deleted = true;
    }

  boost::format fmt;
  if (checked_db)
    fmt = F("public or private key '%s' does not exist in keystore or database");
  else
    fmt = F("public or private key '%s' does not exist in keystore, and no database was specified");
  N(key_deleted, fmt % idx(args, 0)());
}

CMD(chkeypass, N_("key and cert"), N_("KEYID"),
    N_("change passphrase of a private RSA key"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  N(app.keys.key_pair_exists(ident),
    F("key '%s' does not exist in the keystore") % ident);

  keypair key;
  app.keys.get_key_pair(ident, key);
  change_key_passphrase(app.lua, ident, key.priv);
  app.keys.delete_key(ident);
  app.keys.put_key_pair(ident, key);
  P(F("passphrase changed\n"));
}

CMD(cert, N_("key and cert"), N_("REVISION CERTNAME [CERTVAL]"),
    N_("create a cert for a revision"), OPT_NONE)
{
  if ((args.size() != 3) && (args.size() != 2))
    throw usage(name);

  transaction_guard guard(app.db);

  hexenc<id> ident;
  revision_id rid;
  complete(app, idx(args, 0)(), rid);
  ident = rid.inner();
  
  cert_name name;
  internalize_cert_name(idx(args, 1), name);

  rsa_keypair_id key;
  get_user_key(key, app);
  
  cert_value val;
  if (args.size() == 3)
    val = cert_value(idx(args, 2)());
  else
    val = cert_value(get_stdin());

  base64<cert_value> val_encoded;
  encode_base64(val, val_encoded);

  cert t(ident, name, val_encoded, key);

  packet_db_writer dbw(app);
  calculate_cert(app, t);
  dbw.consume_revision_cert(revision<cert>(t));
  guard.commit();
}

CMD(trusted, N_("key and cert"), N_("REVISION NAME VALUE SIGNER1 [SIGNER2 [...]]"),
    N_("test whether a hypothetical cert would be trusted\n"
      "by current settings"),
    OPT_NONE)
{
  if (args.size() < 4)
    throw usage(name);

  revision_id rid;
  complete(app, idx(args, 0)(), rid, false);
  hexenc<id> ident(rid.inner());
  
  cert_name name;
  internalize_cert_name(idx(args, 1), name);
  
  cert_value value(idx(args, 2)());

  set<rsa_keypair_id> signers;
  for (unsigned int i = 3; i != args.size(); ++i)
    {
      rsa_keypair_id keyid;
      internalize_rsa_keypair_id(idx(args, i), keyid);
      signers.insert(keyid);
    }
  
  
  bool trusted = app.lua.hook_get_revision_cert_trust(signers, ident,
                                                      name, value);


  ostringstream all_signers;
  copy(signers.begin(), signers.end(),
       ostream_iterator<rsa_keypair_id>(all_signers, " "));

  cout << F("if a cert on: %s\n"
            "with key: %s\n"
            "and value: %s\n"
            "was signed by: %s\n"
            "it would be: %s\n")
    % ident
    % name
    % value
    % all_signers.str()
    % (trusted ? _("trusted") : _("UNtrusted"));
}

CMD(tag, N_("review"), N_("REVISION TAGNAME"),
    N_("put a symbolic tag cert on a revision version"), OPT_NONE)
{
  if (args.size() != 2)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_tag(r, idx(args, 1)(), app, dbw);
}


CMD(testresult, N_("review"), N_("ID (pass|fail|true|false|yes|no|1|0)"), 
    N_("note the results of running a test on a revision"), OPT_NONE)
{
  if (args.size() != 2)
    throw usage(name);

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_testresult(r, idx(args, 1)(), app, dbw);
}

CMD(approve, N_("review"), N_("REVISION"), 
    N_("approve of a particular revision"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);  

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_value branchname;
  guess_branch(r, app, branchname);
  N(app.branch_name() != "", F("need --branch argument for approval"));  
  cert_revision_in_branch(r, app.branch_name(), app, dbw);
}

/*
// FIXME_ROSTERS: disabled until rewritten to use rosters
CMD(disapprove, N_("review"), N_("REVISION"), 
    N_("disapprove of a particular revision"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);

  revision_id r;
  revision_set rev, rev_inverse;
  boost::shared_ptr<change_set> cs_inverse(new change_set());
  complete(app, idx(args, 0)(), r);
  app.db.get_revision(r, rev);

  N(rev.edges.size() == 1, 
    F("revision '%s' has %d changesets, cannot invert\n") % r % rev.edges.size());

  cert_value branchname;
  guess_branch(r, app, branchname);
  N(app.branch_name() != "", F("need --branch argument for disapproval"));  
  
  edge_entry const & old_edge (*rev.edges.begin());
  rev_inverse.new_manifest = edge_old_manifest(old_edge);
  manifest_map m_old;
  app.db.get_manifest(edge_old_manifest(old_edge), m_old);
  invert_change_set(edge_changes(old_edge), m_old, *cs_inverse);
  rev_inverse.edges.insert(make_pair(r, make_pair(rev.new_manifest, cs_inverse)));

  {
    transaction_guard guard(app.db);
    packet_db_writer dbw(app);

    revision_id inv_id;
    revision_data rdat;

    write_revision_set(rev_inverse, rdat);
    calculate_ident(rdat, inv_id);
    dbw.consume_revision_data(inv_id, rdat);
    
    cert_revision_in_branch(inv_id, branchname, app, dbw); 
    cert_revision_date_now(inv_id, app, dbw);
    cert_revision_author_default(inv_id, app, dbw);
    cert_revision_changelog(inv_id, (boost::format("disapproval of revision '%s'") % r).str(), app, dbw);
    guard.commit();
  }
}
*/

CMD(comment, N_("review"), N_("REVISION [COMMENT]"),
    N_("comment on a particular revision"), OPT_NONE)
{
  if (args.size() != 1 && args.size() != 2)
    throw usage(name);

  string comment;
  if (args.size() == 2)
    comment = idx(args, 1)();
  else
    N(app.lua.hook_edit_comment("", "", comment), 
      F("edit comment failed"));
  
  N(comment.find_first_not_of(" \r\t\n") != string::npos, 
    F("empty comment"));

  revision_id r;
  complete(app, idx(args, 0)(), r);
  packet_db_writer dbw(app);
  cert_revision_comment(r, comment, app, dbw);
}


static void find_unknown_and_ignored (app_state & app, bool want_ignored, vector<utf8> const & args, 
                                      path_set & unknown, path_set & ignored);


CMD(add, N_("working copy"), N_("[PATH]..."),
    N_("add files to working copy"), OPT_UNKNOWN)
{
  if (!app.unknown && (args.size() < 1))
    throw usage(name);

  app.require_working_copy();

  path_set paths;
  if (app.unknown)
    {
      path_set ignored;
      find_unknown_and_ignored(app, false, args, paths, ignored);
    }
  else
    for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
      {
        split_path sp;
        file_path_external(*i).split(sp);
        paths.insert(sp);
      }

  perform_additions(paths, app);
}

static void find_missing (app_state & app,
                          vector<utf8> const & args, path_set & missing);

CMD(drop, N_("working copy"), N_("[PATH]..."),
    N_("drop files from working copy"), OPT_EXECUTE % OPT_MISSING)
{
  if (!app.missing && (args.size() < 1))
    throw usage(name);

  app.require_working_copy();

  path_set paths;
  if (app.missing)
    find_missing(app, args, paths);
  else
    for (vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
      {
        split_path sp;
        file_path_external(*i).split(sp);
        paths.insert(sp);
      }

  perform_deletions(paths, app);
}

ALIAS(rm, drop);


CMD(rename, N_("working copy"), N_("SRC DST"),
    N_("rename entries in the working copy"),
    OPT_EXECUTE)
{
  if (args.size() != 2)
    throw usage(name);
  
  app.require_working_copy();

  file_path src_path = file_path_external(idx(args, 0));
  file_path dst_path = file_path_external(idx(args, 1));

  perform_rename(src_path, dst_path, app);
}

ALIAS(mv, rename)


// fload and fmerge are simple commands for debugging the line
// merger.

CMD(fload, N_("debug"), "", N_("load file contents into db"), OPT_NONE)
{
  string s = get_stdin();

  file_id f_id;
  file_data f_data(s);
  
  calculate_ident (f_data, f_id);
  
  packet_db_writer dbw(app);
  dbw.consume_file_data(f_id, f_data);  
}

CMD(fmerge, N_("debug"), N_("<parent> <left> <right>"),
    N_("merge 3 files and output result"),
    OPT_NONE)
{
  if (args.size() != 3)
    throw usage(name);

  file_id anc_id(idx(args, 0)()), left_id(idx(args, 1)()), right_id(idx(args, 2)());
  file_data anc, left, right;

  N(app.db.file_version_exists (anc_id),
  F("ancestor file id does not exist"));

  N(app.db.file_version_exists (left_id),
  F("left file id does not exist"));

  N(app.db.file_version_exists (right_id),
  F("right file id does not exist"));

  app.db.get_file_version(anc_id, anc);
  app.db.get_file_version(left_id, left);
  app.db.get_file_version(right_id, right);

  vector<string> anc_lines, left_lines, right_lines, merged_lines;

  split_into_lines(anc.inner()(), anc_lines);
  split_into_lines(left.inner()(), left_lines);
  split_into_lines(right.inner()(), right_lines);
  N(merge3(anc_lines, left_lines, right_lines, merged_lines), F("merge failed"));
  copy(merged_lines.begin(), merged_lines.end(), ostream_iterator<string>(cout, "\n"));
  
}

CMD(status, N_("informative"), N_("[PATH]..."), N_("show status of working copy"),
    OPT_DEPTH % OPT_BRIEF)
{
  revision_set rs;
  roster_t old_roster, new_roster;
  data tmp;

  app.require_working_copy();
  get_working_revision_and_rosters(app, args, rs, old_roster, new_roster);

  if (global_sanity.brief)
    {
      I(rs.edges.size() == 1);
      cset const & cs = edge_changes(rs.edges.begin());
      
      for (path_set::const_iterator i = cs.nodes_deleted.begin();
           i != cs.nodes_deleted.end(); ++i) 
        cout << "dropped " << *i << endl;

      for (std::map<split_path, split_path>::const_iterator 
           i = cs.nodes_renamed.begin();
           i != cs.nodes_renamed.end(); ++i) 
        cout << "renamed " << i->first << endl 
             << "     to " << i->second << endl;

      for (path_set::const_iterator i = cs.dirs_added.begin();
           i != cs.dirs_added.end(); ++i) 
        cout << "added   " << *i << endl;

      for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
           i != cs.files_added.end(); ++i) 
        cout << "added   " << i->first << endl;

      for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
             i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i) 
        {
          cout << "patched " << i->first << endl;
        }
    }
  else
    {
      write_revision_set(rs, tmp);
      cout << endl << tmp << endl;
    }
}


CMD(identify, N_("working copy"), N_("[PATH]"),
    N_("calculate identity of PATH or stdin"),
    OPT_NONE)
{
  if (!(args.size() == 0 || args.size() == 1))
    throw usage(name);

  data dat;

  if (args.size() == 1)
    {
      read_localized_data(file_path_external(idx(args, 0)), dat, app.lua);
    }
  else
    {
      dat = get_stdin();
    }
  
  hexenc<id> ident;
  calculate_ident(dat, ident);
  cout << ident << endl;
}

CMD(cat, N_("informative"),
    N_("FILENAME"),
    N_("write file from database to stdout"),
    OPT_REVISION)
{
  if (args.size() != 1)
    throw usage(name);

  if (app.revision_selectors.size() == 0)
    app.require_working_copy();

  transaction_guard guard(app.db);

  revision_id rid;
  if (app.revision_selectors.size() == 0)
    get_revision_id(rid);
  else 
    complete(app, idx(app.revision_selectors, 0)(), rid);
  N(app.db.revision_exists(rid), F("no such revision '%s'") % rid);

  // paths are interpreted as standard external ones when we're in a
  // working copy, but as project-rooted external ones otherwise
  file_path fp;
  split_path sp;
  fp = file_path_external(idx(args, 0));
  fp.split(sp);

  roster_t roster;
  marking_map marks;
  app.db.get_roster(rid, roster, marks);
  node_t node = roster.get_node(sp);
  N((!null_node(node->self) && is_file_t(node)), F("no file '%s' found in revision '%s'\n") % fp % rid);

  file_t file_node = downcast_to_file_t(node);
  file_id ident = file_node->content;  
  file_data dat;
  L(F("dumping file '%s'\n") % ident);
  app.db.get_file_version(ident, dat);
  cout.write(dat.inner()().data(), dat.inner()().size());

  guard.commit();
}

CMD(checkout, N_("tree"), N_("[DIRECTORY]\n"),
    N_("check out a revision from database into directory.\n"
    "If a revision is given, that's the one that will be checked out.\n"
    "Otherwise, it will be the head of the branch (given or implicit).\n"
    "If no directory is given, the branch name will be used as directory"),
    OPT_BRANCH_NAME % OPT_REVISION)
{
  revision_id ident;
  system_path dir;
  // we have a special case for "checkout .", i.e., to current dir
  bool checkout_dot = false;

  if (args.size() > 1 || app.revision_selectors.size() > 1)
    throw usage(name);

  if (args.size() == 0)
    {
      // no checkout dir specified, use branch name for dir
      N(!app.branch_name().empty(), F("need --branch argument for branch-based checkout"));
      dir = system_path(app.branch_name());
    }
  else
    {
      // checkout to specified dir
      dir = system_path(idx(args, 0));
      if (idx(args, 0) == utf8("."))
        checkout_dot = true;
    }

  if (!checkout_dot)
    require_path_is_nonexistent(dir,
                                F("checkout directory '%s' already exists")
                                % dir);

  if (app.revision_selectors.size() == 0)
    {
      // use branch head revision
      N(!app.branch_name().empty(), F("need --branch argument for branch-based checkout"));
      set<revision_id> heads;
      get_branch_heads(app.branch_name(), app, heads);
      N(heads.size() > 0, F("branch '%s' is empty\n") % app.branch_name);
      N(heads.size() == 1, F("branch %s has multiple heads") % app.branch_name);
      ident = *(heads.begin());
    }
  else if (app.revision_selectors.size() == 1)
    {
      // use specified revision
      complete(app, idx(app.revision_selectors, 0)(), ident);
      N(app.db.revision_exists(ident),
        F("no such revision '%s'") % ident);
      
      cert_value b;
      guess_branch(ident, app, b);

      I(!app.branch_name().empty());
      cert_value branch_name(app.branch_name());
      base64<cert_value> branch_encoded;
      encode_base64(branch_name, branch_encoded);
        
      vector< revision<cert> > certs;
      app.db.get_revision_certs(ident, branch_cert_name, branch_encoded, certs);
          
      L(F("found %d %s branch certs on revision %s\n") 
        % certs.size()
        % app.branch_name
        % ident);
        
      N(certs.size() != 0, F("revision %s is not a member of branch %s\n") 
        % ident % app.branch_name);
    }

  app.create_working_copy(dir);
    
  file_data data;
  roster_t ros;
  marking_map mm;
  
  put_revision_id(ident);
  
  L(F("checking out revision %s to directory %s\n") % ident % dir);
  app.db.get_roster(ident, ros, mm);
  
  node_map const & nodes = ros.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_t node = i->second;
      split_path sp;
      ros.get_name(i->first, sp);
      file_path path(sp);

      if (is_dir_t(node))
        {
          if (sp.size() == 1)
            I(null_name(idx(sp,0)));
          else
            mkdir_p(path);
        }
      else
        {
          file_t file = downcast_to_file_t(node);
          N(app.db.file_version_exists(file->content),
            F("no file %s found in database for %s")
            % file->content % path);
      
          file_data dat;
          L(F("writing file %s to %s\n")
            % file->content % path);
          app.db.get_file_version(file->content, dat);
          write_localized_data(path, dat.inner(), app.lua);
        }
    }
  remove_work_cset();
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}

ALIAS(co, checkout)


CMD(heads, N_("tree"), "", N_("show unmerged head revisions of branch"),
    OPT_BRANCH_NAME)
{
  set<revision_id> heads;
  if (args.size() != 0)
    throw usage(name);

  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  if (heads.size() == 0)
    P(F("branch '%s' is empty\n") % app.branch_name);
  else if (heads.size() == 1)
    P(F("branch '%s' is currently merged:\n") % app.branch_name);
  else
    P(F("branch '%s' is currently unmerged:\n") % app.branch_name);
  
  for (set<revision_id>::const_iterator i = heads.begin(); 
       i != heads.end(); ++i)
    cout << describe_revision(app, *i) << endl;
}

static void 
ls_branches(string name, app_state & app, vector<utf8> const & args)
{
  vector<string> names;
  app.db.get_branches(names);

  sort(names.begin(), names.end());
  for (size_t i = 0; i < names.size(); ++i)
    if (!app.lua.hook_ignore_branch(idx(names, i)))
      cout << idx(names, i) << endl;
}

static void 
ls_epochs(string name, app_state & app, vector<utf8> const & args)
{
  std::map<cert_value, epoch_data> epochs;
  app.db.get_epochs(epochs);

  if (args.size() == 0)
    {
      for (std::map<cert_value, epoch_data>::const_iterator i = epochs.begin();
           i != epochs.end(); ++i)
        {
          cout << i->second << " " << i->first << endl;
        }
    }
  else
    {
      for (vector<utf8>::const_iterator i = args.begin(); i != args.end();
           ++i)
        {
          std::map<cert_value, epoch_data>::const_iterator j = epochs.find(cert_value((*i)()));
          N(j != epochs.end(), F("no epoch for branch %s\n") % *i);
          cout << j->second << " " << j->first << endl;
        }
    }  
}

static void 
ls_tags(string name, app_state & app, vector<utf8> const & args)
{
  vector< revision<cert> > certs;
  app.db.get_revision_certs(tag_cert_name, certs);

  std::set< pair<cert_value, pair<revision_id, rsa_keypair_id> > > sorted_vals;

  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value name;
      cert c = i->inner();
      decode_base64(c.value, name);
      sorted_vals.insert(std::make_pair(name, std::make_pair(c.ident, c.key)));
    }
  for (std::set<std::pair<cert_value, std::pair<revision_id, 
         rsa_keypair_id> > >::const_iterator i = sorted_vals.begin();
       i != sorted_vals.end(); ++i)
    {
      cout << i->first << " " 
           << i->second.first  << " "
           << i->second.second  << endl;
    }
}

static void
ls_vars(string name, app_state & app, vector<utf8> const & args)
{
  bool filterp;
  var_domain filter;
  if (args.size() == 0)
    {
      filterp = false;
    }
  else if (args.size() == 1)
    {
      filterp = true;
      internalize_var_domain(idx(args, 0), filter);
    }
  else
    throw usage(name);

  map<var_key, var_value> vars;
  app.db.get_vars(vars);
  for (std::map<var_key, var_value>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      if (filterp && !(i->first.first == filter))
        continue;
      external ext_domain, ext_name;
      externalize_var_domain(i->first.first, ext_domain);
      cout << ext_domain << ": " << i->first.second << " " << i->second << endl;
    }
}

static void
ls_known (app_state & app, vector<utf8> const & args)
{
  revision_set rs;
  roster_t old_roster, new_roster;
  data tmp;

  app.require_working_copy();

  path_set paths;
  get_working_revision_and_rosters(app, args, rs, old_roster, new_roster);
  new_roster.extract_path_set(paths);
  
  for (path_set::const_iterator p = paths.begin(); p != paths.end(); ++p)
    {
      if (app.restriction_includes(*p))
        cout << file_path(*p) << endl;
    }
}


static void
find_unknown_and_ignored (app_state & app, bool want_ignored, vector<utf8> const & args, 
                          path_set & unknown, path_set & ignored)
{
  revision_set rev;
  roster_t old_roster, new_roster;
  path_set known;

  get_working_revision_and_rosters(app, args, rev, old_roster, new_roster);
  new_roster.extract_path_set(known);

  file_itemizer u(app, known, unknown, ignored);
  walk_tree(file_path(), u);
}


static void
ls_unknown_or_ignored (app_state & app, bool want_ignored, vector<utf8> const & args)
{
  app.require_working_copy();

  path_set unknown, ignored;
  find_unknown_and_ignored(app, want_ignored, args, unknown, ignored);

  if (want_ignored)
    for (path_set::const_iterator i = ignored.begin(); i != ignored.end(); ++i)
      cout << file_path(*i) << endl;
  else
    for (path_set::const_iterator i = unknown.begin(); i != unknown.end(); ++i)
      cout << file_path(*i) << endl;
}


static void
find_missing (app_state & app, vector<utf8> const & args, path_set & missing)
{
  revision_id base_rid;
  roster_t base_roster;
  cset included_work, excluded_work;
  path_set old_paths, new_paths;

  app.require_working_copy();
  get_base_roster_and_working_cset(app, args, base_rid, base_roster,
                                   old_paths, new_paths,
                                   included_work, excluded_work);

  for (path_set::const_iterator i = new_paths.begin(); i != new_paths.end(); ++i)
    {
      if (i->size() == 1)
        {
          I(null_name(idx(*i, 0)));
          continue;
        }
      file_path fp(*i);      
      if (app.restriction_includes(*i) && !path_exists(fp))
        missing.insert(*i);
    }
}


static void
ls_missing (app_state & app, vector<utf8> const & args)
{
  path_set missing;
  find_missing(app, args, missing);

  for (path_set::const_iterator i = missing.begin(); i != missing.end(); ++i)
    {
      cout << file_path(*i) << endl;
    }
}


CMD(list, N_("informative"), 
    N_("certs ID\n"
      "keys [PATTERN]\n"
      "branches\n"
      "epochs [BRANCH [...]]\n"
      "tags\n"
      "vars [DOMAIN]\n"
      "known\n"
      "unknown\n"
      "ignored\n"
      "missing"),
    N_("show database objects, or the current working copy manifest,\n"
      "or unknown, intentionally ignored, or missing state files"),
    OPT_DEPTH)
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  ++i;
  vector<utf8> removed (i, args.end());
  if (idx(args, 0)() == "certs")
    ls_certs(name, app, removed);
  else if (idx(args, 0)() == "keys")
    ls_keys(name, app, removed);
  else if (idx(args, 0)() == "branches")
    ls_branches(name, app, removed);
  else if (idx(args, 0)() == "epochs")
    ls_epochs(name, app, removed);
  else if (idx(args, 0)() == "tags")
    ls_tags(name, app, removed);
  else if (idx(args, 0)() == "vars")
    ls_vars(name, app, removed);
  else if (idx(args, 0)() == "known")
    ls_known(app, removed);
  else if (idx(args, 0)() == "unknown")
    ls_unknown_or_ignored(app, false, removed);
  else if (idx(args, 0)() == "ignored")
    ls_unknown_or_ignored(app, true, removed);
  else if (idx(args, 0)() == "missing")
    ls_missing(app, removed);
  else
    throw usage(name);
}

ALIAS(ls, list)

CMD(fdelta, N_("packet i/o"), N_("OLDID NEWID"),
    N_("write file delta packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 2)
    throw usage(name);

  packet_writer pw(cout);

  file_id f_old_id, f_new_id;
  file_data f_old_data, f_new_data;

  complete(app, idx(args, 0)(), f_old_id);
  complete(app, idx(args, 1)(), f_new_id);

  N(app.db.file_version_exists(f_old_id), F("no such file '%s'") % f_old_id);
  app.db.get_file_version(f_old_id, f_old_data);
  N(app.db.file_version_exists(f_new_id), F("no such file '%s'") % f_new_id);
  app.db.get_file_version(f_new_id, f_new_data);
  delta del;
  diff(f_old_data.inner(), f_new_data.inner(), del);
  pw.consume_file_delta(f_old_id, f_new_id, file_delta(del));  
}

CMD(rdata, N_("packet i/o"), N_("ID"), N_("write revision data packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);

  revision_id r_id;
  revision_data r_data;

  complete(app, idx(args, 0)(), r_id);

  N(app.db.revision_exists(r_id), F("no such revision '%s'") % r_id);
  app.db.get_revision(r_id, r_data);
  pw.consume_revision_data(r_id, r_data);  
}


CMD(fdata, N_("packet i/o"), N_("ID"), N_("write file data packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);

  file_id f_id;
  file_data f_data;

  complete(app, idx(args, 0)(), f_id);

  N(app.db.file_version_exists(f_id), F("no such file '%s'") % f_id);
  app.db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id, f_data);  
}


CMD(certs, N_("packet i/o"), N_("ID"), N_("write cert packets to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  packet_writer pw(cout);

  revision_id r_id;
  vector< revision<cert> > certs;

  complete(app, idx(args, 0)(), r_id);

  app.db.get_revision_certs(r_id, certs);
  for (size_t i = 0; i < certs.size(); ++i)
    pw.consume_revision_cert(idx(certs, i));
}

CMD(pubkey, N_("packet i/o"), N_("ID"), N_("write public key packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident(idx(args, 0)());
  bool exists(false);
  base64< rsa_pub_key > key;
  if (app.db.database_specified() && app.db.public_key_exists(ident))
    {
      app.db.get_key(ident, key);
      exists = true;
    }
  if (app.keys.key_pair_exists(ident))
    {
      keypair kp;
      app.keys.get_key_pair(ident, kp);
      key = kp.pub;
      exists = true;
    }
  N(exists,
    F("public key '%s' does not exist") % idx(args, 0)());

  packet_writer pw(cout);
  pw.consume_public_key(ident, key);
}

CMD(privkey, N_("packet i/o"), N_("ID"), N_("write private key packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident(idx(args, 0)());
  N(app.keys.key_pair_exists(ident),
    F("public and private key '%s' do not exist in keystore")
      % idx(args, 0)());

  packet_writer pw(cout);
  keypair kp;
  app.keys.get_key_pair(ident, kp);
  pw.consume_key_pair(ident, kp);
}


CMD(read, N_("packet i/o"), "[FILE1 [FILE2 [...]]]",
    N_("read packets from files or stdin"),
    OPT_NONE)
{
  packet_db_writer dbw(app, true);
  size_t count = 0;
  if (args.empty())
    {
      count += read_packets(cin, dbw);
      N(count != 0, F("no packets found on stdin"));
    }
  else
    {
      for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
        {
          data dat;
          read_data(system_path(*i), dat);
          istringstream ss(dat());
          count += read_packets(ss, dbw);
        }
      N(count != 0, FP("no packets found in given file",
                       "no packets found in given files",
                       args.size()));
    }
  P(FP("read %d packet", "read %d packets", count) % count);
}


CMD(reindex, N_("network"), "",
    N_("rebuild the indices used to sync over the network"),
    OPT_NONE)
{
  if (args.size() > 0)
    throw usage(name);

  transaction_guard guard(app.db);
  ui.set_tick_trailer("rehashing db");
  app.db.rehash();
  guard.commit();
}

static const var_key default_server_key(var_domain("database"),
                                        var_name("default-server"));
static const var_key default_include_pattern_key(var_domain("database"),
                                                 var_name("default-include-pattern"));
static const var_key default_exclude_pattern_key(var_domain("database"),
                                                 var_name("default-exclude-pattern"));

static void
process_netsync_args(std::string const & name,
                     std::vector<utf8> const & args,
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
              P(F("setting default server to %s\n") % addr);
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
          L(F("using default server address: %s\n") % addr);
        }
    }

  // handle include/exclude args
  if (serve_mode || (args.size() >= 2 || !app.exclude_patterns.empty()))
    {
      int pattern_offset = (serve_mode ? 0 : 1);
      std::set<utf8> patterns(args.begin() + pattern_offset, args.end());
      combine_and_check_globish(patterns, include_pattern);
      combine_and_check_globish(app.exclude_patterns, exclude_pattern);
      if (use_defaults &&
          (!app.db.var_exists(default_include_pattern_key) || app.set_default))
        {
          P(F("setting default branch include pattern to '%s'\n") % include_pattern);
          app.db.set_var(default_include_pattern_key, var_value(include_pattern()));
        }
      if (use_defaults &&
          (!app.db.var_exists(default_exclude_pattern_key) || app.set_default))
        {
          P(F("setting default branch exclude pattern to '%s'\n") % exclude_pattern);
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
      L(F("using default branch include pattern: '%s'\n") % include_pattern);
      if (app.db.var_exists(default_exclude_pattern_key))
        {
          app.db.get_var(default_exclude_pattern_key, pattern_value);
          exclude_pattern = utf8(pattern_value());
        }
      else
        exclude_pattern = utf8("");
      L(F("excluding: %s\n") % exclude_pattern);
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
    P(F("doing anonymous pull; use -kKEYNAME if you need authentication\n"));
  
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

CMD(serve, N_("network"), N_("PATTERN ..."),
    N_("serve the branches specified by PATTERNs to connecting clients"),
    OPT_BIND % OPT_PIDFILE % OPT_EXCLUDE)
{
  if (args.size() < 1)
    throw usage(name);

  pid_file pid(app.pidfile);

  rsa_keypair_id key;
  get_user_key(key, app);
  app.signing_key = key;

  N(app.lua.hook_persist_phrase_ok(),
    F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
  require_password(key, app);

  app.db.ensure_open();

  utf8 dummy_addr, include_pattern, exclude_pattern;
  process_netsync_args(name, args, dummy_addr, include_pattern, exclude_pattern, false, true, app);
  run_netsync_protocol(server_voice, source_and_sink_role, app.bind_address,
                       include_pattern, exclude_pattern, app);  
}


CMD(db, N_("database"), 
    N_("init\n"
      "info\n"
      "version\n"
      "dump\n"
      "load\n"
      "migrate\n"
      "execute\n"
      "kill_rev_locally ID\n"
      "kill_branch_certs_locally BRANCH\n"
      "kill_tag_locally TAG\n"
      "check\n"
      "changesetify\n"
      "rosterify\n"
      "set_epoch BRANCH EPOCH\n"), 
    N_("manipulate database state"),
    OPT_NONE)
{
  if (args.size() == 1)
    {
      if (idx(args, 0)() == "init")
        app.db.initialize();
      else if (idx(args, 0)() == "info")
        app.db.info(cout);
      else if (idx(args, 0)() == "version")
        app.db.version(cout);
      else if (idx(args, 0)() == "dump")
        app.db.dump(cout);
      else if (idx(args, 0)() == "load")
        app.db.load(cin);
      else if (idx(args, 0)() == "migrate")
        app.db.migrate();
      else if (idx(args, 0)() == "check")
        check_db(app);
      else if (idx(args, 0)() == "changesetify")
        build_changesets_from_manifest_ancestry(app);
      else if (idx(args, 0)() == "rosterify")
        build_roster_style_revs_from_manifest_style_revs(app);
      else
        throw usage(name);
    }
  else if (args.size() == 2)
    {
      if (idx(args, 0)() == "execute")
        app.db.debug(idx(args, 1)(), cout);
      else if (idx(args, 0)() == "kill_rev_locally")
        kill_rev_locally(app,idx(args, 1)());
      else if (idx(args, 0)() == "clear_epoch")
        app.db.clear_epoch(cert_value(idx(args, 1)()));
      else if (idx(args, 0)() == "kill_branch_certs_locally")
        app.db.delete_branch_named(cert_value(idx(args, 1)()));
      else if (idx(args, 0)() == "kill_tag_locally")
        app.db.delete_tag_named(cert_value(idx(args, 1)()));
      else
        throw usage(name);
    }
  else if (args.size() == 3)
    {
      if (idx(args, 0)() == "set_epoch")
        app.db.set_epoch(cert_value(idx(args, 1)()),
                         epoch_data(idx(args,2)()));
      else
        throw usage(name);
    }
  else
    throw usage(name);
}

CMD(attr, N_("working copy"), N_("set PATH ATTR VALUE\nget PATH [ATTR]\ndrop PATH [ATTR]"), 
    N_("set, get or drop file attributes"),
    OPT_NONE)
{
  if (args.size() < 2 || args.size() > 4)
    throw usage(name);

  revision_set rs;
  roster_t old_roster, new_roster;

  app.require_working_copy();
  get_unrestricted_working_revision_and_rosters(app, rs, old_roster, new_roster);
  
  file_path path = file_path_external(idx(args,1));
  split_path sp;
  path.split(sp);

  N(new_roster.has_node(sp), F("Unknown path '%s'") % path);
  node_t node = new_roster.get_node(sp);

  string subcmd = idx(args, 0)();
  if (subcmd == "set" || subcmd == "drop")
    {
      if (subcmd == "set")
        {
          if (args.size() != 4)
            throw usage(name);

          attr_key a_key = idx(args, 2)();
          attr_value a_value = idx(args, 3)();

          node->attrs[a_key] = make_pair(true, a_value);
        }
      else
        {
          // Clear all attrs (or a specific attr)
          if (args.size() == 2)
            {
              for (full_attr_map_t::iterator i = node->attrs.begin();
                   i != node->attrs.end(); ++i)
                i->second = make_pair(false, "");
            }
          else if (args.size() == 3)
            {
              attr_key a_key = idx(args, 2)();
              N(node->attrs.find(a_key) != node->attrs.end(),
                F("Path '%s' does not have attribute '%s'\n") 
                % path % a_key);
              node->attrs[a_key] = make_pair(false, "");
            }
          else
            throw usage(name);
        }

      cset new_work;
      make_cset(old_roster, new_roster, new_work);
      put_work_cset(new_work);
      update_any_attrs(app);
    }
  else if (subcmd == "get")
    {
      if (args.size() == 2)
        {
          bool has_any_live_attrs = false;
          for (full_attr_map_t::const_iterator i = node->attrs.begin();
               i != node->attrs.end(); ++i)
            if (i->second.first)
              {
                cout << path << " : " << i->first << "=" << i->second.second << endl;
                has_any_live_attrs = true;
              }
          if (!has_any_live_attrs)
            cout << "No attributes for " << path << endl;
        }
      else if (args.size() == 3)
        {
          attr_key a_key = idx(args, 2)();
          full_attr_map_t::const_iterator i = node->attrs.find(a_key);              
          if (i != node->attrs.end() && i->second.first)
            cout << path << " : " << i->first << "=" << i->second.second << endl;
          else
            cout << "No attribute " << a_key << " on path " << path << endl;
        }
      else
        throw usage(name);
    }
  else 
    throw usage(name);
}


CMD(commit, N_("working copy"), N_("[PATH]..."), 
    N_("commit working copy to database"),
    OPT_BRANCH_NAME % OPT_MESSAGE % OPT_MSGFILE % OPT_DATE % 
    OPT_AUTHOR % OPT_DEPTH % OPT_EXCLUDE)
{
  string log_message("");
  revision_set rs;
  revision_id rid;
  roster_t old_roster, new_roster;
  
  app.make_branch_sticky();
  app.require_working_copy();

  // preserve excluded work for future commmits
  cset excluded_work;
  get_working_revision_and_rosters(app, args, rs, old_roster, new_roster, excluded_work);
  calculate_ident(rs, rid);

  N(rs.is_nontrivial(), F("no changes to commit\n"));
    
  cert_value branchname;
  I(rs.edges.size() == 1);

  set<revision_id> heads;
  get_branch_heads(app.branch_name(), app, heads);
  unsigned int old_head_size = heads.size();

  guess_branch(edge_old_revision(rs.edges.begin()), app, branchname);

  P(F("beginning commit on branch '%s'\n") % branchname);
  L(F("new manifest '%s'\n"
      "new revision '%s'\n")
    % rs.new_manifest
    % rid);

  // can't have both a --message and a --message-file ...
  N(app.message().length() == 0 || app.message_file().length() == 0,
    F("--message and --message-file are mutually exclusive"));

  N(!( app.message().length() > 0 && has_contents_user_log()),
    F("MT/log is non-empty and --message supplied\n"
      "perhaps move or delete MT/log,\n"
      "or remove --message from the command line?"));
  
  N(!( app.message_file().length() > 0 && has_contents_user_log()),
    F("MT/log is non-empty and --message-file supplied\n"
      "perhaps move or delete MT/log,\n"
      "or remove --message-file from the command line?"));
  
  // fill app.message with message_file contents
  if (app.message_file().length() > 0)
  {
    data dat;
    read_data_for_command_line(app.message_file(), dat);
    app.message = dat();
  }
  
  if (app.message().length() > 0)
    log_message = app.message();
  else
    {
      get_log_message(rs, app, log_message);
      N(log_message.find_first_not_of(" \r\t\n") != string::npos,
        F("empty log message; commit canceled"));
      // we write it out so that if the commit fails, the log
      // message will be preserved for a retry
      write_user_log(data(log_message));
    }

  {
    transaction_guard guard(app.db);
    packet_db_writer dbw(app);

    if (app.db.revision_exists(rid))
      {
        W(F("revision %s already in database\n") % rid);
      }
    else
      {
        // new revision
        L(F("inserting new revision %s\n") % rid);

        I(rs.edges.size() == 1);
        edge_map::const_iterator edge = rs.edges.begin();
        I(edge != rs.edges.end());

        // process file deltas or new files
        cset const & cs = edge_changes(edge);

        for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
             i != cs.deltas_applied.end(); ++i)
          {
            file_path path(i->first);
            file_id old_content = i->second.first;
            file_id new_content = i->second.second;

            if (app.db.file_version_exists(new_content))
              {
                L(F("skipping file delta %s, already in database\n")
                  % delta_entry_dst(i));
              }
            else if (app.db.file_version_exists(old_content))
              {
                L(F("inserting delta %s -> %s\n")
                  % old_content % new_content);
                file_data old_data;
                data new_data;
                app.db.get_file_version(old_content, old_data);
                read_localized_data(path, new_data, app.lua);
                // sanity check
                hexenc<id> tid;
                calculate_ident(new_data, tid);
                N(tid == new_content.inner(),
                  F("file '%s' modified during commit, aborting")
                  % path);
                delta del;
                diff(old_data.inner(), new_data, del);
                dbw.consume_file_delta(old_content, 
                                       new_content, 
                                       file_delta(del));
              }
            else
              {
                L(F("inserting full version %s\n") % new_content);
                data new_data;
                read_localized_data(path, new_data, app.lua);
                // sanity check
                hexenc<id> tid;
                calculate_ident(new_data, tid);
                N(tid == new_content.inner(),
                  F("file '%s' modified during commit, aborting")
                  % path);
                dbw.consume_file_data(new_content, file_data(new_data));
              }
          }

        for (std::map<split_path, file_id>::const_iterator i = cs.files_added.begin();
             i != cs.files_added.end(); ++i)
          {
            file_path path(i->first);
            file_id new_content = i->second;

            L(F("inserting full version %s\n") % new_content);
            data new_data;
            read_localized_data(path, new_data, app.lua);
            // sanity check
            hexenc<id> tid;
            calculate_ident(new_data, tid);
            N(tid == new_content.inner(),
              F("file '%s' modified during commit, aborting")
              % path);
            dbw.consume_file_data(new_content, file_data(new_data));
          }
      }

    revision_data rdat;
    write_revision_set(rs, rdat);
    dbw.consume_revision_data(rid, rdat);
  
    cert_revision_in_branch(rid, branchname, app, dbw); 
    if (app.date_set)
      cert_revision_date_time(rid, app.date, app, dbw);
    else
      cert_revision_date_now(rid, app, dbw);
    if (app.author().length() > 0)
      cert_revision_author(rid, app.author(), app, dbw);
    else
      cert_revision_author_default(rid, app, dbw);
    cert_revision_changelog(rid, log_message, app, dbw);
    guard.commit();
  }
  
  // small race condition here...
  put_work_cset(excluded_work);
  put_revision_id(rid);
  P(F("committed revision %s\n") % rid);
  
  blank_user_log();

  get_branch_heads(app.branch_name(), app, heads);
  if (heads.size() > old_head_size && old_head_size > 0) {
    P(F("note: this revision creates divergence\n"
        "note: you may (or may not) wish to run 'monotone merge'"));
  }
    
  update_any_attrs(app);
  maybe_update_inodeprints(app);

  {
    // tell lua what happened. yes, we might lose some information here,
    // but it's just an indicator for lua, eg. to post stuff to a mailing
    // list. if the user *really* cares about cert validity, multiple certs
    // with same name, etc.  they can inquire further, later.
    map<cert_name, cert_value> certs;
    vector< revision<cert> > ctmp;
    app.db.get_revision_certs(rid, ctmp);
    for (vector< revision<cert> >::const_iterator i = ctmp.begin();
         i != ctmp.end(); ++i)
      {
        cert_value vtmp;
        decode_base64(i->inner().value, vtmp);
        certs.insert(make_pair(i->inner().name, vtmp));
      }
    revision_data rdat;
    app.db.get_revision(rid, rdat);
    app.lua.hook_note_commit(rid, rdat, certs);
  }
}

ALIAS(ci, commit);


static void
do_external_diff(cset const & cs,
                 app_state & app,
                 bool new_is_archived)
{
  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      data data_old;
      data data_new;

      file_data f_old;
      app.db.get_file_version(delta_entry_src(i), f_old);
      data_old = f_old.inner();

      if (new_is_archived)
        {
          file_data f_new;
          app.db.get_file_version(delta_entry_dst(i), f_new);
          data_new = f_new.inner();
        }
      else
        {
          read_localized_data(file_path(delta_entry_path(i)),
                              data_new, app.lua);
        }

      bool is_binary = false;
      if (guess_binary(data_old()) ||
          guess_binary(data_new()))
        is_binary = true;

      app.lua.hook_external_diff(file_path(delta_entry_path(i)),
                                 data_old,
                                 data_new,
                                 is_binary,
                                 app.diff_args_provided,
                                 app.diff_args(),
                                 delta_entry_src(i).inner()(),
                                 delta_entry_dst(i).inner()());
    }
}

static void 
dump_diffs(cset const & cs,
           app_state & app,
           bool new_is_archived,
           diff_type type)
{
  // 60 is somewhat arbitrary, but less than 80
  std::string patch_sep = std::string(60, '=');

  for (std::map<split_path, file_id>::const_iterator 
         i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    {
      cout << patch_sep << "\n";
      data unpacked;
      vector<string> lines;
      
      if (new_is_archived)
        {
          file_data dat;
          app.db.get_file_version(i->second, dat);
          unpacked = dat.inner();
        }
      else
        {
          read_localized_data(file_path(i->first),
                              unpacked, app.lua);
        }
      
      if (guess_binary(unpacked()))
        cout << "# " << file_path(i->first) << " is binary\n";
      else
        {     
          split_into_lines(unpacked(), lines);
          if (! lines.empty())
            {
              cout << (boost::format("--- %s\t%s\n") % file_path(i->first) % i->second)
                   << (boost::format("+++ %s\t%s\n") % file_path(i->first) % i->second)
                   << (boost::format("@@ -0,0 +1,%d @@\n") % lines.size());
              for (vector<string>::const_iterator j = lines.begin();
                   j != lines.end(); ++j)
                {
                  cout << "+" << *j << endl;
                }
            }
        }
    }

  std::map<split_path, split_path> reverse_rename_map;

  for (std::map<split_path, split_path>::const_iterator 
         i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    {
      reverse_rename_map.insert(std::make_pair(i->second, i->first));
    }

  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
         i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    {
      file_data f_old;
      data data_old, data_new;
      vector<string> old_lines, new_lines;
      
      app.db.get_file_version(delta_entry_src(i), f_old);
      data_old = f_old.inner();
      
      if (new_is_archived)
        {
          file_data f_new;
          app.db.get_file_version(delta_entry_dst(i), f_new);
          data_new = f_new.inner();
        }
      else
        {
          read_localized_data(file_path(delta_entry_path(i)), 
                              data_new, app.lua);
        }
      
      if (guess_binary(data_new()) || 
          guess_binary(data_old()))
        cout << "# " << file_path(delta_entry_path(i)) << " is binary\n";
      else
        {
          split_into_lines(data_old(), old_lines);
          split_into_lines(data_new(), new_lines);

          split_path dst_path = delta_entry_path(i);
          split_path src_path = dst_path;

          std::map<split_path, split_path>::const_iterator re;
          re = reverse_rename_map.find(dst_path);

          if (re != reverse_rename_map.end())
            src_path = re->second;

          make_diff(file_path(src_path).as_internal(), 
                    file_path(dst_path).as_internal(), 
                    delta_entry_src(i),
                    delta_entry_dst(i),
                    old_lines, new_lines,
                    cout, type);
        }
    }
}

CMD(diff, N_("informative"), N_("[PATH]..."), 
    N_("show current diffs on stdout.\n"
    "If one revision is given, the diff between the working directory and\n"
    "that revision is shown.  If two revisions are given, the diff between\n"
    "them is given.  If no format is specified, unified is used by default."),
    OPT_BRANCH_NAME % OPT_REVISION % OPT_DEPTH %
    OPT_UNIFIED_DIFF % OPT_CONTEXT_DIFF % OPT_EXTERNAL_DIFF %
    OPT_EXTERNAL_DIFF_ARGS)
{
  revision_set r_old, r_new;
  roster_t new_roster, old_roster;
  bool new_is_archived;
  diff_type type = app.diff_format;
  ostringstream header;

  if (app.diff_args_provided)
    N(app.diff_format == external_diff,
      F("--diff-args requires --external\n"
        "try adding --external or removing --diff-args?"));

  cset composite;
  cset excluded;

  // initialize before transaction so we have a database to work with

  if (app.revision_selectors.size() == 0)
    app.require_working_copy();
  else if (app.revision_selectors.size() == 1)
    app.require_working_copy();

  if (app.revision_selectors.size() == 0)
    {
      get_working_revision_and_rosters(app, args, r_new,
                                       old_roster, 
                                       new_roster,
                                       excluded);

      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      if (r_new.edges.size() == 1)
        composite = edge_changes(r_new.edges.begin());
      new_is_archived = false;
      revision_id old_rid;
      get_revision_id(old_rid);
      header << "# old_revision [" << old_rid << "]" << endl;
    }
  else if (app.revision_selectors.size() == 1)
    {
      revision_id r_old_id;
      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      N(app.db.revision_exists(r_old_id),
        F("no such revision '%s'") % r_old_id);
      get_working_revision_and_rosters(app, args, r_new,
                                       old_roster, 
                                       new_roster,
                                       excluded);
      // now clobber old_roster with the one specified
      app.db.get_revision(r_old_id, r_old);
      app.db.get_roster(r_old_id, old_roster);
      I(r_new.edges.size() == 1 || r_new.edges.size() == 0);
      N(r_new.edges.size() == 1, F("current revision has no ancestor"));
      new_is_archived = false;
      header << "# old_revision [" << r_old_id << "]" << endl;
    }
  else if (app.revision_selectors.size() == 2)
    {
      revision_id r_old_id, r_new_id;
      complete(app, idx(app.revision_selectors, 0)(), r_old_id);
      complete(app, idx(app.revision_selectors, 1)(), r_new_id);
      N(app.db.revision_exists(r_old_id),
        F("no such revision '%s'") % r_old_id);
      app.db.get_revision(r_old_id, r_old);
      N(app.db.revision_exists(r_new_id),
        F("no such revision '%s'") % r_new_id);
      app.db.get_revision(r_new_id, r_new);
      app.db.get_roster(r_old_id, old_roster);
      app.db.get_roster(r_new_id, new_roster);
      new_is_archived = true;
    }
  else
    {
      throw usage(name);
    }

  if (app.revision_selectors.size() > 0)
    make_cset (old_roster, new_roster, composite);
  
  data summary;
  write_cset(composite, summary);

  vector<string> lines;
  split_into_lines(summary(), lines);
  cout << "# " << endl;
  if (summary().size() > 0) 
    {
      cout << header.str() << "# " << endl;
      for (vector<string>::iterator i = lines.begin(); i != lines.end(); ++i)
        cout << "# " << *i << endl;
    }
  else
    {
      cout << "# no changes" << endl;
    }
  cout << "# " << endl;

  if (type == external_diff) {
    do_external_diff(composite, app, new_is_archived);
  } else
    dump_diffs(composite, app, new_is_archived, type);
}


struct update_source 
  : public file_content_source
{
  std::map<file_id, file_data> & temporary_store;
  app_state & app;
  update_source (std::map<file_id, file_data> & tmp,
                 app_state & app)
    : temporary_store(tmp), app(app)
  {}
  void get_file_content(file_id const & fid,
                        file_data & dat) const
  {
    std::map<file_id, file_data>::const_iterator i = temporary_store.find(fid);
    if (i != temporary_store.end())
      dat = i->second;
    else
      app.db.get_file_version(fid, dat);
  }
};

CMD(update, N_("working copy"), "",
    N_("update working copy.\n"
    "If a revision is given, base the update on that revision.  If not,\n"
    "base the update on the head of the branch (given or implicit)."),
    OPT_BRANCH_NAME % OPT_REVISION)
{
  revision_set r_old, r_working, r_new;
  roster_t working_roster, chosen_roster;
  boost::shared_ptr<roster_t> old_roster = boost::shared_ptr<roster_t>(new roster_t());
  marking_map working_mm, chosen_mm, merged_mm;
  revision_id r_old_id, r_working_id, r_chosen_id;

  if (args.size() > 0)
    throw usage(name);

  if (app.revision_selectors.size() > 1)
    throw usage(name);

  app.require_working_copy();

  // FIXME: the next few lines are a little bit expensive insofar as they
  // load the base roster twice. The API could use some factoring or
  // such. But it should work for now; revisit if performance is
  // intolerable.

  get_unrestricted_working_revision_and_rosters(app, r_working,
                                                *old_roster, 
                                                working_roster);
  calculate_ident(r_working, r_working_id);
  I(r_working.edges.size() == 1);
  r_old_id = edge_old_revision(r_working.edges.begin());
  make_roster_for_base_plus_cset(r_old_id, 
                                 edge_changes(r_working.edges.begin()),
                                 r_working_id,
                                 working_roster, working_mm, app);

  N(!null_id(r_old_id),
    F("this working directory is a new project; cannot update"));

  if (app.revision_selectors.size() == 0)
    {
      set<revision_id> candidates;
      pick_update_candidates(r_old_id, app, candidates);
      N(!candidates.empty(),
        F("your request matches no descendents of the current revision\n"
          "in fact, it doesn't even match the current revision\n"
          "maybe you want --revision=<rev on other branch>"));
      if (candidates.size() != 1)
        {
          P(F("multiple update candidates:\n"));
          for (set<revision_id>::const_iterator i = candidates.begin();
               i != candidates.end(); ++i)
            P(boost::format("  %s\n") % describe_revision(app, *i));
          P(F("choose one with 'monotone update -r<id>'\n"));
          N(false, F("multiple candidates remain after selection"));
        }
      r_chosen_id = *(candidates.begin());
    }
  else
    {
      complete(app, app.revision_selectors[0](), r_chosen_id);
      N(app.db.revision_exists(r_chosen_id),
        F("no such revision '%s'") % r_chosen_id);
    }

  notify_if_multiple_heads(app);
  
  if (r_old_id == r_chosen_id)
    {
      P(F("already up to date at %s\n") % r_old_id);
      // do still switch the working copy branch, in case they have used
      // update to switch branches.
      if (!app.branch_name().empty())
        app.make_branch_sticky();
      return;
    }
  
  P(F("selected update target %s\n") % r_chosen_id);

  if (!app.branch_name().empty())
    {
      cert_value branch_name(app.branch_name());
      base64<cert_value> branch_encoded;
      encode_base64(branch_name, branch_encoded);
  
      vector< revision<cert> > certs;
      app.db.get_revision_certs(r_chosen_id, branch_cert_name, branch_encoded, certs);

      N(certs.size() != 0,
        F("revision %s is not a member of branch %s\n"
          "try again with explicit --branch\n")
        % r_chosen_id % app.branch_name);
    }

  // FIXME_ROSTERS: In the old (pre-roster) code, we supported updating to
  // any revision in the graph, anywhere; if there was a path to get there,
  // we'd synthesize a changeset to get there. This was a little easier to
  // work with in pre-roster monotone because we merged changesets, not
  // rosters.
  //
  // To do this using rosters' default "mark-merge", we'd need to
  // synthesize a marking map using a fake ancestry graph. It's
  // conceivable, but it's a fair amount of work, and it's not clear that
  // many people used it, nor that mark-merge is ideal for the task. It
  // might make more sense to make a different merger for this
  // case. 
  //
  // Anyways, for the time being we're only implementing "forwards
  // updates". That is, you can only "update" to new base revisions which
  // are descendents of the base revision you have in your working copy.

  N(is_ancestor(r_old_id, r_chosen_id, app),
    F("Update target is not a descendent of working copy base revision\n"));

  app.db.get_roster(r_chosen_id, chosen_roster, chosen_mm);

  std::set<revision_id> 
    working_uncommon_ancestors, 
    chosen_uncommon_ancestors;

  app.db.get_uncommon_ancestors(r_old_id, r_chosen_id,
                                working_uncommon_ancestors, 
                                chosen_uncommon_ancestors);

  // Note that under the definition of mark-merge, the working copy is an
  // "uncommon ancestor" if itself too, even though it was not present in
  // the database (hence not returned by the query above).

  working_uncommon_ancestors.insert(r_working_id);

  // Now merge the working roster with the chosen target. 

  roster_merge_result result;  
  roster_merge(working_roster, working_mm, working_uncommon_ancestors,
               chosen_roster, chosen_mm, chosen_uncommon_ancestors,
               result);

  roster_t & merged_roster = result.roster;

  content_merge_working_copy_adaptor wca(app, old_roster);
  resolve_merge_conflicts (r_old_id, r_chosen_id,
                           working_roster, chosen_roster,
                           working_mm, chosen_mm,
                           result, wca, app);

  I(result.is_clean());
  merged_roster.check_sane();

  // we have the following
  //
  // old --> working
  //   |         | 
  //   V         V
  //  chosen --> merged
  //
  // - old is the revision specified in MT/revision
  // - working is based on old and includes the working copy's changes
  // - chosen is the revision we're updating to and will end up in MT/revision
  // - merged is the merge of working and chosen
  // 
  // we apply the working to merged cset to the working copy
  // and write the cset from chosen to merged changeset in MT/work
  
  cset update, remaining;
  make_cset (working_roster, merged_roster, update);
  make_cset (chosen_roster, merged_roster, remaining);

  //   {
  //     data t1, t2, t3;
  //     write_cset(update, t1);
  //     write_cset(remaining, t2);
  //     write_manifest_of_roster(merged_roster, t3);
  //     P(F("updating working copy with [[[\n%s\n]]]\n") % t1);
  //     P(F("leaving residual work [[[\n%s\n]]]\n") % t2);
  //     P(F("merged roster [[[\n%s\n]]]\n") % t3);
  //   }

  update_source fsource(wca.temporary_store, app);
  editable_working_tree ewt(app, fsource);
  update.apply_to(ewt);
  
  // small race condition here...
  // nb: we write out r_chosen, not r_new, because the revision-on-disk
  // is the basis of the working copy, not the working copy itself.
  put_revision_id(r_chosen_id);
  if (!app.branch_name().empty())
    {
      app.make_branch_sticky();
    }
  P(F("updated to base revision %s\n") % r_chosen_id);

  put_work_cset(remaining);
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}


CMD(merge, N_("tree"), "", N_("merge unmerged heads of branch"),
    OPT_BRANCH_NAME % OPT_DATE % OPT_AUTHOR % OPT_LCA)
{
  set<revision_id> heads;

  if (args.size() != 0)
    throw usage(name);

  N(app.branch_name() != "",
    F("please specify a branch, with --branch=BRANCH"));

  get_branch_heads(app.branch_name(), app, heads);

  N(heads.size() != 0, F("branch '%s' is empty\n") % app.branch_name);
  N(heads.size() != 1, F("branch '%s' is merged\n") % app.branch_name);

  set<revision_id>::const_iterator i = heads.begin();
  revision_id left = *i;
  revision_id ancestor;
  size_t count = 1;
  P(F("starting with revision 1 / %d\n") % heads.size());
  for (++i; i != heads.end(); ++i, ++count)
    {
      revision_id right = *i;
      P(F("merging with revision %d / %d\n") % (count + 1) % heads.size());
      P(F("[source] %s\n") % left);
      P(F("[source] %s\n") % right);

      revision_id merged;
      transaction_guard guard(app.db);
      interactive_merge_and_store(left, right, merged, app);
                  
      // merged 1 edge; now we commit this, update merge source and
      // try next one

      packet_db_writer dbw(app);
      cert_revision_in_branch(merged, app.branch_name(), app, dbw);

      string log = (boost::format("merge of %s\n"
                                  "     and %s\n") % left % right).str();
      cert_revision_changelog(merged, log, app, dbw);
          
      guard.commit();
      P(F("[merged] %s\n") % merged);
      left = merged;
    }
  P(F("note: your working copies have not been updated\n"));
}

CMD(propagate, N_("tree"), N_("SOURCE-BRANCH DEST-BRANCH"), 
    N_("merge from one branch to another asymmetrically"),
    OPT_DATE % OPT_AUTHOR % OPT_LCA)
{
  //   this is a special merge operator, but very useful for people maintaining
  //   "slightly disparate but related" trees. it does a one-way merge; less
  //   powerful than putting things in the same branch and also more flexible.
  //
  //   1. check to see if src and dst branches are merged, if not abort, if so
  //   call heads N1 and N2 respectively.
  //
  //   2. (FIXME: not yet present) run the hook propagate ("src-branch",
  //   "dst-branch", N1, N2) which gives the user a chance to massage N1 into
  //   a state which is likely to "merge nicely" with N2, eg. edit pathnames,
  //   omit optional files of no interest.
  //
  //   3. do a normal 2 or 3-way merge on N1 and N2, depending on the
  //   existence of common ancestors.
  //
  //   4. save the results as the delta (N2,M), the ancestry edges (N1,M)
  //   and (N2,M), and the cert (N2,dst).
  //
  //   there are also special cases we have to check for where no merge is
  //   actually necessary, because there hasn't been any divergence since the
  //   last time propagate was run.
  
  set<revision_id> src_heads, dst_heads;

  if (args.size() != 2)
    throw usage(name);

  get_branch_heads(idx(args, 0)(), app, src_heads);
  get_branch_heads(idx(args, 1)(), app, dst_heads);

  N(src_heads.size() != 0, F("branch '%s' is empty\n") % idx(args, 0)());
  N(src_heads.size() == 1, F("branch '%s' is not merged\n") % idx(args, 0)());

  N(dst_heads.size() != 0, F("branch '%s' is empty\n") % idx(args, 1)());
  N(dst_heads.size() == 1, F("branch '%s' is not merged\n") % idx(args, 1)());

  set<revision_id>::const_iterator src_i = src_heads.begin();
  set<revision_id>::const_iterator dst_i = dst_heads.begin();
  
  P(F("propagating %s -> %s\n") % idx(args,0) % idx(args,1));
  P(F("[source] %s\n") % *src_i);
  P(F("[target] %s\n") % *dst_i);

  // check for special cases
  if (*src_i == *dst_i || is_ancestor(*src_i, *dst_i, app))
    {
      P(F("branch '%s' is up-to-date with respect to branch '%s'\n")
          % idx(args, 1)() % idx(args, 0)());
      P(F("no action taken\n"));
    }
  else if (is_ancestor(*dst_i, *src_i, app))
    {
      P(F("no merge necessary; putting %s in branch '%s'\n")
        % (*src_i) % idx(args, 1)());
      transaction_guard guard(app.db);
      packet_db_writer dbw(app);
      cert_revision_in_branch(*src_i, idx(args, 1)(), app, dbw);
      guard.commit();
    }
  else
    {
      revision_id merged;
      transaction_guard guard(app.db);
      interactive_merge_and_store(*src_i, *dst_i, merged, app);

      packet_db_writer dbw(app);

      cert_revision_in_branch(merged, idx(args, 1)(), app, dbw);

      string log = (boost::format("propagate from branch '%s' (head %s)\n"
                                  "            to branch '%s' (head %s)\n")
                    % idx(args, 0) % (*src_i)
                    % idx(args, 1) % (*dst_i)).str();

      cert_revision_changelog(merged, log, app, dbw);

      guard.commit();      
      P(F("[merged] %s\n") % merged);
    }
}

CMD(refresh_inodeprints, N_("tree"), "", N_("refresh the inodeprint cache"),
    OPT_NONE)
{
  enable_inodeprints();
  maybe_update_inodeprints(app);
}

CMD(explicit_merge, N_("tree"),
    N_("LEFT-REVISION RIGHT-REVISION DEST-BRANCH"),
    N_("merge two explicitly given revisions, placing result in given branch"),
    OPT_DATE % OPT_AUTHOR)
{
  revision_id left, right;
  string branch;

  if (args.size() != 3)
    throw usage(name);

  complete(app, idx(args, 0)(), left);
  complete(app, idx(args, 1)(), right);
  branch = idx(args, 2)();
  
  N(!(left == right),
    F("%s and %s are the same revision, aborting") % left % right);
  N(!is_ancestor(left, right, app),
    F("%s is already an ancestor of %s") % left % right);
  N(!is_ancestor(right, left, app),
    F("%s is already an ancestor of %s") % right % left);

  // Somewhat redundant, but consistent with output of plain "merge" command.
  P(F("[source] %s\n") % left);
  P(F("[source] %s\n") % right);

  revision_id merged;
  transaction_guard guard(app.db);
  interactive_merge_and_store(left, right, merged, app);
  
  packet_db_writer dbw(app);
  
  cert_revision_in_branch(merged, branch, app, dbw);
  
  string log = (boost::format("explicit_merge of '%s'\n"
                              "              and '%s'\n"
                              "        to branch '%s'\n")
                % left % right % branch).str();
  
  cert_revision_changelog(merged, log, app, dbw);
  
  guard.commit();      
  P(F("[merged] %s\n") % merged);
}

CMD(complete, N_("informative"), N_("(revision|manifest|file|key) PARTIAL-ID"),
    N_("complete partial id"),
    OPT_VERBOSE)
{
  if (args.size() != 2)
    throw usage(name);

  bool verbose = app.verbose;

  N(idx(args, 1)().find_first_not_of("abcdef0123456789") == string::npos,
    F("non-hex digits in partial id"));

  if (idx(args, 0)() == "revision")
    {      
      set<revision_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<revision_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        {
          if (!verbose) cout << i->inner()() << endl;
          else cout << describe_revision(app, i->inner()) << endl;
        }
    }
  else if (idx(args, 0)() == "manifest")
    {      
      set<manifest_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<manifest_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        cout << i->inner()() << endl;
    }
  else if (idx(args, 0)() == "file")
    {
      set<file_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<file_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        cout << i->inner()() << endl;
    }
  else if (idx(args, 0)() == "key")
    {
      typedef set< pair<key_id, utf8 > > completions_t;
      completions_t completions;
      app.db.complete(idx(args, 1)(), completions);
      for (completions_t::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        {
          cout << i->first.inner()();
          if (verbose) cout << " " << i->second();
          cout << endl;
        }
    }
  else
    throw usage(name);  
}

CMD(revert, N_("working copy"), N_("[PATH]..."), 
    N_("revert file(s), dir(s) or entire working copy"), OPT_DEPTH % OPT_EXCLUDE % OPT_MISSING)
{
  roster_t old_roster;
  revision_id old_revision_id;
  cset work, included_work, excluded_work;
  path_set old_paths;
 
  app.require_working_copy();

  get_base_revision(app, old_revision_id, old_roster);

  get_work_cset(work);
  old_roster.extract_path_set(old_paths);

  path_set valid_paths(old_paths);

  extract_rearranged_paths(work, valid_paths);
  add_intermediate_paths(valid_paths);

  vector<utf8> args_copy(args);
  if (app.missing)
    {
      L(F("revert adding find_missing entries to %d original args elements\n") % args_copy.size());
      path_set missing;
      find_missing(app, args_copy, missing);

      // chose as_external because app_state::set_restriction turns utf8s into file_paths
      // using file_path_external()...
      for (path_set::const_iterator i = missing.begin(); i != missing.end(); i++)
        args_copy.push_back(file_path(*i).as_external());

      L(F("after adding everything from find_missing, revert args_copy has %d elements\n") % args_copy.size());

      // when given --missing, never revert if there's nothing missing and no 
      // specific files were specified.
      if (args_copy.size() == 0)
        return;
    }

  app.set_restriction(valid_paths, args_copy, false);

  restrict_cset(work, included_work, excluded_work, app);

  node_map const & nodes = old_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;
      node_t node = i->second;

      if (null_node(node->parent))
        continue;

      split_path sp;
      old_roster.get_name(nid, sp);
      file_path fp(sp);
      
      // Only revert restriction-included files.
      if (!app.restriction_includes(sp))
        continue;

      if (is_file_t(node))
        {
          file_t f = downcast_to_file_t(node);
          if (file_exists(fp))
            {
              hexenc<id> ident;
              calculate_ident(fp, ident, app.lua);
              // don't touch unchanged files
              if (ident == f->content.inner()) 
                continue;
            }
      
          L(F("reverting %s to [%s]\n") % fp % f->content);
          
          N(app.db.file_version_exists(f->content),
            F("no file version %s found in database for %s")
            % f->content % fp);
          
          file_data dat;
          L(F("writing file %s to %s\n")
            % f->content % fp);
          app.db.get_file_version(f->content, dat);
          write_localized_data(fp, dat.inner(), app.lua);
        }
      else
        {
          mkdir_p(fp);
        }
    }

  // race
  put_work_cset(excluded_work);
  update_any_attrs(app);
  maybe_update_inodeprints(app);
}

CMD(rcs_import, N_("debug"), N_("RCSFILE..."),
    N_("parse versions in RCS files\n"
       "this command doesn't reconstruct or import revisions."
       "you probably want cvs_import"),
    OPT_BRANCH_NAME)
{
  if (args.size() < 1)
    throw usage(name);
  
  for (vector<utf8>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      test_parse_rcs_file(system_path((*i)()), app.db);
    }
}


CMD(cvs_import, N_("rcs"), N_("CVSROOT"), N_("import all versions in CVS repository"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);

  import_cvs_repo(system_path(idx(args, 0)()), app);
}

static void
log_certs(app_state & app, revision_id id, cert_name name,
          string label, string separator,
          bool multiline, bool newline)
{
  vector< revision<cert> > certs;
  bool first = true;

  if (multiline)
    newline = true;

  app.db.get_revision_certs(id, name, certs);
  erase_bogus_certs(certs, app);
  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);

      if (first)
        cout << label;
      else
        cout << separator;

      if (multiline)
        {
          cout << endl << endl << tv;
          if (newline)
            cout << endl;
        }
      else
        {
          cout << tv;
          if (newline)
            cout << endl;
        }

      first = false;
    }
}

static void
log_certs(app_state & app, revision_id id, cert_name name, string label, bool multiline)
{
  log_certs(app, id, name, label, label, multiline, true);
}

static void
log_certs(app_state & app, revision_id id, cert_name name)
{
  log_certs(app, id, name, " ", ",", false, false);
}


/*
// FIXME_ROSTERS: disabled until rewritten to use rosters
CMD(annotate, N_("informative"), N_("PATH"),
    N_("print annotated copy of the file from REVISION"),
    OPT_REVISION)
{
  // this function compiles, but the do_annotate() stuff in annotate.cc is 
  // still in flux, so disabling command here.
  revision_id rid;

  if (app.revision_selectors.size() == 0)
    app.require_working_copy();

  if ((args.size() != 1) || (app.revision_selectors.size() > 1))
    throw usage(name);

  file_path file = file_path_external(idx(args, 0));
  split_path sp;
  file.split(sp);

  if (app.revision_selectors.size() == 0)
    get_revision_id(rid);
  else 
    complete(app, idx(app.revision_selectors, 0)(), rid);

  N(!null_id(rid), F("no revision for file '%s' in database") % file);
  N(app.db.revision_exists(rid), F("no such revision '%s'") % rid);

  L(F("annotate file file_path '%s'\n") % file);

  // find the version of the file requested
  roster_t roster;
  marking_map marks;
  app.db.get_roster(rid, roster, marks);
  node_t node = roster.get_node(sp);
  N((!null_node(node->self) && is_file_t(node)), F("no file '%s' found in revision '%s'\n") % file % rid);

  file_t file_node = downcast_to_file_t(node);
  L(F("annotate for file_id %s\n") % file_node->self);
  do_annotate(app, file_node, rid);
}
*/

CMD(log, N_("informative"), N_("[FILE] ..."),
    N_("print history in reverse order (filtering by 'FILE'). If one or more\n"
    "revisions are given, use them as a starting point."),
    OPT_LAST % OPT_REVISION % OPT_BRIEF % OPT_DIFFS % OPT_NO_MERGES)
{
  if (app.revision_selectors.size() == 0)
    app.require_working_copy("try passing a --revision to start at");

  set<node_id> nodes;

  if (args.size() > 0)
    {
      // User wants to trace only specific files
      roster_t old_roster, new_roster;
      revision_set rev;
      get_unrestricted_working_revision_and_rosters(app, rev, old_roster, new_roster);
      for (size_t i = 0; i < args.size(); ++i)
        {
          file_path fp = file_path_external(idx(args, i));
          split_path sp;
          fp.split(sp);
          N(new_roster.has_node(sp),
            F("Unknown file '%s' for log command") % fp);
          nodes.insert(new_roster.get_node(sp)->self);
        }
    }

  set<revision_id> frontier;

  if (app.revision_selectors.size() == 0)
    {
      revision_id rid;
      get_revision_id(rid);
      frontier.insert(rid);
    }
  else
    {
      for (std::vector<utf8>::const_iterator i = app.revision_selectors.begin();
           i != app.revision_selectors.end(); i++) {
        revision_id rid;
        complete(app, (*i)(), rid);
        frontier.insert(rid);
      }
    }

  cert_name author_name(author_cert_name);
  cert_name date_name(date_cert_name);
  cert_name branch_name(branch_cert_name);
  cert_name tag_name(tag_cert_name);
  cert_name changelog_name(changelog_cert_name);
  cert_name comment_name(comment_cert_name);

  set<revision_id> seen;
  long last = app.last;

  revision_set rev;
  while(! frontier.empty() && (last == -1 || last > 0))
    {
      set<revision_id> next_frontier;
      
      for (set<revision_id>::const_iterator i = frontier.begin();
           i != frontier.end(); ++i)
        { 
          revision_id rid = *i;

          bool print_this = nodes.empty();
          set<  revision<id> > parents;
          vector< revision<cert> > tmp;

          if (!app.db.revision_exists(rid))
            {
              L(F("revision %s does not exist in db, skipping\n") % rid);
              continue;
            }

          if (seen.find(rid) != seen.end())
            continue;

          seen.insert(rid);
          app.db.get_revision(rid, rev);

          if (!nodes.empty())
            {
              set<node_id> nodes_changed;
              set<node_id> nodes_born;
              bool any_node_hit = false;
              select_nodes_modified_by_rev(rid, rev, 
                                           nodes_changed, 
                                           nodes_born,
                                           app);
              for (set<node_id>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
                {
                  if (nodes_changed.find(*n) != nodes_changed.end())
                    {
                      any_node_hit = true;
                      break;
                    }
                }
              for (set<node_id>::const_iterator n = nodes_born.begin(); n != nodes_born.end();
                   ++n)
                nodes.erase(*n);

              if (!any_node_hit)
                print_this = false;
            }

          changes_summary csum;
          
          set<revision_id> ancestors;

          for (edge_map::const_iterator e = rev.edges.begin();
               e != rev.edges.end(); ++e)
            {
              ancestors.insert(edge_old_revision(e));
              next_frontier.insert(edge_old_revision(e));
              csum.add_change_set(edge_changes(e));
            }

          if (app.no_merges && rev.is_merge_node())
            print_this = false;
          
          if (print_this)
          {
            if (global_sanity.brief)
              {
                cout << rid;
                log_certs(app, rid, author_name);
                log_certs(app, rid, date_name);
                log_certs(app, rid, branch_name);
                cout << endl;
              }
            else
              {
                cout << "-----------------------------------------------------------------"
                     << endl;
                cout << "Revision: " << rid << endl;

                for (set<revision_id>::const_iterator anc = ancestors.begin();
                     anc != ancestors.end(); ++anc)
                  cout << "Ancestor: " << *anc << endl;

                log_certs(app, rid, author_name, "Author: ", false);
                log_certs(app, rid, date_name,   "Date: ",   false);
                log_certs(app, rid, branch_name, "Branch: ", false);
                log_certs(app, rid, tag_name,    "Tag: ",    false);

                if (! csum.cs.empty())
                  {
                    cout << endl;
                    csum.print(cout, 70);
                    cout << endl;
                  }

                log_certs(app, rid, changelog_name, "ChangeLog: ", true);
                log_certs(app, rid, comment_name,   "Comments: ",  true);
              }

            if (app.diffs)
              {
                for (edge_map::const_iterator e = rev.edges.begin();
                     e != rev.edges.end(); ++e)
                  {
                    dump_diffs(edge_changes(e), app, true, unified_diff);
                  }
              }

            if (last > 0)
              {
                last--;
              }
          }
        }
      frontier = next_frontier;
    }
}

CMD(setup, N_("tree"), N_("DIRECTORY"), N_("setup a new working copy directory"),
    OPT_BRANCH_NAME)
{
  if (args.size() != 1)
    throw usage(name);

  N(!app.branch_name().empty(), F("need --branch argument for setup"));
  app.db.ensure_open();

  string dir = idx(args,0)();
  app.create_working_copy(dir);
  revision_id null;
  put_revision_id(null);
}

CMD(automate, N_("automation"),
    N_("interface_version\n"
      "heads [BRANCH]\n"
      "ancestors REV1 [REV2 [REV3 [...]]]\n"
      "attributes [FILE]\n"
      "parents REV\n"
      "descendents REV1 [REV2 [REV3 [...]]]\n"
      "children REV\n"
      "graph\n"
      "erase_ancestors [REV1 [REV2 [REV3 [...]]]]\n"
      "toposort [REV1 [REV2 [REV3 [...]]]]\n"
      "ancestry_difference NEW_REV [OLD_REV1 [OLD_REV2 [...]]]\n"
      "leaves\n"
      "inventory\n"
      "stdio\n"
      "certs REV\n"
      "select SELECTOR\n"
      "get_file ID\n"
      "get_manifest [ID]\n"
      "get_revision [ID]\n"
      "keys\n"),
    N_("automation interface"), 
    OPT_NONE)
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  utf8 cmd = *i;
  ++i;
  vector<utf8> cmd_args(i, args.end());

  automate_command(cmd, cmd_args, name, app, cout);
}

CMD(set, N_("vars"), N_("DOMAIN NAME VALUE"),
    N_("set the database variable NAME to VALUE, in domain DOMAIN"),
    OPT_NONE)
{
  if (args.size() != 3)
    throw usage(name);

  var_domain d;
  var_name n;
  var_value v;
  internalize_var_domain(idx(args, 0), d);
  n = var_name(idx(args, 1)());
  v = var_value(idx(args, 2)());
  app.db.set_var(std::make_pair(d, n), v);
}

CMD(unset, N_("vars"), N_("DOMAIN NAME"),
    N_("remove the database variable NAME in domain DOMAIN"),
    OPT_NONE)
{
  if (args.size() != 2)
    throw usage(name);

  var_domain d;
  var_name n;
  internalize_var_domain(idx(args, 0), d);
  n = var_name(idx(args, 1)());
  var_key k(d, n);
  N(app.db.var_exists(k), F("no var with name %s in domain %s") % n % d);
  app.db.clear_var(k);
}

CMD(get_roster, N_("debug"), N_("REVID"),
    N_("dump the roster associated with the given REVID"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  revision_id rid;
  complete(app, idx(args, 0)(), rid);
      
  roster_t roster;
  marking_map mm;
  app.db.get_roster(rid, roster, mm);
  
  data dat;
  write_roster_and_marking(roster, mm, dat);
  cout << dat;
}

}; // namespace commands
