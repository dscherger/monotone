// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "selectors.hh"

#include "sanity.hh"
#include "constants.hh"
#include "database.hh"
#include "app_state.hh"
#include "project.hh"
#include "revision.hh"
#include "globish.hh"
#include "cmd.hh"
#include "work.hh"
#include "transforms.hh"
#include "roster.hh"
#include "vector.hh"
#include "vocab_cast.hh"

#include <algorithm>
#include <boost/shared_ptr.hpp>
#include <boost/tokenizer.hpp>

using std::make_pair;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::set_intersection;
using std::inserter;

using boost::shared_ptr;

void
diagnose_ambiguous_expansion(options const & opts, lua_hooks & lua,
                             project_t & project,
                             string const & str,
                             set<revision_id> const & completions)
{
  if (completions.size() <= 1)
    return;

  string err = (F("selection '%s' has multiple ambiguous expansions:")
                % str).str();
  for (set<revision_id>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    err += ("\n" + describe_revision(opts, lua, project, *i));

  E(false, origin::user, i18n_format(err));
}

class selector
{
public:
  static shared_ptr<selector> create(options const & opts,
                                     lua_hooks & lua,
                                     project_t & project,
                                     string const & orig);
  static shared_ptr<selector> create_simple_selector(options const & opts,
                                                     lua_hooks & lua,
                                                     project_t & project,
                                                     string const & orig);
  virtual set<revision_id> complete(project_t & project) = 0;
  virtual ~selector();
};
selector::~selector() { }

class author_selector : public selector
{
  string value;
public:
  author_selector(string const & arg) : value(arg) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_cert(author_cert_name(), value, ret);
    return ret;
  }
};
class key_selector : public selector
{
  key_identity_info identity;
public:
  key_selector(string const & arg, lua_hooks & lua, project_t & project)
  {
    E(!arg.empty(), origin::user,
      F("the key selector k: must not be empty"));

    project.get_key_identity(lua,
                             external_key_name(arg, origin::user),
                             identity);
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_key(identity.id, ret);
    return ret;
  }
};
class branch_selector : public selector
{
  string value;
public:
  branch_selector(string const & arg, options const & opts) : value(arg)
  {
    if (value.empty())
      {
        workspace::require_workspace(F("the empty branch selector b: refers to the current branch"));
        value = opts.branch();
      }
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_cert(branch_cert_name(), value, ret);
    return ret;
  }
};
class cert_selector : public selector
{
  string value;
public:
  cert_selector(string const & arg) : value(arg)
  {
    E(!value.empty(), origin::user,
      F("the cert selector c: may not be empty"));
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    size_t equals = value.find("=");
    if (equals == string::npos)
      project.db.select_cert(value, ret);
    else
      project.db.select_cert(value.substr(0, equals), value.substr(equals + 1), ret);
    return ret;
  }
};
string preprocess_date_for_selector(string sel, lua_hooks & lua, bool equals)
{
  string tmp;
  if (lua.hook_exists("expand_date"))
    {
      E(lua.hook_expand_date(sel, tmp), origin::user,
        F("selector '%s' is not a valid date\n") % sel);
    }
  else
    {
      tmp = sel;
    }
  // if we still have a too short datetime string, expand it with
  // default values, but only if the type is earlier or later;
  // for searching a specific date cert this makes no sense
  // FIXME: this is highly speculative if expand_date wasn't called
  // beforehand - tmp could be _anything_ but a partial date string
  if (tmp.size()<8 && !equals)
    tmp += "-01T00:00:00";
  else if (tmp.size()<11 && !equals)
    tmp += "T00:00:00";
  E(tmp.size()==19 || equals, origin::user,
    F("selector '%s' is not a valid date (%s)") % sel % tmp);

  if (sel != tmp)
    {
      P (F ("expanded date '%s' -> '%s'\n") % sel % tmp);
      sel = tmp;
    }
  if (equals && sel.size() < 19)
    sel = string("*") + sel + "*"; // to be GLOBbed later
  return sel;
}
class date_selector : public selector
{
  string value;
public:
  date_selector(string const & arg, lua_hooks & lua)
    : value(preprocess_date_for_selector(arg, lua, true)) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_date(value, "GLOB", ret);
    return ret;
  }
};
class earlier_than_selector : public selector
{
  string value;
public:
  earlier_than_selector(string const & arg, lua_hooks & lua)
    : value(preprocess_date_for_selector(arg, lua, false)) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_date(value, "<=", ret);
    return ret;
  }
};
class head_selector : public selector
{
  string value;
  bool ignore_suspend;
public:
  head_selector(string const & arg, options const & opts)
    : value(arg)
  {
    if (value.empty())
      {
        workspace::require_workspace(F("the empty head selector h: refers to "
                                       "the head of the current branch"));
        value = opts.branch();
      }
    ignore_suspend = opts.ignore_suspend_certs;
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;

    set<branch_name> branch_names;
    project.get_branch_list(globish(value, origin::user), branch_names);

    L(FL("found %d matching branches") % branch_names.size());

    // for each branch name, get the branch heads
    for (set<branch_name>::const_iterator bn = branch_names.begin();
         bn != branch_names.end(); bn++)
      {
        set<revision_id> branch_heads;
        project.get_branch_heads(*bn, branch_heads, ignore_suspend);
        ret.insert(branch_heads.begin(), branch_heads.end());
        L(FL("after get_branch_heads for %s, heads has %d entries")
          % (*bn) % ret.size());
      }

    return ret;
  }
};
class ident_selector : public selector
{
  string value;
public:
  ident_selector(string const & arg) : value(arg) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.complete(value, ret);
    return ret;
  }
  bool is_full_length() const
  {
    return value.size() == constants::idlen;
  }
  revision_id get_assuming_full_length() const
  {
    return decode_hexenc_as<revision_id>(value, origin::user);
  }
};
class later_than_selector : public selector
{
  string value;
public:
  later_than_selector(string const & arg, lua_hooks & lua)
    : value(preprocess_date_for_selector(arg, lua, false)) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_date(value, ">", ret);
    return ret;
  }
};
class message_selector : public selector
{
  string value;
public:
  message_selector(string const & arg) : value(arg) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> changelogs, comments;
    project.db.select_cert(changelog_cert_name(), value, changelogs);
    project.db.select_cert(comment_cert_name(), value, comments);

