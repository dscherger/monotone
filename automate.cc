// Copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
//               2007 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <iterator>
#include <sstream>
#include <unistd.h>
#include "vector.hh"

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include "lexical_cast.hh"
#include <boost/tuple/tuple.hpp>

#include "app_state.hh"
#include "project.hh"
#include "basic_io.hh"
#include "cert.hh"
#include "cmd.hh"
#include "commands.hh"
#include "constants.hh"
#include "inodeprint.hh"
#include "keys.hh"
#include "key_store.hh"
#include "file_io.hh"
#include "packet.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "vocab.hh"
#include "globish.hh"
#include "charset.hh"
#include "safe_map.hh"
#include "work.hh"
#include "xdelta.hh"
#include "database.hh"
#include "vocab_cast.hh"

using std::allocator;
using std::basic_ios;
using std::basic_stringbuf;
using std::char_traits;
using std::find;
using std::inserter;
using std::make_pair;
using std::map;
using std::multimap;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::sort;
using std::streamsize;
using std::string;
using std::vector;


// Name: heads
// Arguments:
//   1: branch name (optional, default branch is used if non-existant)
// Added in: 0.0
// Purpose: Prints the heads of the given branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If the branch does not exist, prints nothing.  (There are
//   no heads.)
CMD_AUTOMATE(heads, N_("[BRANCH]"),
             N_("Prints the heads of the given branch"),
             "",
             options::opts::none)
{
  E(args.size() < 2, origin::user,
    F("wrong argument count"));

  database db(app);
  project_t project(db);

  branch_name branch;
  if (args.size() == 1)
    // branchname was explicitly given, use that
    branch = typecast_vocab<branch_name>(idx(args, 0));
  else
    {
      workspace::require_workspace(F("with no argument, this command prints the heads of the workspace's branch"));
      branch = app.opts.branch;
    }

  set<revision_id> heads;
  project.get_branch_heads(branch, heads, app.opts.ignore_suspend_certs);
  for (set<revision_id>::const_iterator i = heads.begin();
       i != heads.end(); ++i)
    output << *i << '\n';
}

// Name: ancestors
// Arguments:
//   1 or more: revision ids
// Added in: 0.2
// Purpose: Prints the ancestors (exclusive) of the given revisions
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(ancestors, N_("REV1 [REV2 [REV3 [...]]]"),
             N_("Prints the ancestors of the given revisions"),
             "",
             options::opts::none)
{
  E(args.size() > 0, origin::user,
    F("wrong argument count"));

  database db(app);

  set<revision_id> ancestors;
  vector<revision_id> frontier;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid(decode_hexenc_as<revision_id>((*i)(), origin::user));
      E(db.revision_exists(rid), origin::user,
        F("no such revision '%s'") % rid);
      frontier.push_back(rid);
    }
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if(!null_id(rid)) {
        set<revision_id> parents;
        db.get_revision_parents(rid, parents);
        for (set<revision_id>::const_iterator i = parents.begin();
             i != parents.end(); ++i)
          {
            if (ancestors.find(*i) == ancestors.end())
              {
                frontier.push_back(*i);
                ancestors.insert(*i);
              }
          }
      }
    }
  for (set<revision_id>::const_iterator i = ancestors.begin();
       i != ancestors.end(); ++i)
    if (!null_id(*i))
      output << *i << '\n';
}


// Name: descendents
// Arguments:
//   1 or more: revision ids
// Added in: 0.1
// Purpose: Prints the descendents (exclusive) of the given revisions
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(descendents, N_("REV1 [REV2 [REV3 [...]]]"),
             N_("Prints the descendents of the given revisions"),
             "",
             options::opts::none)
{
  E(args.size() > 0, origin::user,
    F("wrong argument count"));

  database db(app);

  set<revision_id> descendents;
  vector<revision_id> frontier;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid(decode_hexenc_as<revision_id>((*i)(), origin::user));
      E(db.revision_exists(rid), origin::user,
        F("no such revision '%s'") % rid);
      frontier.push_back(rid);
    }
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      set<revision_id> children;
      db.get_revision_children(rid, children);
      for (set<revision_id>::const_iterator i = children.begin();
           i != children.end(); ++i)
        {
          if (descendents.find(*i) == descendents.end())
            {
              frontier.push_back(*i);
              descendents.insert(*i);
            }
        }
    }
  for (set<revision_id>::const_iterator i = descendents.begin();
       i != descendents.end(); ++i)
    output << *i << '\n';
}


// Name: erase_ancestors
// Arguments:
//   0 or more: revision ids
// Added in: 0.1
// Purpose: Prints all arguments, except those that are an ancestor of some
//   other argument.  One way to think about this is that it prints the
//   minimal elements of the given set, under the ordering imposed by the
//   "child of" relation.  Another way to think of it is if the arguments were
//   a branch, then we print the heads of that branch.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(erase_ancestors, N_("[REV1 [REV2 [REV3 [...]]]]"),
             N_("Erases the ancestors in a list of revisions"),
             "",
             options::opts::none)
{
  database db(app);

  set<revision_id> revs;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid(decode_hexenc_as<revision_id>((*i)(), origin::user));
      E(db.revision_exists(rid), origin::user,
        F("no such revision '%s'") % rid);
      revs.insert(rid);
    }
  erase_ancestors(db, revs);
  for (set<revision_id>::const_iterator i = revs.begin(); i != revs.end(); ++i)
    output << *i << '\n';
}

// Name: toposort
// Arguments:
//   0 or more: revision ids
// Added in: 0.1
// Purpose: Prints all arguments, topologically sorted.  I.e., if A is an
//   ancestor of B, then A will appear before B in the output list.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revisions are printed in topologically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(toposort, N_("[REV1 [REV2 [REV3 [...]]]]"),
             N_("Topologically sorts a list of revisions"),
             "",
             options::opts::none)
{
  database db(app);

  set<revision_id> revs;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid(decode_hexenc_as<revision_id>((*i)(), origin::user));
      E(db.revision_exists(rid), origin::user,
        F("no such revision '%s'") % rid);
      revs.insert(rid);
    }
  vector<revision_id> sorted;
  toposort(db, revs, sorted);
  for (vector<revision_id>::const_iterator i = sorted.begin();
       i != sorted.end(); ++i)
    output << *i << '\n';
}

// Name: ancestry_difference
// Arguments:
//   1: a revision id
//   0 or more further arguments: also revision ids
// Added in: 0.1
// Purpose: Prints all ancestors of the first revision A, that are not also
//   ancestors of the other revision ids, the "Bs".  For purposes of this
//   command, "ancestor" is an inclusive term; that is, A is an ancestor of
//   one of the Bs, it will not be printed, but otherwise, it will be; and
//   none of the Bs will ever be printed.  If A is a new revision, and Bs are
//   revisions that you have processed before, then this command tells you
//   which revisions are new since then.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revisions are printed in topologically sorted order.
// Error conditions: If any of the revisions do not exist, prints nothing to
//   stdout, prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(ancestry_difference, N_("NEW_REV [OLD_REV1 [OLD_REV2 [...]]]"),
             N_("Lists the ancestors of the first revision given, not in "
                "the others"),
             "",
             options::opts::none)
{
  E(args.size() > 0, origin::user,
    F("wrong argument count"));

  database db(app);

  revision_id a;
  set<revision_id> bs;
  args_vector::const_iterator i = args.begin();
  a = decode_hexenc_as<revision_id>((*i)(), origin::user);
  E(db.revision_exists(a), origin::user,
    F("no such revision '%s'") % a);
  for (++i; i != args.end(); ++i)
    {
      revision_id b(decode_hexenc_as<revision_id>((*i)(), origin::user));
      E(db.revision_exists(b), origin::user,
        F("no such revision '%s'") % b);
      bs.insert(b);
    }
  set<revision_id> ancestors;
  ancestry_difference(db, a, bs, ancestors);

  vector<revision_id> sorted;
  toposort(db, ancestors, sorted);
  for (vector<revision_id>::const_iterator i = sorted.begin();
       i != sorted.end(); ++i)
    output << *i << '\n';
}

