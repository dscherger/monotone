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
#include "globish.hh"
#include "cmd.hh"
#include "work.hh"
#include "transforms.hh"

#include <algorithm>
#include <boost/tokenizer.hpp>

using std::make_pair;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::set_intersection;
using std::inserter;

enum selector_type
  {
    sel_author,
    sel_branch,
    sel_head,
    sel_any_head,
    sel_date,
    sel_tag,
    sel_ident,
    sel_cert,
    sel_earlier,
    sel_later,
    sel_message,
    sel_parent,
    sel_update,
    sel_base,
    sel_unknown
  };

typedef vector<pair<selector_type, string> > selector_list;

static void
decode_selector(options const & opts, lua_hooks & lua,
                project_t & project,
                string const & orig_sel,
                selector_type & type,
                string & sel)
{
  sel = orig_sel;

  L(FL("decoding selector '%s'") % sel);

  string tmp;
  if (sel.size() < 2 || sel[1] != ':')
    {
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

  if (sel.size() >= 2 && sel[1] == ':')
    {
      switch (sel[0])
        {
        case 'a':
          type = sel_author;
          break;
        case 'b':
          type = sel_branch;
          break;
        case 'h':
          type = opts.ignore_suspend_certs ? sel_any_head : sel_head;
          break;
        case 'd':
          type = sel_date;
          break;
        case 'i':
          type = sel_ident;
          break;
        case 't':
          type = sel_tag;
          break;
        case 'c':
          type = sel_cert;
          break;
        case 'l':
          type = sel_later;
          break;
        case 'e':
          type = sel_earlier;
          break;
        case 'm':
          type = sel_message;
          break;
        case 'p':
          type = sel_parent;
          break;
        case 'u':
          type = sel_update;
          break;
        case 'w':
          type = sel_base;
          break;
        default:
          E(false, origin::user, F("unknown selector type: %c") % sel[0]);
        }
      sel.erase(0,2);

      // validate certain selector values and provide defaults
      switch (type)
        {
        case sel_date:
        case sel_later:
        case sel_earlier:
          if (lua.hook_exists("expand_date"))
            {
              E(lua.hook_expand_date(sel, tmp), origin::user,
                F("selector '%s' is not a valid date\n") % sel);
            }
          else
            {
              // if expand_date is not available, start with something
              tmp = sel;
            }

          // if we still have a too short datetime string, expand it with
          // default values, but only if the type is earlier or later;
          // for searching a specific date cert this makes no sense
          // FIXME: this is highly speculative if expand_date wasn't called
          // beforehand - tmp could be _anything_ but a partial date string
          if (tmp.size()<8 && (sel_later==type || sel_earlier==type))
            tmp += "-01T00:00:00";
          else if (tmp.size()<11 && (sel_later==type || sel_earlier==type))
            tmp += "T00:00:00";
          E(tmp.size()==19 || sel_date==type, origin::user,
            F("selector '%s' is not a valid date (%s)") % sel % tmp);

          if (sel != tmp)
            {
              P (F ("expanded date '%s' -> '%s'\n") % sel % tmp);
              sel = tmp;
            }
          if (sel_date == type && sel.size() < 19)
            sel = string("*") + sel + "*"; // to be GLOBbed later
          break;

        case sel_branch:
        case sel_head:
        case sel_any_head:
          if (sel.empty())
            {
              i18n_format msg = sel_branch == type
                ? F("the empty branch selector b: refers to "
                    "the current branch")
                : F("the empty head selector h: refers to "
                    "the head of the current branch");
              workspace::require_workspace(msg);
              sel = opts.branch();
            }
          break;

        case sel_cert:
          E(!sel.empty(), origin::user,
            F("the cert selector c: may not be empty"));
          break;

        case sel_parent:
          if (sel.empty())
            {
              workspace work(opts, lua, F("the empty parent selector p: refers to "
                                          "the base revision of the workspace"));

              parent_map parents;
              set<revision_id> parent_ids;

              work.get_parent_rosters(project.db, parents);

              for (parent_map::const_iterator i = parents.begin();
                i != parents.end(); ++i)
                {
                  parent_ids.insert(i->first);
                }

              diagnose_ambiguous_expansion(project, "p:", parent_ids);
              sel = encode_hexenc((* parent_ids.begin()).inner()(),
                                  origin::internal);
            }
          break;
        case sel_update:
          E(sel.empty(), origin::user,
            F("no value is allowed with the update selector u:"));
          {
            workspace work(opts, lua, F("the update selector u: refers to the "
                                        "revision before the last update in the "
                                        "workspace"));
            revision_id update_id;
            work.get_update_id(update_id);
            sel = encode_hexenc(update_id.inner()(), origin::internal);
          }
          break;
        case sel_base:
          E(sel.empty(), origin::user,
            F("no value is allowed with the base revision selector w:"));
          break;

        default: break;
        }
    }
}

static void
parse_selector(options const & opts, lua_hooks & lua,
               project_t & project,
               string const & str, selector_list & sels)
{
  sels.clear();

  // this rule should always be enabled, even if the user specifies
  // --norc: if you provide a revision id, you get a revision id.
  if (str.find_first_not_of(constants::legal_id_bytes) == string::npos
      && str.size() == constants::idlen)
    {
      sels.push_back(make_pair(sel_ident, str));
    }
  else
    {
      typedef boost::tokenizer<boost::escaped_list_separator<char> > tokenizer;
      boost::escaped_list_separator<char> slash("\\", "/", "");
      tokenizer tokens(str, slash);

      vector<string> selector_strings;
      copy(tokens.begin(), tokens.end(), back_inserter(selector_strings));

      for (vector<string>::const_iterator i = selector_strings.begin();
           i != selector_strings.end(); ++i)
        {
          string sel;
          selector_type type = sel_unknown;

          decode_selector(opts, lua, project,  *i, type, sel);
          sels.push_back(make_pair(type, sel));
        }
    }
}

static void
complete_one_selector(options const & opts, lua_hooks & lua,
                      project_t & project,
                      selector_type ty, string const & value,
                      set<revision_id> & completions)
{
  switch (ty)
    {
    case sel_ident:
      project.db.complete(value, completions);
      break;

    case sel_parent:
      I(!value.empty());
      project.db.select_parent(value, completions);
      break;

    case sel_update:
      project.db.complete(value, completions);
      break;

    case sel_author:
      project.db.select_cert(author_cert_name(), value, completions);
      break;

    case sel_tag:
      project.db.select_cert(tag_cert_name(), value, completions);
      break;

    case sel_branch:
      I(!value.empty());
      project.db.select_cert(branch_cert_name(), value, completions);
      break;

    case sel_unknown:
      project.db.select_author_tag_or_branch(value, completions);
      break;

    case sel_date:
      project.db.select_date(value, "GLOB", completions);
      break;

    case sel_earlier:
      project.db.select_date(value, "<=", completions);
      break;

    case sel_later:
      project.db.select_date(value, ">", completions);
      break;

    case sel_message:
      {
        set<revision_id> changelogs, comments;
        project.db.select_cert(changelog_cert_name(), value, changelogs);
        project.db.select_cert(comment_cert_name(), value, comments);
        completions.insert(changelogs.begin(), changelogs.end());
        completions.insert(comments.begin(), comments.end());
      }
      break;

    case sel_cert:
      {
        I(!value.empty());
        size_t spot = value.find("=");

        if (spot != (size_t)-1)
          {
            string certname;
            string certvalue;

            certname = value.substr(0, spot);
            spot++;
            certvalue = value.substr(spot);

            project.db.select_cert(certname, certvalue, completions);
          }
        else
          project.db.select_cert(value, completions);
      }
      break;

    case sel_head:
    case sel_any_head:
      {
        // get branch names
        set<branch_name> branch_names;
        I(!value.empty());
        project.get_branch_list(globish(value, origin::user), branch_names);

        L(FL("found %d matching branches") % branch_names.size());

        // for each branch name, get the branch heads
        for (set<branch_name>::const_iterator bn = branch_names.begin();
             bn != branch_names.end(); bn++)
          {
            set<revision_id> branch_heads;
            project.get_branch_heads(*bn, branch_heads, ty == sel_any_head);
            completions.insert(branch_heads.begin(), branch_heads.end());
            L(FL("after get_branch_heads for %s, heads has %d entries")
              % (*bn) % completions.size());
          }
      }
      break;

    case sel_base:
      {
        workspace work(opts, lua, F("the selector w: returns the "
                                    "base revision(s) of the workspace"));
        parent_map parents;
        work.get_parent_rosters(project.db, parents);

        for (parent_map::const_iterator i = parents.begin();
             i != parents.end(); ++i)
          {
            completions.insert(i->first);
          }
      }
      break;
    }
}

static void
complete_selector(options const & opts, lua_hooks & lua,
                  project_t & project,
                  selector_list const & limit,
                  set<revision_id> & completions)
{
  if (limit.empty()) // all the ids in the database
    {
      project.db.complete("", completions);
      return;
    }

  selector_list::const_iterator i = limit.begin();
  complete_one_selector(opts, lua, project, i->first, i->second, completions);
  i++;

  while (i != limit.end())
    {
      set<revision_id> candidates;
      set<revision_id> intersection;
      complete_one_selector(opts, lua, project, i->first, i->second, candidates);

      intersection.clear();
      set_intersection(completions.begin(), completions.end(),
                       candidates.begin(), candidates.end(),
                       inserter(intersection, intersection.end()));

      completions = intersection;
      i++;
    }
}

void
complete(options const & opts, lua_hooks & lua,
         project_t & project,
         string const & str,
         set<revision_id> & completions)
{
  selector_list sels;
  parse_selector(opts, lua, project, str, sels);

  // avoid logging if there's no expansion to be done
  if (sels.size() == 1
      && sels[0].first == sel_ident
      && sels[0].second.size() == constants::idlen)
    {
      completions.insert(decode_hexenc_as<revision_id>(sels[0].second, origin::user));
      E(project.db.revision_exists(*completions.begin()), origin::user,
        F("no such revision '%s'") % *completions.begin());
      return;
    }

  P(F("expanding selection '%s'") % str);
  complete_selector(opts, lua, project, sels, completions);

  E(!completions.empty(), origin::user,
    F("no match for selection '%s'") % str);

  for (set<revision_id>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    {
      P(F("expanded to '%s'") % *i);

      // This may be impossible, but let's make sure.
      // All the callers used to do it.
      E(project.db.revision_exists(*i), origin::user,
        F("no such revision '%s'") % *i);
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
  diagnose_ambiguous_expansion(project, str, completions);

  completion = *completions.begin();
}


void
expand_selector(options const & opts, lua_hooks & lua,
                project_t & project,
                string const & str,
                set<revision_id> & completions)
{
  selector_list sels;
  parse_selector(opts, lua, project, str, sels);

  // avoid logging if there's no expansion to be done
  if (sels.size() == 1
      && sels[0].first == sel_ident
      && sels[0].second.size() == constants::idlen)
    {
      completions.insert(decode_hexenc_as<revision_id>(sels[0].second,
                                                       origin::user));
      return;
    }

  complete_selector(opts, lua, project, sels, completions);
}

void
diagnose_ambiguous_expansion(project_t & project,
                             string const & str,
                             set<revision_id> const & completions)
{
  if (completions.size() <= 1)
    return;

  string err = (F("selection '%s' has multiple ambiguous expansions:")
                % str).str();
  for (set<revision_id>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    err += ("\n" + describe_revision(project, *i));

  E(false, origin::user, i18n_format(err));
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