    changelogs.insert(comments.begin(), comments.end());
    return changelogs;
  }
};
class parent_selector : public selector
{
  string value;
public:
  parent_selector(string const & arg,
                  options const & opts,
                  lua_hooks & lua,
                  project_t & project)
    : value(arg)
  {
    if (value.empty())
      {
        workspace work(lua, F("the empty parent selector p: refers to "
                              "the base revision of the workspace"));

        parent_map parents;
        set<revision_id> parent_ids;

        work.get_parent_rosters(project.db, parents);

        for (parent_map::const_iterator i = parents.begin();
             i != parents.end(); ++i)
          {
            parent_ids.insert(i->first);
          }

        diagnose_ambiguous_expansion(opts, lua, project, "p:", parent_ids);
        value = encode_hexenc((* parent_ids.begin()).inner()(),
                              origin::internal);

      }
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_parent(value, ret);
    return ret;
  }
};
class tag_selector : public selector
{
  string value;
public:
  tag_selector(string const & arg) : value(arg) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_cert(tag_cert_name(), value, ret);
    return ret;
  }
};
class update_selector : public selector
{
  string value;
public:
  update_selector(string const & arg, lua_hooks & lua)
  {
    E(arg.empty(), origin::user,
      F("no value is allowed with the update selector u:"));

    workspace work(lua, F("the update selector u: refers to the "
                          "revision before the last update in the "
                          "workspace"));
    revision_id update_id;
    work.get_update_id(update_id);
    value = encode_hexenc(update_id.inner()(), origin::internal);
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.complete(value, ret);
    return ret;
  }
};
class working_base_selector : public selector
{
  set<revision_id> ret;
public:
  working_base_selector(string const & arg, project_t & project, lua_hooks & lua)
  {
    E(arg.empty(), origin::user,
      F("no value is allowed with the base revision selector w:"));

    workspace work(lua, F("the selector w: returns the "
                          "base revision(s) of the workspace"));
    parent_map parents;
    work.get_parent_rosters(project.db, parents);

    for (parent_map::const_iterator i = parents.begin();
         i != parents.end(); ++i)
      {
        ret.insert(i->first);
      }
  }
  virtual set<revision_id> complete(project_t & project)
  {
    return ret;
  }
};

class unknown_selector : public selector
{
  string value;
public:
  unknown_selector(string const & arg) : value(arg) {}
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    project.db.select_author_tag_or_branch(value, ret);
    return ret;
  }
};