// Name: leaves
// Arguments:
//   None
// Added in: 0.1
// Purpose: Prints the leaves of the revision graph, i.e., all revisions that
//   have no children.  This is similar, but not identical to the
//   functionality of 'heads', which prints every revision in a branch, that
//   has no descendents in that branch.  If every revision in the database was
//   in the same branch, then they would be identical.  Generally, every leaf
//   is the head of some branch, but not every branch head is a leaf.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: None.
CMD_AUTOMATE(leaves, "",
             N_("Lists the leaves of the revision graph"),
             "",
             options::opts::none)
{
  E(args.size() == 0, origin::user,
    F("no arguments needed"));

  database db(app);

  set<revision_id> leaves;
  db.get_leaves(leaves);
  for (set<revision_id>::const_iterator i = leaves.begin();
       i != leaves.end(); ++i)
    output << *i << '\n';
}

// Name: roots
// Arguments:
//   None
// Added in: 4.3
// Purpose: Prints the roots of the revision graph, i.e. all revisions that
//   have no parents.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: None.
CMD_AUTOMATE(roots, "",
             N_("Lists the roots of the revision graph"),
             "",
             options::opts::none)
{
  E(args.size() == 0, origin::user,
    F("no arguments needed"));

  database db(app);

  // the real root revisions are the children of one single imaginary root
  // with an empty revision id
  set<revision_id> roots;
  revision_id nullid;
  db.get_revision_children(nullid, roots);
  for (set<revision_id>::const_iterator i = roots.begin();
       i != roots.end(); ++i)
      output << *i << '\n';
}

// Name: parents
// Arguments:
//   1: a revision id
// Added in: 0.2
// Purpose: Prints the immediate ancestors of the given revision, i.e., the
//   parents.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: If the revision does not exist, prints nothing to stdout,
//   prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(parents, N_("REV"),
             N_("Prints the parents of a revision"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  revision_id rid(decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user));
  E(db.revision_exists(rid), origin::user,
    F("no such revision '%s'") % rid);
  set<revision_id> parents;
  db.get_revision_parents(rid, parents);
  for (set<revision_id>::const_iterator i = parents.begin();
       i != parents.end(); ++i)
      if (!null_id(*i))
          output << *i << '\n';
}

// Name: children
// Arguments:
//   1: a revision id
// Added in: 0.2
// Purpose: Prints the immediate descendents of the given revision, i.e., the
//   children.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline.  Revision ids are printed in alphabetically sorted order.
// Error conditions: If the revision does not exist, prints nothing to stdout,
//   prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(children, N_("REV"),
             N_("Prints the children of a revision"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  revision_id rid(decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user));
  E(db.revision_exists(rid), origin::user,
    F("no such revision '%s'") % rid);
  set<revision_id> children;
  db.get_revision_children(rid, children);
  for (set<revision_id>::const_iterator i = children.begin();
       i != children.end(); ++i)
      if (!null_id(*i))
          output << *i << '\n';
}