class or_selector : public selector
{
  vector<shared_ptr<selector> > members;
public:
  void add(shared_ptr<selector> s)
  {
    members.push_back(s);
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    for (vector<shared_ptr<selector> >::const_iterator i = members.begin();
         i != members.end(); ++i)
      {
        set<revision_id> current = (*i)->complete(project);
        ret.insert(current.begin(), current.end());
      }
    return ret;
  }
};
class and_selector : public selector
{
  vector<shared_ptr<selector> > members;
public:
  void add(shared_ptr<selector> s)
  {
    members.push_back(s);
  }
  virtual set<revision_id> complete(project_t & project)
  {
    set<revision_id> ret;
    bool first = true;
    for (vector<shared_ptr<selector> >::const_iterator i = members.begin();
         i != members.end(); ++i)
      {
        set<revision_id> current = (*i)->complete(project);
        if (first)
          {
            first = false;
            ret = current;
          }
        else
          {
            set<revision_id> intersection;
            set_intersection(ret.begin(), ret.end(),
                             current.begin(), current.end(),
                             inserter(intersection, intersection.end()));
            ret = intersection;
          }
      }
    return ret;
  }
};
class nested_selector : public selector
{
  shared_ptr<selector> s;
public:
  nested_selector(shared_ptr<selector> s) : s(s) {}
  virtual set<revision_id> complete(project_t & project)
  {
    return s->complete(project);
  }
};

set<revision_id> get_ancestors(project_t const & project,
                               set<revision_id> frontier)
{
  set<revision_id> ret;
  while (!frontier.empty())
    {
      revision_id revid = *frontier.begin();
      frontier.erase(frontier.begin());
      set<revision_id> p;
      project.db.get_revision_parents(revid, p);
      for (set<revision_id>::const_iterator i = p.begin();
           i != p.end(); ++i)
        {
          if (null_id(*i))
            continue;
          pair<set<revision_id>::iterator, bool> x = ret.insert(*i);
          if (x.second)
            frontier.insert(*i);
        }
    }
  return ret;
}

static void
diagnose_wrong_arg_count(string const & func, int expected, int actual)
{
  E(expected == actual, origin::user,
    FP("the '%s' function takes %d argument, not %d",
       "the '%s' function takes %d arguments, not %d",
       expected)
      % func % expected % actual);
}

class fn_selector : public selector
{
  string name;
  vector<shared_ptr<selector> > args;
public:
  fn_selector(string const & fn_name) : name(fn_name) {}
  void add(shared_ptr<selector> s)
  {
    args.push_back(s);
  }
  virtual set<revision_id> complete(project_t & project)
  {
    if (name == "difference")
      {
        diagnose_wrong_arg_count("difference", 2, args.size());
        set<revision_id> lhs = args[0]->complete(project);
        set<revision_id> rhs = args[1]->complete(project);

        set<revision_id> ret;
        set_difference(lhs.begin(), lhs.end(),
                       rhs.begin(), rhs.end(),
                       inserter(ret, ret.end()));
        return ret;
      }
    else if (name == "lca")
      {
        diagnose_wrong_arg_count("lca", 2, args.size());
        set<revision_id> lhs_heads = args[0]->complete(project);
        set<revision_id> rhs_heads = args[1]->complete(project);
        set<revision_id> lhs = get_ancestors(project, lhs_heads);
        set<revision_id> rhs = get_ancestors(project, rhs_heads);
        lhs.insert(lhs_heads.begin(), lhs_heads.end());
        rhs.insert(rhs_heads.begin(), rhs_heads.end());
        set<revision_id> common;
        set_intersection(lhs.begin(), lhs.end(),
                         rhs.begin(), rhs.end(),
                         inserter(common, common.end()));
        erase_ancestors(project.db, common);
        return common;
      }
    else if (name == "max")
      {
        diagnose_wrong_arg_count("max", 1, args.size());
        set<revision_id> ret = args[0]->complete(project);
        erase_ancestors(project.db, ret);
        return ret;
      }
    else if (name == "ancestors")
      {
        diagnose_wrong_arg_count("ancestors", 1, args.size());
        return get_ancestors(project, args[0]->complete(project));
      }
    else if (name == "descendants")
      {
        diagnose_wrong_arg_count("descendants", 1, args.size());
        set<revision_id> frontier = args[0]->complete(project);
        set<revision_id> ret;
        while (!frontier.empty())
          {
            revision_id revid = *frontier.begin();
            frontier.erase(frontier.begin());
            set<revision_id> c;
            project.db.get_revision_children(revid, c);
            for (set<revision_id>::const_iterator i = c.begin();
                 i != c.end(); ++i)
              {
                if (null_id(*i))
                  continue;
                pair<set<revision_id>::iterator, bool> x = ret.insert(*i);
                if (x.second)
                  frontier.insert(*i);
              }
          }
        return ret;
      }
    else if (name == "parents")
      {
        diagnose_wrong_arg_count("parents", 1, args.size());
        set<revision_id> ret;
        set<revision_id> tmp = args[0]->complete(project);
        for (set<revision_id>::const_iterator i = tmp.begin();
             i != tmp.end(); ++i)
          {
            set<revision_id> p;
            project.db.get_revision_parents(*i, p);
            ret.insert(p.begin(), p.end());
          }
        ret.erase(revision_id());
        return ret;
      }
    else if (name == "children")
      {
        diagnose_wrong_arg_count("children", 1, args.size());
        set<revision_id> ret;
        set<revision_id> tmp = args[0]->complete(project);
        for (set<revision_id>::const_iterator i = tmp.begin();
             i != tmp.end(); ++i)
          {
            set<revision_id> c;
            project.db.get_revision_children(*i, c);
            ret.insert(c.begin(), c.end());
          }
        ret.erase(revision_id());
        return ret;
      }
    else if (name == "pick")
      {
        diagnose_wrong_arg_count("pick", 1, args.size());
        set<revision_id> tmp = args[0]->complete(project);
        set<revision_id> ret;
        if (!tmp.empty())
          ret.insert(*tmp.begin());
        return ret;
      }
    else
      {
        E(false, origin::user,
          F("unknown selection function '%s'") % name);
      }
  }
};