// Name: graph
// Arguments:
//   None
// Added in: 0.2
// Purpose: Prints out the complete ancestry graph of this database.
// Output format:
//   Each line begins with a revision id.  Following this are zero or more
//   space-prefixed revision ids.  Each revision id after the first is a
//   parent (in the sense of 'automate parents') of the first.  For instance,
//   the following are valid lines:
//     07804171823d963f78d6a0ff1763d694dd74ff40
//     07804171823d963f78d6a0ff1763d694dd74ff40 79d755c197e54dd3db65751d3803833d4cbf0d01
//     07804171823d963f78d6a0ff1763d694dd74ff40 79d755c197e54dd3db65751d3803833d4cbf0d01 a02e7a1390e3e4745c31be922f03f56450c13dce
//   The first would indicate that 07804171823d963f78d6a0ff1763d694dd74ff40
//   was a root node; the second would indicate that it had one parent, and
//   the third would indicate that it had two parents, i.e., was a merge.
//
//   The output as a whole is alphabetically sorted; additionally, the parents
//   within each line are alphabetically sorted.
// Error conditions: None.
CMD_AUTOMATE(graph, "",
             N_("Prints the complete ancestry graph"),
             "",
             options::opts::none)
{
  E(args.size() == 0, origin::user,
    F("no arguments needed"));

  database db(app);

  multimap<revision_id, revision_id> edges_mmap;
  map<revision_id, set<revision_id> > child_to_parents;

  db.get_revision_ancestry(edges_mmap);

  for (multimap<revision_id, revision_id>::const_iterator i = edges_mmap.begin();
       i != edges_mmap.end(); ++i)
    {
      if (child_to_parents.find(i->second) == child_to_parents.end())
        child_to_parents.insert(make_pair(i->second, set<revision_id>()));
      if (null_id(i->first))
        continue;
      map<revision_id, set<revision_id> >::iterator
        j = child_to_parents.find(i->second);
      I(j->first == i->second);
      j->second.insert(i->first);
    }

  for (map<revision_id, set<revision_id> >::const_iterator
         i = child_to_parents.begin();
       i != child_to_parents.end(); ++i)
    {
      output << i->first;
      for (set<revision_id>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        output << ' ' << *j;
      output << '\n';
    }
}

// Name: select
// Arguments:
//   1: selector
// Added in: 0.2
// Purpose: Prints all the revisions that match the given selector.
// Output format: A list of revision ids, in hexadecimal, each followed by a
//   newline. Revision ids are printed in alphabetically sorted order.
// Error conditions: None.
CMD_AUTOMATE(select, N_("SELECTOR"),
             N_("Lists the revisions that match a selector"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  project_t project(db);
  set<revision_id> completions;
  expand_selector(app.opts, app.lua, project, idx(args, 0)(), completions);

  for (set<revision_id>::const_iterator i = completions.begin();
       i != completions.end(); ++i)
    output << *i << '\n';
}

struct node_info
{
  bool exists;
  // true if node_id is present in corresponding roster with the inventory map file_path
  // false if not present, or present with a different file_path
  // rest of data in this struct is invalid if false.
  node_id id;
  path::status type;
  file_id ident;
  attr_map_t attrs;

  node_info() : exists(false), id(the_null_node), type(path::nonexistent) {}
};

static void
get_node_info(node_t const & node, node_info & info)
{
  info.exists = true;
  info.id = node->self;
  info.attrs = node->attrs;
  if (is_file_t(node))
    {
      info.type = path::file;
      info.ident = downcast_to_file_t(node)->content;
    }
  else if (is_dir_t(node))
    info.type = path::directory;
  else
    I(false);
}

struct inventory_item
{
  // Records information about a pair of nodes with the same node_id in the
  // old roster and new roster, and the corresponding path in the
  // filesystem.
  node_info old_node;
  node_info new_node;
  file_path old_path;
  file_path new_path;

  path::status fs_type;
  file_id fs_ident;

  inventory_item() : fs_type(path::nonexistent) {}
};

typedef std::map<file_path, inventory_item> inventory_map;
// file_path will typically be an existing filesystem file, but in the case
// of a dropped or rename_source file it is only in the old roster, and in
// the case of a file added --bookkeep_only or rename_target
// --bookkeep_only, it is only in the new roster.

static void
inventory_rosters(roster_t const & old_roster,
                  roster_t const & new_roster,
                  node_restriction const & nmask,
                  path_restriction const & pmask,
                  inventory_map & inventory)
{
  std::map<int, file_path> old_paths;
  std::map<int, file_path> new_paths;

  node_map const & old_nodes = old_roster.all_nodes();
  for (node_map::const_iterator i = old_nodes.begin(); i != old_nodes.end(); ++i)
    {
      if (nmask.includes(old_roster, i->first))
        {
          file_path fp;
          old_roster.get_name(i->first, fp);
          if (pmask.includes(fp))
            {
              get_node_info(old_roster.get_node(i->first), inventory[fp].old_node);
              old_paths[inventory[fp].old_node.id] = fp;
            }
        }
    }

  node_map const & new_nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = new_nodes.begin(); i != new_nodes.end(); ++i)
    {
      if (nmask.includes(new_roster, i->first))
        {
          file_path fp;
          new_roster.get_name(i->first, fp);
          if (pmask.includes(fp))
            {
              get_node_info(new_roster.get_node(i->first), inventory[fp].new_node);
              new_paths[inventory[fp].new_node.id] = fp;
            }
        }
    }

  std::map<int, file_path>::iterator i;
  for (i = old_paths.begin(); i != old_paths.end(); ++i)
    {
      if (new_paths.find(i->first) == new_paths.end())
        {
          // There is no new node available; this is either a drop or a
          // rename to outside the current path restriction.

          if (new_roster.has_node(i->first))
            {
              // record rename to outside restriction
              new_roster.get_name(i->first, inventory[i->second].new_path);
              continue;
            }
          else
            // drop; no new path
            continue;
        }

      file_path old_path(i->second);
      file_path new_path(new_paths[i->first]);

      // both paths are identical, no rename
      if (old_path == new_path)
        continue;

      // record rename
      inventory[new_path].old_path = old_path;
      inventory[old_path].new_path = new_path;
    }

  // Now look for new_paths that are renames from outside the current
  // restriction, and thus are not in old_paths.
  // FIXME: only need this if restriction is not null
  for (i = new_paths.begin(); i != new_paths.end(); ++i)
    {
      if (old_paths.find(i->first) == old_paths.end())
        {
          // There is no old node available; this is either added or a
          // rename from outside the current path restriction.

          if (old_roster.has_node(i->first))
            {
              // record rename from outside restriction
              old_roster.get_name(i->first, inventory[i->second].old_path);
            }
          else
            // added; no old path
            continue;
        }
    }
}

// check if the include/exclude paths contains paths to renamed nodes
// if yes, add the corresponding old/new name of these nodes to the
// paths as well, so the tree walker code will correctly identify them later
// on or skips them if they should be excluded
static void
inventory_determine_corresponding_paths(roster_t const & old_roster,
                                        roster_t const & new_roster,
                                        vector<file_path> const & includes,
                                        vector<file_path> const & excludes,
                                        vector<file_path> & additional_includes,
                                        vector<file_path> & additional_excludes)
{
  // at first check the includes vector
  for (int i=0, s=includes.size(); i<s; i++)
    {
      file_path fp = includes.at(i);

      if (old_roster.has_node(fp))
        {
          node_t node = old_roster.get_node(fp);
          if (new_roster.has_node(node->self))
            {
              file_path new_path;
              new_roster.get_name(node->self, new_path);
              if (fp != new_path &&
                  find(includes.begin(), includes.end(), new_path) == includes.end())
                {
                  additional_includes.push_back(new_path);
                }
            }
        }

      if (new_roster.has_node(fp))
        {
          node_t node = new_roster.get_node(fp);
          if (old_roster.has_node(node->self))
            {
              file_path old_path;
              old_roster.get_name(node->self, old_path);
              if (fp != old_path &&
                  find(includes.begin(), includes.end(), old_path) == includes.end())
                {
                  additional_includes.push_back(old_path);
                }
            }
        }
    }

  // and now the excludes vector
  vector<file_path> new_excludes;
  for (int i=0, s=excludes.size(); i<s; i++)
    {
      file_path fp = excludes.at(i);

      if (old_roster.has_node(fp))
        {
          node_t node = old_roster.get_node(fp);
          if (new_roster.has_node(node->self))
            {
              file_path new_path;
              new_roster.get_name(node->self, new_path);
              if (fp != new_path &&
                  find(excludes.begin(), excludes.end(), new_path) == excludes.end())
                {
                  additional_excludes.push_back(new_path);
                }
            }
        }

      if (new_roster.has_node(fp))
        {
          node_t node = new_roster.get_node(fp);
          if (old_roster.has_node(node->self))
            {
              file_path old_path;
              old_roster.get_name(node->self, old_path);
              if (fp != old_path &&
                  find(excludes.begin(), excludes.end(), old_path) == excludes.end())
                {
                  additional_excludes.push_back(old_path);
                }
            }
        }
    }
}

struct inventory_itemizer : public tree_walker
{
  path_restriction const & mask;
  inventory_map & inventory;
  inodeprint_map ipm;
  workspace & work;

  inventory_itemizer(workspace & work,
                     path_restriction const & m,
                     inventory_map & i)
    : mask(m), inventory(i), work(work)
  {
    if (work.in_inodeprints_mode())
      {
        data dat;
        work.read_inodeprints(dat);
        read_inodeprint_map(dat, ipm);
      }
  }
  virtual bool visit_dir(file_path const & path);
  virtual void visit_file(file_path const & path);
};

bool
inventory_itemizer::visit_dir(file_path const & path)
{
  if(mask.includes(path))
    {
      inventory[path].fs_type = path::directory;
    }
  // don't recurse into ignored subdirectories
  return !work.ignore_file(path);
}

void
inventory_itemizer::visit_file(file_path const & path)
{
  if (mask.includes(path))
    {
      inventory_item & item = inventory[path];

      item.fs_type = path::file;

      if (item.new_node.exists)
        {
          if (inodeprint_unchanged(ipm, path))
            item.fs_ident = item.old_node.ident;
          else
            ident_existing_file(path, item.fs_ident);
        }
    }
}

static void
inventory_filesystem(workspace & work,
                     path_restriction const & mask,
                     inventory_map & inventory)
{
  inventory_itemizer itemizer(work, mask, inventory);
  file_path const root;
  // The constructor file_path() returns ""; the root directory. walk_tree
  // does not visit that node, so set fs_type now, if it meets the
  // restriction.
  if (mask.includes(root))
    {
      inventory[root].fs_type = path::directory;
    }
  walk_tree(root, itemizer);
}

namespace
{
  namespace syms
  {
    symbol const path("path");
    symbol const old_type("old_type");
    symbol const new_type("new_type");
    symbol const fs_type("fs_type");
    symbol const old_path("old_path");
    symbol const new_path("new_path");
    symbol const status("status");
    symbol const birth("birth");
    symbol const changes("changes");
  }
}

static void
inventory_determine_states(workspace & work, file_path const & fs_path,
                           inventory_item const & item, roster_t const & old_roster,
                           roster_t const & new_roster, vector<string> & states)
{
  // if both nodes exist, the only interesting case is
  // when the node ids aren't equal (so we have different nodes
  // with one and the same path in the old and the new roster)
  if (item.old_node.exists &&
      item.new_node.exists &&
      item.old_node.id != item.new_node.id)
    {
        if (new_roster.has_node(item.old_node.id))
          states.push_back("rename_source");
        else
          states.push_back("dropped");

        if (old_roster.has_node(item.new_node.id))
          states.push_back("rename_target");
        else
          states.push_back("added");
    }
  // this can be either a drop or a renamed item
  else if (item.old_node.exists &&
          !item.new_node.exists)
    {
      if (new_roster.has_node(item.old_node.id))
        states.push_back("rename_source");
      else
        states.push_back("dropped");
    }
  // this can be either an add or a renamed item
  else if (!item.old_node.exists &&
            item.new_node.exists)
    {
      if (old_roster.has_node(item.new_node.id))
        states.push_back("rename_target");
      else
        states.push_back("added");
    }

  // check the state of the file system item
  if (item.fs_type == path::nonexistent)
    {
      if (item.new_node.exists)
        {
          states.push_back("missing");

          // If this node is in a directory that is ignored in .mtn-ignore,
          // we will output this warning. Note that we don't detect a known
          // file that is ignored but not in an ignored directory.
          if (work.ignore_file(fs_path))
            W(F("'%s' is both known and ignored; "
              "it will be shown as 'missing'. Check .mtn-ignore.")
            % fs_path);
        }
    }
  else // exists on filesystem
    {
      if (!item.new_node.exists)
        {
          if (work.ignore_file(fs_path))
            {
              states.push_back("ignored");
            }
          else
            {
              states.push_back("unknown");
            }
        }
      else if (item.new_node.type != item.fs_type)
        {
          states.push_back("invalid");
        }
      else
        {
          states.push_back("known");
        }
    }
}

static void
inventory_determine_changes(inventory_item const & item, roster_t const & old_roster,
                            vector<string> & changes)
{
  // old nodes do not have any recorded content changes and attributes,
  // so we can't print anything for them here
  if (!item.new_node.exists)
    return;

  // this is an existing item
  if (old_roster.has_node(item.new_node.id))
    {
      // check if the content has changed - this makes only sense for files
      // for which we can get the content id of both new and old nodes.
      if (item.new_node.type == path::file && item.fs_type != path::nonexistent)
        {
          file_t old_file = downcast_to_file_t(old_roster.get_node(item.new_node.id));

          switch (item.old_node.type)
            {
            case path::file:
            case path::nonexistent:
              // A file can be nonexistent due to mtn drop, user delete, mtn
              // rename, or user rename. If it was drop or delete, it would
              // not be in the new roster, and we would not get here. So
              // it's a rename, and we can get the content. This lets us
              // check if a user has edited a file after renaming it.
              if (item.fs_ident != old_file->content)
                changes.push_back("content");
              break;

            case path::directory:
              break;
            }
        }

      // now look for changed attributes
      node_t old_node = old_roster.get_node(item.new_node.id);
      if (old_node->attrs != item.new_node.attrs)
        changes.push_back("attrs");
    }
  else
    {
      // FIXME: paranoia: shall we I(new_roster.has_node(item.new_node.id)) here?

      // this is apparently a new item, if it is a file it gets at least
      // the "content" marker and we also check for recorded attributes
      if (item.new_node.type == path::file)
        changes.push_back("content");

      if (!item.new_node.attrs.empty())
        changes.push_back("attrs");
    }
}

static revision_id
inventory_determine_birth(inventory_item const & item,
                          roster_t const & old_roster,
                          marking_map const & old_marking)
{
  revision_id rid;
  if (old_roster.has_node(item.new_node.id))
    {
      node_t node = old_roster.get_node(item.new_node.id);
      marking_map::const_iterator m = old_marking.find(node->self);
      I(m != old_marking.end());
      marking_t mark = m->second;
      rid = mark.birth_revision;
    }
  return rid;
}

// Name: inventory
// Arguments: [PATH]...
// Added in: 1.0
// Modified to basic_io in: 4.1

// Purpose: Prints a summary of every file or directory found in the
//   workspace or its associated base manifest.

// See monotone.texi for output format description.
//
// Error conditions: If no workspace book keeping _MTN directory is found,
//   prints an error message to stderr, and exits with status 1.

CMD_AUTOMATE(inventory,  N_("[PATH]..."),
             N_("Prints a summary of files found in the workspace"),
             "",
             options::opts::depth |
             options::opts::exclude |
             options::opts::no_ignored |
             options::opts::no_unknown |
             options::opts::no_unchanged |
             options::opts::no_corresponding_renames)
{
  database db(app);
  workspace work(app);

  parent_map parents;
  work.get_parent_rosters(db, parents);
  // for now, until we've figured out what the format could look like
  // and what conceptional model we can implement
  // see: http://monotone.ca/wiki/MultiParentWorkspaceFallout/
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));

  roster_t new_roster, old_roster = parent_roster(parents.begin());
  marking_map old_marking = parent_marking(parents.begin());
  temp_node_id_source nis;

  work.get_current_roster_shape(db, nis, new_roster);

  inventory_map inventory;
  vector<file_path> includes = args_to_paths(args);
  vector<file_path> excludes = args_to_paths(app.opts.exclude_patterns);

  if (!app.opts.no_corresponding_renames)
    {
      vector<file_path> add_includes, add_excludes;
      inventory_determine_corresponding_paths(old_roster, new_roster,
                                              includes, excludes,
                                              add_includes, add_excludes);

      copy(add_includes.begin(), add_includes.end(),
           inserter(includes, includes.end()));

      copy(add_excludes.begin(), add_excludes.end(),
           inserter(excludes, excludes.end()));
    }

  node_restriction nmask(includes, excludes, app.opts.depth,
                         old_roster, new_roster, ignored_file(work));
  // skip the check of the workspace paths because some of them might
  // be missing and the user might want to query the recorded structure
  // of them anyways
  path_restriction pmask(includes, excludes, app.opts.depth,
                         path_restriction::skip_check);

  inventory_rosters(old_roster, new_roster, nmask, pmask, inventory);
  inventory_filesystem(work, pmask, inventory);

  basic_io::printer pr;

  for (inventory_map::const_iterator i = inventory.begin(); i != inventory.end();
       ++i)
    {
      file_path const & fp = i->first;
      inventory_item const & item = i->second;

      //
      // check if we should output this element at all
      //
      vector<string> states;
      inventory_determine_states(work, fp, item,
                                 old_roster, new_roster, states);

      if (find(states.begin(), states.end(), "ignored") != states.end() &&
          app.opts.no_ignored)
        continue;

      if (find(states.begin(), states.end(), "unknown") != states.end() &&
          app.opts.no_unknown)
        continue;

      vector<string> changes;
      inventory_determine_changes(item, old_roster, changes);

      revision_id birth_revision =
        inventory_determine_birth(item, old_roster, old_marking);

      bool is_tracked =
        find(states.begin(), states.end(), "unknown") == states.end() &&
        find(states.begin(), states.end(), "ignored") == states.end();

      bool has_changed =
        find(states.begin(), states.end(), "rename_source") != states.end() ||
        find(states.begin(), states.end(), "rename_target") != states.end() ||
        find(states.begin(), states.end(), "added")         != states.end() ||
        find(states.begin(), states.end(), "dropped")       != states.end() ||
        find(states.begin(), states.end(), "missing")       != states.end() ||
        !changes.empty();

      if (is_tracked && !has_changed && app.opts.no_unchanged)
        continue;

      //
      // begin building the output stanza
      //
      basic_io::stanza st;
      st.push_file_pair(syms::path, fp);

      if (item.old_node.exists)
        {
          switch (item.old_node.type)
            {
            case path::file: st.push_str_pair(syms::old_type, "file"); break;
            case path::directory: st.push_str_pair(syms::old_type, "directory"); break;
            case path::nonexistent: I(false);
            }

          if (item.new_path.as_internal().length() > 0)
            {
              st.push_file_pair(syms::new_path, item.new_path);
            }
        }

      if (item.new_node.exists)
        {
          switch (item.new_node.type)
            {
            case path::file: st.push_str_pair(syms::new_type, "file"); break;
            case path::directory: st.push_str_pair(syms::new_type, "directory"); break;
            case path::nonexistent: I(false);
            }

          if (item.old_path.as_internal().length() > 0)
            {
              st.push_file_pair(syms::old_path, item.old_path);
            }
        }

      switch (item.fs_type)
        {
        case path::file: st.push_str_pair(syms::fs_type, "file"); break;
        case path::directory: st.push_str_pair(syms::fs_type, "directory"); break;
        case path::nonexistent: st.push_str_pair(syms::fs_type, "none"); break;
        }

      //
      // finally output the previously recorded states and changes
      //
      if (!birth_revision.inner()().empty())
        st.push_binary_pair(syms::birth, birth_revision.inner());

      I(!states.empty());
      st.push_str_multi(syms::status, states);

      if (!changes.empty())
        st.push_str_multi(syms::changes, changes);

      pr.print_stanza(st);
    }

  output.write(pr.buf.data(), pr.buf.size());
}

// Name: get_revision
// Arguments:
//   1: a revision id
// Added in: 1.0
// Changed in: 7.0 (REVID argument is now mandatory)

// Purpose: Prints change information for the specified revision id.
//   There are several changes that are described; each of these is
//   described by a different basic_io stanza. The first string pair
//   of each stanza indicates the type of change represented.
//
//   All stanzas are formatted by basic_io. Stanzas are separated
//   by a blank line. Values will be escaped, '\' to '\\' and
//   '"' to '\"'.
//
//   Possible values of this first value are along with an ordered list of
//   basic_io formatted stanzas that will be provided are:
//
//   'format_version'
//         used in case this format ever needs to change.
//         format: ('format_version', the string "1")
//         occurs: exactly once
//   'new_manifest'
//         represents the new manifest associated with the revision.
//         format: ('new_manifest', manifest id)
//         occurs: exactly one
//   'old_revision'
//         represents a parent revision.
//         format: ('old_revision', revision id)
//         occurs: either one or two times
//   'delete
//         represents a file or directory that was deleted.
//         format: ('delete', path)
//         occurs: zero or more times
//   'rename'
//         represents a file or directory that was renamed.
//         format: ('rename, old filename), ('to', new filename)
//         occurs: zero or more times
//   'add_dir'
//         represents a directory that was added.
//         format: ('add_dir, path)
//         occurs: zero or more times
//   'add_file'
//         represents a file that was added.
//         format: ('add_file', path), ('content', file id)
//         occurs: zero or more times
//   'patch'
//         represents a file that was modified.
//         format: ('patch', filename), ('from', file id), ('to', file id)
//         occurs: zero or more times
//   'clear'
//         represents an attr that was removed.
//         format: ('clear', filename), ('attr', attr name)
//         occurs: zero or more times
//   'set'
//         represents an attr whose value was changed.
//         format: ('set', filename), ('attr', attr name), ('value', attr value)
//         occurs: zero or more times
//
//   These stanzas will always occur in the order listed here; stanzas of
//   the same type will be sorted by the filename they refer to.
// Error conditions: If the revision specified is unknown or invalid
// prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(get_revision, N_("REVID"),
             N_("Shows change information for a revision"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  revision_data dat;
  revision_id rid(decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user));
  E(db.revision_exists(rid), origin::user,
    F("no revision %s found in database") % rid);
  db.get_revision(rid, dat);

  L(FL("dumping revision %s") % rid);
  output << dat;
}