struct parse_item
{
  shared_ptr<selector> sel;
  string str;
  explicit parse_item(shared_ptr<selector> const & s) : sel(s) { }
  explicit parse_item(string const & s) : str(s) { }
};

shared_ptr<selector>
selector::create_simple_selector(options const & opts,
                                 lua_hooks & lua,
                                 project_t & project,
                                 string const & orig)
{
  string sel = orig;
  if (sel.find_first_not_of(constants::legal_id_bytes) == string::npos
      && sel.size() == constants::idlen)
    return shared_ptr<selector>(new ident_selector(sel));

  if (sel.size() < 2 || sel[1] != ':')
    {
      string tmp;
      if (!lua.hook_expand_selector(sel, tmp))
        {
          L(FL("expansion of selector '%s' failed") % sel);
        }
      else
        {
          P(F("expanded selector '%s' -> '%s'") % sel % tmp);
          sel = tmp;
        }
    }
  if (sel.size() < 2 || sel[1] != ':')
    return shared_ptr<selector>(new unknown_selector(sel));
  char sel_type = sel[0];
  sel.erase(0,2);
  switch (sel_type)
    {
    case 'a':
      return shared_ptr<selector>(new author_selector(sel));
    case 'b':
      return shared_ptr<selector>(new branch_selector(sel, opts));
    case 'c':
      return shared_ptr<selector>(new cert_selector(sel));
    case 'd':
      return shared_ptr<selector>(new date_selector(sel, lua));
    case 'e':
      return shared_ptr<selector>(new earlier_than_selector(sel, lua));
    case 'h':
      return shared_ptr<selector>(new head_selector(sel, opts));
    case 'i':
      return shared_ptr<selector>(new ident_selector(sel));
    case 'k':
      return shared_ptr<selector>(new key_selector(sel, lua, project));
    case 'l':
      return shared_ptr<selector>(new later_than_selector(sel, lua));
    case 'm':
      return shared_ptr<selector>(new message_selector(sel));
    case 'p':
      return shared_ptr<selector>(new parent_selector(sel, opts, lua, project));
    case 't':
      return shared_ptr<selector>(new tag_selector(sel));
    case 'u':
      return shared_ptr<selector>(new update_selector(sel, lua));
    case 'w':
      return shared_ptr<selector>(new working_base_selector(sel, project, lua));
    default:
      E(false, origin::user, F("unknown selector type: %c") % sel_type);
    }
}