// Name: get_current_revision
// Arguments:
//   1: zero or more path names
// Added in: 7.0
// Purpose: Outputs (an optionally restricted) revision based on
//          changes in the current workspace
// Error conditions: If the restriction is invalid, prints an error message
// to stderr and exits with status 1. A workspace is required.
CMD_AUTOMATE(get_current_revision, N_("[PATHS ...]"),
             N_("Shows change information for a workspace"),
             "",
             options::opts::exclude | options::opts::depth)
{
  temp_node_id_source nis;
  revision_data dat;
  revision_id ident;

  roster_t new_roster;
  parent_map old_rosters;
  revision_t rev;
  cset excluded;

  database db(app);
  workspace work(app);
  work.get_parent_rosters(db, old_rosters);
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_rosters, new_roster);

  work.update_current_roster_from_filesystem(new_roster, mask);

  make_revision(old_rosters, new_roster, rev);
  make_restricted_revision(old_rosters, new_roster, mask, rev,
                           excluded, join_words(execid));
  rev.check_sane();

  calculate_ident(rev, ident);
  write_revision(rev, dat);

  L(FL("dumping revision %s") % ident);
  output << dat;
}


// Name: get_base_revision_id
// Arguments: none
// Added in: 2.0
// Purpose: Prints the revision id the current workspace is based
//   on. This is the value stored in _MTN/revision
// Error conditions: If no workspace book keeping _MTN directory is found,
//   prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(get_base_revision_id, "",
             N_("Shows the revision on which the workspace is based"),
             "",
             options::opts::none)
{
  E(args.size() == 0, origin::user,
    F("no arguments needed"));

  database db(app);
  workspace work(app);

  parent_map parents;
  work.get_parent_rosters(db, parents);
  E(parents.size() == 1, origin::user,
    F("this command can only be used in a single-parent workspace"));

  output << parent_id(parents.begin()) << '\n';
}

// Name: get_current_revision_id
// Arguments: none
// Added in: 2.0
// Purpose: Prints the revision id of the current workspace. This is the
//   id of the revision that would be committed by an unrestricted
//   commit calculated from _MTN/revision, _MTN/work and any edits to
//   files in the workspace.
// Error conditions: If no workspace book keeping _MTN directory is found,
//   prints an error message to stderr, and exits with status 1.
CMD_AUTOMATE(get_current_revision_id, "",
             N_("Shows the revision of the current workspace"),
             "",
             options::opts::none)
{
  E(args.size() == 0, origin::user,
    F("no arguments needed"));

  workspace work(app);
  database db(app);

  parent_map parents;
  roster_t new_roster;
  revision_id new_revision_id;
  revision_t rev;
  temp_node_id_source nis;

  work.get_current_roster_shape(db, nis, new_roster);
  work.update_current_roster_from_filesystem(new_roster);

  work.get_parent_rosters(db, parents);
  make_revision(parents, new_roster, rev);

  calculate_ident(rev, new_revision_id);

  output << new_revision_id << '\n';
}

// Name: get_manifest_of
// Arguments:
//   1: a revision id (optional, determined from the workspace if not given)
// Added in: 2.0
// Purpose: Prints the contents of the manifest associated with the
//   given revision ID.
//
// Output format:
//   There is one basic_io stanza for each file or directory in the
//   manifest.
//
//   All stanzas are formatted by basic_io. Stanzas are separated
//   by a blank line. Values will be escaped, '\' to '\\' and
//   '"' to '\"'.
//
//   Possible values of this first value are along with an ordered list of
//   basic_io formatted stanzas that will be provided are:
//
//   'format_version'
//         used in case this format ever needs to change.
//         format: ('format_version', the string "1")
//         occurs: exactly once
//   'dir':
//         represents a directory.  The path "" (the empty string) is used
//         to represent the root of the tree.
//         format: ('dir', pathname)
//         occurs: one or more times
//   'file':
//         represents a file.
//         format: ('file', pathname), ('content', file id)
//         occurs: zero or more times
//
//   In addition, 'dir' and 'file' stanzas may have attr information
//   included.  These are appended to the stanza below the basic
//   dir/file information, with one line describing each attr.  These
//   lines take the form ('attr', attr name, attr value).
//
//   Stanzas are sorted by the path string.
//
// Error conditions: If the revision ID specified is unknown or
// invalid prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(get_manifest_of, N_("[REVID]"),
             N_("Shows the manifest associated with a revision"),
             "",
             options::opts::none)
{
  database db(app);

  E(args.size() < 2, origin::user,
    F("wrong argument count"));

  manifest_data dat;
  manifest_id mid;
  roster_t new_roster;

  if (args.size() == 0)
    {
      workspace work(app);

      temp_node_id_source nis;

      work.get_current_roster_shape(db, nis, new_roster);
      work.update_current_roster_from_filesystem(new_roster);
    }
  else
    {
      revision_id rid = decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user);
      E(db.revision_exists(rid), origin::user,
        F("no revision %s found in database") % rid);
      db.get_roster(rid, new_roster);
    }

  calculate_ident(new_roster, mid);
  write_manifest_of_roster(new_roster, dat);
  L(FL("dumping manifest %s") % mid);
  output << dat;
}


// Name: packet_for_rdata
// Arguments:
//   1: a revision id
// Added in: 2.0
// Purpose: Prints the revision data in packet format
//
// Output format: revision data in "monotone read" compatible packet
//   format
//
// Error conditions: If the revision id specified is unknown or
// invalid prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(packet_for_rdata, N_("REVID"),
             N_("Prints the revision data in packet format"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  packet_writer pw(output);

  revision_id r_id(decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user));
  revision_data r_data;

  E(db.revision_exists(r_id), origin::user,
    F("no such revision '%s'") % r_id);
  db.get_revision(r_id, r_data);
  pw.consume_revision_data(r_id, r_data);
}

// Name: packets_for_certs
// Arguments:
//   1: a revision id
// Added in: 2.0
// Purpose: Prints the certs associated with a revision in packet format
//
// Output format: certs in "monotone read" compatible packet format
//
// Error conditions: If the revision id specified is unknown or
// invalid prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(packets_for_certs, N_("REVID"),
             N_("Prints the certs associated with a revision in "
                "packet format"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);
  project_t project(db);
  packet_writer pw(output);

  revision_id r_id(decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user));
  vector<cert> certs;

  E(db.revision_exists(r_id), origin::user,
    F("no such revision '%s'") % r_id);
  project.get_revision_certs(r_id, certs);

  for (vector<cert>::const_iterator i = certs.begin();
       i != certs.end(); i++)
    pw.consume_revision_cert(*i);
}