shared_ptr<selector> selector::create(options const & opts,
                                      lua_hooks & lua,
                                      project_t & project,
                                      string const & orig)
{
  // I would try to use lex/yacc for this, but they kinda look like a mess
  // with lots of global variables and icky macros and such in the output.
  // Using bisonc++ with flex in c++ mode might be better, except that
  // bisonc++ is GPLv3 *without* (as far as I can see) an exception for use
  // of the parser skeleton as included in the output.
  string const special_chars("();\\/|");
  boost::char_separator<char> splitter("", special_chars.c_str());
  typedef boost::tokenizer<boost::char_separator<char> > tokenizer_t;
  tokenizer_t tokenizer(orig, splitter);
  vector<string> splitted;
  L(FL("tokenizing selector '%s'") % orig);
  bool dont_advance = false;
  for (tokenizer_t::const_iterator iter = tokenizer.begin();
       iter != tokenizer.end(); dont_advance || (++iter, true))
    {
      dont_advance = false;
      string const & val = *iter;
      if (val != "\\")
        splitted.push_back(val);
      else
        {
          if (splitted.empty())
            splitted.push_back(string());

          ++iter;
          E(iter != tokenizer.end(), origin::user,
            F("selector '%s' is invalid, it ends with the escape character '\\'")
            % orig);
          string const & val2 = *iter;
          I(!val2.empty());
          E(special_chars.find(val2) != string::npos, origin::user,
            F("selector '%s' is invalid, it contains an unknown escape sequence '%s%s'")
            % val % '\\' % val2.substr(0,1));
          splitted.back().append(val2);

          ++iter;
          if (iter != tokenizer.end())
            {
              string const & val3 = *iter;
              if (val3.size() != 1 || special_chars.find(val3) == string::npos)
                splitted.back().append(val3);
              else
                dont_advance = true;
            }
        }
    }
  if (splitted.empty())
    splitted.push_back(string());
  for (vector<string>::const_iterator i = splitted.begin();
       i != splitted.end(); ++i)
    {
      L(FL("tokens: '%s'") % *i);
    }

  vector<parse_item> items;
  size_t tok_num = 0;
  for (vector<string>::const_iterator tok = splitted.begin();
       tok != splitted.end(); ++tok)
    {
      L(FL("Processing token number %d: '%s'") % tok_num % *tok);
      ++tok_num;
      if (*tok == "(") {
        items.push_back(parse_item(*tok));
      } else if (*tok == ")") {
        unsigned int lparen_pos = 1;
        while (lparen_pos <= items.size() && idx(items, items.size() - lparen_pos).str != "(")
          {
            ++lparen_pos;
          }
        E(lparen_pos < items.size(), origin::user,
          F("selector '%s' is invalid, unmatched ')'") % orig);
        I(idx(items, items.size() - lparen_pos).str == "(");
        unsigned int name_idx = items.size() - lparen_pos - 1;
        if (lparen_pos < items.size() && !idx(items, name_idx).str.empty()
            && special_chars.find(idx(items, name_idx).str) == string::npos)
          {
            // looks like a function call
            shared_ptr<fn_selector> to_add(new fn_selector(idx(items, name_idx).str));
            L(FL("found function-like selector '%s' at stack position %d of %d")
              % items[name_idx].str % name_idx % items.size());
            // note the closing paren is not on the item stack
            for (unsigned int i = items.size() - lparen_pos + 1;
                 i < items.size(); i += 2)
              {
                L(FL("        found argument at stack position %d") % i);
                shared_ptr<selector> arg = idx(items,i).sel;
                E(i == items.size() - 1 || idx(items,i+1).str == ";", origin::user,
                  F("selector '%s' is invalid, function argument doesn't look like an arg-list"));
                to_add->add(arg);
              }
            while (name_idx < items.size())
              items.pop_back();
            items.push_back(parse_item(to_add));
          }
        else
          {
            // just parentheses for grouping, closing paren is not on the item stack
            E(lparen_pos == 2 && idx(items, items.size() - 1).sel, origin::user,
              F("selector '%s' is invalid, grouping parentheses contain something that "
                "doesn't look like an expr") % orig);
            shared_ptr<selector> to_add(new nested_selector(idx(items, items.size() - 1).sel));
            items.pop_back();
            items.pop_back();
            items.push_back(parse_item(to_add));
          }
      } else if (*tok == ";") {
        items.push_back(parse_item(*tok));
      } else if (*tok == "/") {
        E(!items.empty(), origin::user,
          F("selector '%s' is invalid, because it starts with a '/'") % orig);
        items.push_back(parse_item(*tok));
      } else if (*tok == "|") {
        E(!items.empty(), origin::user,
          F("selector '%s' is invalid, because it starts with a '|'") % orig);
        items.push_back(parse_item(*tok));
      } else {
        vector<string>::const_iterator next = tok;
        ++next;
        bool next_is_oparen = false;
        if (next != splitted.end())
          next_is_oparen = (*next == "(");
        if (next_is_oparen)
          items.push_back(parse_item(*tok));
        else
          items.push_back(parse_item(create_simple_selector(opts, lua,
                                                            project,
                                                            *tok)));
      }

      // may have an infix operator to reduce
      if (items.size() >= 3 && items.back().sel)
        {
          string op = idx(items, items.size() - 2).str;
          if (op == "|" || op == "/")
            {
              shared_ptr<selector> lhs = idx(items, items.size() - 3).sel;
              shared_ptr<selector> rhs = idx(items, items.size() - 1).sel;
              E(lhs, origin::user,
                F("selector '%s is invalid, because there is a '%s' someplace it shouldn't be")
                % orig % op);
              shared_ptr<or_selector> lhs_as_or = boost::dynamic_pointer_cast<or_selector>(lhs);
              shared_ptr<and_selector> lhs_as_and = boost::dynamic_pointer_cast<and_selector>(lhs);
              E(op == "/" || !lhs_as_and, origin::user,
                F("selector '%s' is invalid, don't mix '/' and '|' operators without parentheses")
                % orig);
              E(op == "|" || !lhs_as_or, origin::user,
                F("selector '%s' is invalid, don't mix '/' and '|' operators without parentheses")
                % orig);
              shared_ptr<selector> new_item;
              if (lhs_as_or)
                {
                  lhs_as_or->add(rhs);
                  new_item = lhs;
                }
              else if (lhs_as_and)
                {
                  lhs_as_and->add(rhs);
                  new_item = lhs;
                }
              else
                {
                  if (op == "/")
                    {
                      shared_ptr<and_selector> x(new and_selector());
                      x->add(lhs);
                      x->add(rhs);
                      new_item = x;
                    }
                  else
                    {
                      shared_ptr<or_selector> x(new or_selector());
                      x->add(lhs);
                      x->add(rhs);
                      new_item = x;
                    }
                }
              I(new_item);
              items.pop_back();
              items.pop_back();
              items.pop_back();
              items.push_back(parse_item(new_item));
            }
        }
    }
  E(items.size() == 1 && items[0].sel, origin::user,
    F("selector '%s' is invalid, it doesn't look like an expr") % orig);
  return items[0].sel;
}