// Name: packet_for_fdata
// Arguments:
//   1: a file id
// Added in: 2.0
// Purpose: Prints the file data in packet format
//
// Output format: file data in "monotone read" compatible packet format
//
// Error conditions: If the file id specified is unknown or invalid
// prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(packet_for_fdata, N_("FILEID"),
             N_("Prints the file data in packet format"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  packet_writer pw(output);

  file_id f_id(decode_hexenc_as<file_id>(idx(args, 0)(), origin::user));
  file_data f_data;

  E(db.file_version_exists(f_id), origin::user,
    F("no such file '%s'") % f_id);
  db.get_file_version(f_id, f_data);
  pw.consume_file_data(f_id, f_data);
}

// Name: packet_for_fdelta
// Arguments:
//   1: a file id
//   2: a file id
// Added in: 2.0
// Purpose: Prints the file delta in packet format
//
// Output format: file delta in "monotone read" compatible packet format
//
// Error conditions: If any of the file ids specified are unknown or
// invalid prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(packet_for_fdelta, N_("OLD_FILE NEW_FILE"),
             N_("Prints the file delta in packet format"),
             "",
             options::opts::none)
{
  E(args.size() == 2, origin::user,
    F("wrong argument count"));

  database db(app);

  packet_writer pw(output);

  file_id f_old_id(decode_hexenc_as<file_id>(idx(args, 0)(), origin::user));
  file_id f_new_id(decode_hexenc_as<file_id>(idx(args, 1)(), origin::user));
  file_data f_old_data, f_new_data;

  E(db.file_version_exists(f_old_id), origin::user,
    F("no such revision '%s'") % f_old_id);
  E(db.file_version_exists(f_new_id), origin::user,
    F("no such revision '%s'") % f_new_id);
  db.get_file_version(f_old_id, f_old_data);
  db.get_file_version(f_new_id, f_new_data);
  delta del;
  diff(f_old_data.inner(), f_new_data.inner(), del);
  pw.consume_file_delta(f_old_id, f_new_id, file_delta(del));
}

// Name: common_ancestors
// Arguments:
//   1 or more revision ids
// Added in: 2.1
// Purpose: Prints all revisions which are ancestors of all of the
//   revisions given as arguments.
// Output format: A list of revision ids, in hexadecimal, each
//   followed by a newline.  Revisions are printed in alphabetically
//   sorted order.
// Error conditions: If any of the revisions do not exist, prints
//   nothing to stdout, prints an error message to stderr, and exits
//   with status 1.
CMD_AUTOMATE(common_ancestors, N_("REV1 [REV2 [REV3 [...]]]"),
             N_("Prints revisions that are common ancestors of a list "
                "of revisions"),
             "",
             options::opts::none)
{
  E(args.size() > 0, origin::user,
    F("wrong argument count"));

  database db(app);

  set<revision_id> revs, common_ancestors;
  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    {
      revision_id rid(decode_hexenc_as<revision_id>((*i)(), origin::user));
      E(db.revision_exists(rid), origin::user,
        F("No such revision %s") % rid);
      revs.insert(rid);
    }

  db.get_common_ancestors(revs, common_ancestors);

  for (set<revision_id>::const_iterator i = common_ancestors.begin();
       i != common_ancestors.end(); ++i)
      output << *i << "\n";
}

// Name: branches
// Arguments:
//   None
// Added in: 2.2
// Purpose:
//   Prints all branch certs present in the revision graph, that are not
//   excluded by the lua hook 'ignore_branch'.
// Output format:
//   Zero or more lines, each the name of a branch. The lines are printed
//   in alphabetically sorted order.
// Error conditions:
//   None.
CMD_AUTOMATE(branches, "",
             N_("Prints all branch certs in the revision graph"),
             "",
             options::opts::none)
{
  E(args.size() == 0, origin::user,
    F("no arguments needed"));

  database db(app);
  project_t project(db);
  set<branch_name> names;

  project.get_branch_list(names, !app.opts.ignore_suspend_certs);

  for (set<branch_name>::const_iterator i = names.begin();
       i != names.end(); ++i)
    if (!app.lua.hook_ignore_branch(*i))
      output << (*i) << '\n';
}

// Name: tags
// Arguments:
//   A branch pattern (optional).
// Added in: 2.2
// Purpose:
//   If a branch pattern is given, prints all tags that are attached to
//   revisions on branches matched by the pattern; otherwise prints all tags
//   of the revision graph.
//
//   If a branch name is ignored by means of the lua hook 'ignore_branch',
//   it is neither printed, nor can it be matched by a pattern.
// Output format:
//   There is one basic_io stanza for each tag.
//
//   All stanzas are formatted by basic_io. Stanzas are separated
//   by a blank line. Values will be escaped, '\' to '\\' and
//   '"' to '\"'.
//
//   Each stanza has exactly the following four entries:
//
//   'tag'
//         the value of the tag cert, i.e. the name of the tag
//   'revision'
//         the hexadecimal id of the revision the tag is attached to
//   'signer'
//         the name of the key used to sign the tag cert
//   'branches'
//         a (possibly empty) list of all branches the tagged revision is on
//
//   Stanzas are printed in arbitrary order.
// Error conditions:
//   A run-time exception is thrown for illegal patterns.
CMD_AUTOMATE(tags, N_("[BRANCH_PATTERN]"),
             N_("Prints all tags attached to a set of branches"),
             "",
             options::opts::none)
{
  E(args.size() < 2, origin::user,
    F("wrong argument count"));

  database db(app);
  project_t project(db);
  globish incl("*", origin::internal);
  bool filtering(false);

  if (args.size() == 1) {
    incl = globish(idx(args, 0)(), origin::user);
    filtering = true;
  }

  basic_io::printer prt;
  basic_io::stanza stz;
  stz.push_str_pair(symbol("format_version"), "1");
  prt.print_stanza(stz);

  set<tag_t> tags;
  project.get_tags(tags);

  for (set<tag_t>::const_iterator tag = tags.begin();
       tag != tags.end(); ++tag)
    {
      set<branch_name> branches;
      project.get_revision_branches(tag->ident, branches);

      bool show(!filtering);
      vector<string> branch_names;

      for (set<branch_name>::const_iterator branch = branches.begin();
           branch != branches.end(); ++branch)
        {
          // FIXME: again, hook_ignore_branch should probably be in the
          //        database context...
          if (app.lua.hook_ignore_branch(*branch))
            continue;

          if (!show && incl.matches((*branch)()))
            show = true;
          branch_names.push_back((*branch)());
        }

      if (show)
        {
          basic_io::stanza stz;
          stz.push_str_pair(symbol("tag"), tag->name());
          stz.push_binary_pair(symbol("revision"), tag->ident.inner());
          stz.push_binary_pair(symbol("signer"), tag->key.inner());
          stz.push_str_multi(symbol("branches"), branch_names);
          prt.print_stanza(stz);
        }
    }
  output.write(prt.buf.data(), prt.buf.size());
}

namespace
{
  namespace syms
  {
    symbol const key("key");
    symbol const signature("signature");
    symbol const name("name");
    symbol const value("value");
    symbol const trust("trust");

    symbol const hash("hash");
    symbol const public_location("public_location");
    symbol const private_location("private_location");

    symbol const domain("domain");
    symbol const entry("entry");
  }
};