void
complete(options const & opts, lua_hooks & lua,
         project_t & project,
         string const & str,
         set<revision_id> & completions)
{
  shared_ptr<selector> sel = selector::create(opts, lua, project, str);

  // avoid logging if there's no expansion to be done
  shared_ptr<ident_selector> isel = boost::dynamic_pointer_cast<ident_selector>(sel);
  if (isel && isel->is_full_length())
    {
      completions.insert(isel->get_assuming_full_length());
      E(project.db.revision_exists(*completions.begin()), origin::user,
        F("no revision %s found in database") % *completions.begin());
      return;
    }

  P(F("expanding selection '%s'") % str);
  completions = sel->complete(project);

  E(!completions.empty(), origin::user,
    F("no match for selection '%s'") % str);

  for (set<revision_id>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    {
      P(F("expanded to '%s'") % *i);

      // This may be impossible, but let's make sure.
      // All the callers used to do it.
      E(project.db.revision_exists(*i), origin::user,
        F("no revision %s found in database") % *i);
    }
}

void
complete(options const & opts, lua_hooks & lua,
         project_t & project,
         string const & str,
         revision_id & completion)
{
  set<revision_id> completions;

  complete(opts, lua, project, str, completions);

  I(!completions.empty());
  diagnose_ambiguous_expansion(opts, lua, project, str, completions);

  completion = *completions.begin();
}


void
expand_selector(options const & opts, lua_hooks & lua,
                project_t & project,
                string const & str,
                set<revision_id> & completions)
{
  shared_ptr<selector> sel = selector::create(opts, lua, project, str);
  completions = sel->complete(project);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