// Name: genkey
// Arguments:
//   1: the key ID
//   2: the key passphrase
// Added in: 3.1
// Changed in: 10.0
// Purpose: Generates a key with the given ID and passphrase
//
// Output format: a basic_io stanza for the new key, as for ls keys
//
// Sample output:
//               name "tbrownaw@gmail.com"
//               hash [475055ec71ad48f5dfaf875b0fea597b5cbbee64]
//    public_location "database" "keystore"
//   private_location "keystore"
//
// Error conditions: If the passphrase is empty or the key already exists,
// prints an error message to stderr and exits with status 1.
CMD_AUTOMATE(genkey, N_("KEYID PASSPHRASE"),
             N_("Generates a key"),
             "",
             options::opts::none)
{
  E(args.size() == 2, origin::user,
    F("wrong argument count"));

  database db(app);
  key_store keys(app);

  key_name name;
  internalize_key_name(idx(args, 0), name);

  E(!keys.key_pair_exists(name), origin::user,
    F("you already have a key named '%s'") % name);
  if (db.database_specified())
    {
      E(!db.public_key_exists(name), origin::user,
        F("there is another key named '%s'") % name);
    }

  utf8 passphrase = idx(args, 1);

  key_id hash;
  keys.create_key_pair(db, name, &passphrase, &hash);

  basic_io::printer prt;
  basic_io::stanza stz;
  vector<string> publocs, privlocs;
  if (db.database_specified())
    publocs.push_back("database");
  publocs.push_back("keystore");
  privlocs.push_back("keystore");

  stz.push_str_pair(syms::name, name());
  stz.push_binary_pair(syms::hash, hash.inner());
  stz.push_str_multi(syms::public_location, publocs);
  stz.push_str_multi(syms::private_location, privlocs);
  prt.print_stanza(stz);

  output.write(prt.buf.data(), prt.buf.size());

}

// Name: get_option
// Arguments:
//   1: an options name
// Added in: 3.1
// Purpose: Show the value of the named option in _MTN/options
//
// Output format: A string
//
// Sample output (for 'mtn automate get_option branch:
//   net.venge.monotone
//
CMD_AUTOMATE(get_option, N_("OPTION"),
             N_("Shows the value of an option"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  workspace work(app);
  work.print_option(args[0], output);
}

// Name: get_content_changed
// Arguments:
//   1: a revision ID
//   2: a file name
// Added in: 3.1
// Purpose: Returns a list of revision IDs in which the content
// was most recently changed, relative to the revision ID specified
// in argument 1. This equates to a content mark following
// the *-merge algorithm.
//
// Output format: Zero or more basic_io stanzas, each specifying a
// revision ID for which a content mark is set.
//
//   Each stanza has exactly one entry:
//
//   'content_mark'
//         the hexadecimal id of the revision the content mark is attached to
// Sample output (for 'mtn automate get_content_changed 3bccff99d08421df72519b61a4dded16d1139c33 ChangeLog):
//   content_mark [276264b0b3f1e70fc1835a700e6e61bdbe4c3f2f]
//
CMD_AUTOMATE(get_content_changed, N_("REV FILE"),
             N_("Lists the revisions that changed the content relative "
                "to another revision"),
             "",
             options::opts::none)
{
  E(args.size() == 2, origin::user,
    F("wrong argument count"));

  database db(app);

  roster_t new_roster;
  revision_id ident;
  marking_map mm;

  ident = decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user);
  E(db.revision_exists(ident), origin::user,
    F("no revision %s found in database") % ident);
  db.get_roster(ident, new_roster, mm);

  file_path path = file_path_external(idx(args,1));
  E(new_roster.has_node(path), origin::user,
    F("file %s is unknown for revision %s")
    % path % ident);

  node_t node = new_roster.get_node(path);
  marking_map::const_iterator m = mm.find(node->self);
  I(m != mm.end());
  marking_t mark = m->second;

  basic_io::printer prt;
  for (set<revision_id>::const_iterator i = mark.file_content.begin();
       i != mark.file_content.end(); ++i)
    {
      basic_io::stanza st;
      st.push_binary_pair(basic_io::syms::content_mark, i->inner());
      prt.print_stanza(st);
    }
    output.write(prt.buf.data(), prt.buf.size());
}

// Name: get_corresponding_path
// Arguments:
//   1: a source revision ID
//   2: a file name (in the source revision)
//   3: a target revision ID
// Added in: 3.1
// Purpose: Given a the file name in the source revision, a filename
// will if possible be returned naming the file in the target revision.
// This allows the same file to be matched between revisions, accounting
// for renames and other changes.
//
// Output format: Zero or one basic_io stanzas. Zero stanzas will be
// output if the file does not exist within the target revision; this is
// not considered an error.
// If the file does exist in the target revision, a single stanza with the
// following details is output.
//
//   The stanza has exactly one entry:
//
//   'file'
//         the file name corresponding to "file name" (arg 2) in the target revision
//
// Sample output (for automate get_corresponding_path 91f25c8ee830b11b52dd356c925161848d4274d0 foo2 dae0d8e3f944c82a9688bcd6af99f5b837b41968; see automate_get_corresponding_path test)
// file "foo"
CMD_AUTOMATE(get_corresponding_path, N_("REV1 FILE REV2"),
             N_("Prints the name of a file in a target revision relative "
                "to a given revision"),
             "",
             options::opts::none)
{
  E(args.size() == 3, origin::user,
    F("wrong argument count"));

  database db(app);

  roster_t new_roster, old_roster;
  revision_id ident, old_ident;

  ident = decode_hexenc_as<revision_id>(idx(args, 0)(), origin::user);
  E(db.revision_exists(ident), origin::user,
    F("no revision %s found in database") % ident);
  db.get_roster(ident, new_roster);

  old_ident = decode_hexenc_as<revision_id>(idx(args, 2)(), origin::user);
  E(db.revision_exists(old_ident), origin::user,
    F("no revision %s found in database") % old_ident);
  db.get_roster(old_ident, old_roster);

  file_path path = file_path_external(idx(args,1));
  E(new_roster.has_node(path), origin::user,
    F("file %s is unknown for revision %s") % path % ident);

  node_t node = new_roster.get_node(path);
  basic_io::printer prt;
  if (old_roster.has_node(node->self))
    {
      file_path old_path;
      basic_io::stanza st;
      old_roster.get_name(node->self, old_path);
      st.push_file_pair(basic_io::syms::file, old_path);
      prt.print_stanza(st);
    }
  output.write(prt.buf.data(), prt.buf.size());
}

// Name: put_file
// Arguments:
//   base FILEID (optional)
//   file contents (binary, intended for automate stdio use)
// Added in: 4.1
// Purpose:
//   Store a file in the database.
//   Optionally encode it as a file_delta
// Output format:
//   The ID of the new file (40 digit hex string)
// Error conditions:
//   a runtime exception is thrown if base revision is not available
CMD_AUTOMATE(put_file, N_("[FILEID] CONTENTS"),
             N_("Stores a file in the database"),
             "",
             options::opts::none)
{
  E(args.size() == 1 || args.size() == 2, origin::user,
    F("wrong argument count"));

  database db(app);

  file_id sha1sum;
  transaction_guard tr(db);
  if (args.size() == 1)
    {
      file_data dat = typecast_vocab<file_data>(idx(args, 0));
      calculate_ident(dat, sha1sum);

      db.put_file(sha1sum, dat);
    }
  else if (args.size() == 2)
    {
      file_data dat = typecast_vocab<file_data>(idx(args, 1));
      calculate_ident(dat, sha1sum);
      file_id base_id(decode_hexenc_as<file_id>(idx(args, 0)(), origin::user));
      E(db.file_version_exists(base_id), origin::user,
        F("no file version %s found in database") % base_id);

      // put_file_version won't do anything if the target ID already exists,
      // but we can save the delta calculation by checking here too
      if (!db.file_version_exists(sha1sum))
        {
          file_data olddat;
          db.get_file_version(base_id, olddat);
          delta del;
          diff(olddat.inner(), dat.inner(), del);

          db.put_file_version(base_id, sha1sum, file_delta(del));
        }
    }
  else I(false);

  tr.commit();
  output << sha1sum << '\n';
}

// Name: put_revision
// Arguments:
//   revision-data
// Added in: 4.1
// Purpose:
//   Store a revision into the database.
// Output format:
//   The ID of the new revision
// Error conditions:
//   none
CMD_AUTOMATE(put_revision, N_("REVISION-DATA"),
             N_("Stores a revision into the database"),
             "",
             options::opts::none)
{
  E(args.size() == 1, origin::user,
    F("wrong argument count"));

  database db(app);

  revision_t rev;
  read_revision(typecast_vocab<revision_data>(idx(args, 0)), rev);

  // recalculate manifest
  temp_node_id_source nis;
  rev.new_manifest = manifest_id();
  for (edge_map::const_iterator e = rev.edges.begin(); e != rev.edges.end(); ++e)
    {
      // calculate new manifest
      roster_t old_roster;
      if (!null_id(e->first)) db.get_roster(e->first, old_roster);
      roster_t new_roster = old_roster;
      editable_roster_base eros(new_roster, nis);
      e->second->apply_to(eros);
      if (null_id(rev.new_manifest))
        // first edge, initialize manifest
        calculate_ident(new_roster, rev.new_manifest);
      else
        // following edge, make sure that all csets end at the same manifest
        {
          manifest_id calculated;
          calculate_ident(new_roster, calculated);
          I(calculated == rev.new_manifest);
        }
    }

  revision_id id;
  calculate_ident(rev, id);

  // If the database refuses the revision, make sure this is because it's
  // already there.
  E(db.put_revision(id, rev) || db.revision_exists(id),
    origin::user,
    F("missing prerequisite for revision %s") % id);

  output << id << '\n';
}

// Name: cert
// Arguments:
//   revision ID
//   certificate name
//   certificate value
// Added in: 4.1
// Purpose:
//   Add a revision certificate (like mtn cert).
// Output format:
//   nothing
// Error conditions:
//   none
CMD_AUTOMATE(cert, N_("REVISION-ID NAME VALUE"),
             N_("Adds a revision certificate"),
             "",
             options::opts::none)
{
  E(args.size() == 3, origin::user,
    F("wrong argument count"));

  database db(app);
  key_store keys(app);
  project_t project(db);

  hexenc<id> hrid(idx(args, 0)(), origin::user);
  revision_id rid(decode_hexenc_as<revision_id>(hrid(), origin::user));
  E(db.revision_exists(rid), origin::user,
    F("no such revision '%s'") % hrid);

  cache_user_key(app.opts, app.lua, db, keys, project);

  project.put_cert(keys, rid,
                   typecast_vocab<cert_name>(idx(args, 1)),
                   typecast_vocab<cert_value>(idx(args, 2)));
}

// Name: get_db_variables
// Arguments:
//   variable domain
// Changes:
//  4.1 (added as 'db_get')
//  7.0 (changed to 'get_db_variables', output is now basic_io)
// Purpose:
//   Retrieves db variables, optionally filtered by DOMAIN
// Output format:
//   basic_io, see the mtn docs for details
// Error conditions:
//   none
CMD_AUTOMATE(get_db_variables, N_("[DOMAIN]"),
             N_("Retrieve database variables"),
             "",
             options::opts::none)
{
  E(args.size() < 2, origin::user,
    F("wrong argument count"));

  database db(app);
  bool filter_by_domain = false;
  var_domain filter;
  if (args.size() == 1)
    {
      filter_by_domain = true;
      filter = typecast_vocab<var_domain>(idx(args, 0));
    }

  map<var_key, var_value> vars;
  db.get_vars(vars);

  var_domain cur_domain;
  basic_io::stanza st;
  basic_io::printer pr;
  bool found_something = false;

  for (map<var_key, var_value>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      if (filter_by_domain && !(i->first.first == filter))
        continue;

      found_something = true;

      if (cur_domain != i->first.first)
        {
          // check if we need to print a previous stanza
          if (!st.entries.empty())
            {
              pr.print_stanza(st);
              st.entries.clear();
            }
          cur_domain = i->first.first;
          st.push_str_pair(syms::domain, cur_domain());
        }

      st.push_str_triple(syms::entry, i->first.second(), i->second());
    }

  E(found_something, origin::user,
    F("No variables found or invalid domain specified"));

  // print the last stanza
  pr.print_stanza(st);
  output.write(pr.buf.data(), pr.buf.size());
}

// Name: set_db_variable
// Arguments:
//   variable domain
//   variable name
//   veriable value
// Changes:
//   4.1 (added as 'db_set')
//   7.0 (renamed to 'set_db_variable')
// Purpose:
//   Set a database variable (like mtn database set)
// Output format:
//   nothing
// Error conditions:
//   none
CMD_AUTOMATE(set_db_variable, N_("DOMAIN NAME VALUE"),
             N_("Sets a database variable"),
             "",
             options::opts::none)
{
  E(args.size() == 3, origin::user,
    F("wrong argument count"));

  database db(app);

  var_domain domain = typecast_vocab<var_domain>(idx(args, 0));
  utf8 name = idx(args, 1);
  utf8 value = idx(args, 2);
  var_key key(domain, typecast_vocab<var_name>(name));
  db.set_var(key, typecast_vocab<var_value>(value));
}

// Name: drop_db_variables
// Arguments:
//   variable domain
//   variable name
// Changes:
//  7.0 (added)
// Purpose:
//   Drops a database variable (like mtn unset DOMAIN NAME) or all variables
//   within a domain
// Output format:
//   none
// Error conditions:
//   a runtime exception is thrown if the variable was not found
CMD_AUTOMATE(drop_db_variables, N_("DOMAIN [NAME]"),
             N_("Drops a database variable"),
             "",
             options::opts::none)
{
  E(args.size() == 1 || args.size() == 2, origin::user,
    F("wrong argument count"));

  database db(app);

  var_domain domain = typecast_vocab<var_domain>(idx(args, 0));

  if (args.size() == 2)
    {
      var_name name = typecast_vocab<var_name>(idx(args, 1));
      var_key  key(domain, name);
      E(db.var_exists(key), origin::user,
        F("no var with name %s in domain %s") % name % domain);
      db.clear_var(key);
    }
  else
    {
      map<var_key, var_value> vars;
      db.get_vars(vars);
      bool found_something = false;

      for (map<var_key, var_value>::const_iterator i = vars.begin();
           i != vars.end(); ++i)
        {
          if (i->first.first == domain)
            {
              found_something = true;
              db.clear_var(i->first);
            }
        }

      E(found_something, origin::user,
        F("no variables found in domain %s") % domain);
    }
}

// Name: drop_db_variables
// Arguments:
//   none
// Changes:
//  8.0 (added)
// Purpose:
//   To show the path of the workspace root for the current directory.
// Output format:
//   A path
// Error conditions:
//   a runtime exception is thrown if the current directory isn't part
//   of a workspace.
CMD_AUTOMATE(get_workspace_root, "",
             N_("Prints the workspace root for the current directory"),
             "",
             options::opts::none)
{
  workspace work(app);
  output << get_current_working_dir() << '\n';
}

// Name: lua
// Arguments:
//   A lua function name
//   Zero or more function arguments
// Changes:
//   9.0 (added)
// Purpose:
//   Execute lua functions and return their results.
// Output format:
//   Lua parsable output.
// Error conditions:
//   a runtime exception is thrown if the function does not exists, the arguments cannot be parsed
//   or the function cannot be executed for some other reason.

CMD_AUTOMATE(lua, "LUA_FUNCTION [ARG1 [ARG2 [...]]]",
             N_("Executes the given lua function and returns the result"),
             "",
             options::opts::none)
{
  E(args.size() >= 1, origin::user,
    F("wrong argument count"));

    std::string func = idx(args, 0)();

    E(app.lua.hook_exists(func), origin::user,
      F("lua function '%s' does not exist") % func);

    std::vector<std::string> func_args;
    if (args.size() > 1)
      {
        for (unsigned int i=1; i<args.size(); i++)
        {
          func_args.push_back(idx(args, i)());
        }
      }

    std::string out;
    E(app.lua.hook_hook_wrapper(func, func_args, out), origin::user,
      F("lua call '%s' failed") % func);

    // the output already contains a trailing newline, so we don't add
    // another one here
    output << out;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
