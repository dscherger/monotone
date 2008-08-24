// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <set>

#include "basic_io.hh"
#include "lexical_cast.hh"
#include "lua_hooks.hh"
#include "vocab.hh"
#include "roster_merge.hh"
#include "options.hh"
#include "parallel_iter.hh"
#include "safe_map.hh"
#include "transforms.hh"

using boost::shared_ptr;

using std::make_pair;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;

static char * const
image(resolve_conflicts::side_t side)
{
  switch (side)
    {
    case resolve_conflicts::left_side:
      return "left";
    case resolve_conflicts::right_side:
      return "right";

    default:
      I(false);
    }
}

static char * const
image(resolve_conflicts::resolution_t resolution)
{
  switch (resolution)
    {
    case resolve_conflicts::none:
      return "none";
    case resolve_conflicts::content_user:
      return "content_user";
    case resolve_conflicts::content_internal:
      return "content_internal";
    case resolve_conflicts::ignore_drop:
      return "ignore_drop";
    case resolve_conflicts::rename:
      return "rename";
    case resolve_conflicts::respect_drop:
      return "respect_drop";
    case resolve_conflicts::suture:
      return "suture";
    default:
      I(false);
    }
}

template <> void
dump(invalid_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "invalid_name_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(directory_loop_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "directory_loop_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(orphaned_node_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "orphaned_node_conflict on node: " << conflict.nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second << "\n";
  out = oss.str();
}

template <> void
dump(multiple_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "multiple_name_conflict on node: " << conflict.nid << " "
      << "left parent: " << conflict.left.first << " "
      << "basename: " << conflict.left.second << " "
      << "right parent: " << conflict.right.first << " "
      << "basename: " << conflict.right.second << "\n";
  out = oss.str();
}

template <> void
dump(duplicate_name_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "duplicate_name_conflict between left node: " << conflict.left_nid << " "
      << "and right node: " << conflict.right_nid << " "
      << "parent: " << conflict.parent_name.first << " "
      << "basename: " << conflict.parent_name.second;

  if (conflict.left_resolution.first != resolve_conflicts::none)
    {
      oss << " left_resolution: " << image(conflict.left_resolution.first);
      oss << " left_name: " << conflict.left_resolution.second;
    }
  if (conflict.right_resolution.first != resolve_conflicts::none)
    {
      oss << " right_resolution: " << image(conflict.right_resolution.first);
      oss << " right_name: " << conflict.right_resolution.second;
    }
  oss << "\n";
  out = oss.str();
}

template <> void
dump(content_drop_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "content_drop_conflict: "
      << "node: "<< conflict.nid << " "
      << "content: " << conflict.fid << " "
      << "parent_side: " << image(conflict.parent_side);

  if (conflict.resolution.first != resolve_conflicts::none)
    {
      oss << " resolution: " << image(conflict.resolution.first);
      if (conflict.resolution.first != resolve_conflicts::ignore_drop)
        {
          oss << " new_name: " << conflict.resolution.second;
        }
    }
  oss << "\n";
  out = oss.str();
}

template <> void
dump(suture_drop_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "suture_drop_conflict: "
      << "sutured_node: " << conflict.sutured_nid << " "
      << "sutured_side: " << image(conflict.sutured_side) << " "
      << "dropped_nodes: ";

  for (set<node_id>::const_iterator i = conflict.dropped_nids.begin(); i != conflict.dropped_nids.end(); i++)
    {
      oss << *i << " ";
    }

  if (conflict.resolution.first != resolve_conflicts::none)
    {
      oss << "resolution: " << image(conflict.resolution.first);
      if (conflict.resolution.first != resolve_conflicts::ignore_drop)
        {
          oss << " new_name: " << conflict.resolution.second;
        }
    }
  oss << "\n";
  out = oss.str();
}

template <> void
dump(attribute_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "attribute_conflict on node: " << conflict.nid << " "
      << "attr: '" << conflict.key << "' "
      << "left: " << conflict.left.first << " '" << conflict.left.second << "' "
      << "right: " << conflict.right.first << " '" << conflict.right.second << "'\n";
  out = oss.str();
}

template <> void
dump(file_content_conflict const & conflict, string & out)
{
  ostringstream oss;
  oss << "file_content_conflict: "
      << "left_node: "<< conflict.left_nid << " "
      << "left_content: " << conflict.left << " "
      << "right_node: " << conflict.right_nid << " "
      << "right_content: " << conflict.right << " "
      << "result_node: " << conflict.result_nid << "\n";
  out = oss.str();
}

bool
roster_merge_result::is_clean() const
{
  return !has_non_content_conflicts()
    && !has_content_conflicts();
}

bool
roster_merge_result::has_content_conflicts() const
{
  return file_content_conflicts.size() > 0;
}

bool
roster_merge_result::has_non_content_conflicts() const
{
  return missing_root_dir
    || !invalid_name_conflicts.empty()
    || !directory_loop_conflicts.empty()
    || !orphaned_node_conflicts.empty()
    || !multiple_name_conflicts.empty()
    || !duplicate_name_conflicts.empty()
    || !content_drop_conflicts.empty()
    || !suture_drop_conflicts.empty()
    || !attribute_conflicts.empty();
}
static void
dump_conflicts(roster_merge_result const & result, string & out)
{
  if (result.missing_root_dir)
    out += (FL("missing_root_conflict: root directory has been removed\n")).str();

  dump(result.invalid_name_conflicts, out);
  dump(result.directory_loop_conflicts, out);

  dump(result.orphaned_node_conflicts, out);
  dump(result.multiple_name_conflicts, out);
  dump(result.duplicate_name_conflicts, out);
  dump(result.content_drop_conflicts, out);
  dump(result.suture_drop_conflicts, out);

  dump(result.attribute_conflicts, out);
  dump(result.file_content_conflicts, out);
}

template <> void
dump(roster_merge_result const & result, string & out)
{
  dump_conflicts(result, out);

  string roster_part;
  dump(result.roster, roster_part);
  out += "\n\n";
  out += roster_part;
}

void
roster_merge_result::log_conflicts() const
{
  string str;
  dump_conflicts(*this, str);
  L(FL("%s") % str);
}

namespace
{
  enum node_type { file_type, dir_type };

  node_type
  get_type(roster_t const & roster, node_id const nid)
  {
    node_t n = roster.get_node(nid);

    if (is_file_t(n))
      return file_type;
    else if (is_dir_t(n))
      return dir_type;
    else
      I(false);
  }

  namespace syms
  {
    symbol const ancestor_file_id("ancestor_file_id");
    symbol const ancestor_name("ancestor_name");
    symbol const attr_name("attr_name");
    symbol const attribute("attribute");
    symbol const conflict("conflict");
    symbol const content("content");
    symbol const content_drop("content_drop");
    symbol const directory_loop_created("directory_loop_created");
    symbol const dropped("dropped");
    symbol const duplicate_name("duplicate_name");
    symbol const invalid_name("invalid_name");
    symbol const left_attr_state("left_attr_state");
    symbol const left_attr_value("left_attr_value");
    symbol const left_file_id("left_file_id");
    symbol const left_name("left_name");
    symbol const left_type("left_type");
    symbol const missing_root("missing_root");
    symbol const multiple_names("multiple_names");
    symbol const node_type("node_type");
    symbol const orphaned_directory("orphaned_directory");
    symbol const orphaned_file("orphaned_file");
    symbol const resolved_ignore_drop("resolved_ignore_drop");
    symbol const resolved_internal("resolved_internal");
    symbol const resolved_rename_left("resolved_rename_left");
    symbol const resolved_rename_right("resolved_rename_right");
    symbol const resolved_respect_drop("resolved_respect_drop");
    symbol const resolved_suture ("resolved_suture");
    symbol const resolved_user("resolved_user");
    symbol const right_attr_state("right_attr_state");
    symbol const right_attr_value("right_attr_value");
    symbol const right_file_id("right_file_id");
    symbol const right_name("right_name");
    symbol const right_type("right_type");
    symbol const suture_drop("suture_drop");
  }
}

static void
put_added_conflict_left (basic_io::stanza & st,
                         content_merge_adaptor & adaptor,
                         node_id const nid)
{
  // We access the roster via the adaptor, to be sure we use the left
  // roster; avoids typos in long parameter lists.

  // If we get a workspace adaptor here someday, we should add the required
  // access functions to content_merge_adaptor.

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> roster(db_adaptor.rosters[db_adaptor.left_rid]);
  file_path name;

  roster->get_name (nid, name);

  if (file_type == get_type (*roster, nid))
    {
      file_id fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, nid, fid);
      st.push_str_pair(syms::left_type, "added file");
      st.push_file_pair(syms::left_name, name);
      st.push_binary_pair(syms::left_file_id, fid.inner());
    }
  else
    {
      st.push_str_pair(syms::left_type, "added directory");
      st.push_file_pair(syms::left_name, name);
    }
}

static void
put_added_conflict_right (basic_io::stanza & st,
                          content_merge_adaptor & adaptor,
                          node_id const nid)
{
  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != roster);

  file_path name;

  roster->get_name (nid, name);

  if (file_type == get_type (*roster, nid))
    {
      file_id fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, nid, fid);

      st.push_str_pair(syms::right_type, "added file");
      st.push_file_pair(syms::right_name, name);
      st.push_binary_pair(syms::right_file_id, fid.inner());
    }
  else
    {
      st.push_str_pair(syms::right_type, "added directory");
      st.push_file_pair(syms::right_name, name);
    }
}

static void
put_rename_conflict_left (basic_io::stanza & st,
                          content_merge_adaptor & adaptor,
                          node_id const nid)
{
  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> ancestor_roster(db_adaptor.rosters[db_adaptor.lca]);
  I(0 != ancestor_roster);
  boost::shared_ptr<roster_t const> left_roster(db_adaptor.rosters[db_adaptor.left_rid]);

  file_path ancestor_name;
  file_path left_name;

  ancestor_roster->get_name (nid, ancestor_name);
  left_roster->get_name (nid, left_name);

  if (file_type == get_type (*left_roster, nid))
    {
      st.push_str_pair(syms::left_type, "renamed file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      file_id left_fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, nid, left_fid);
      st.push_file_pair(syms::left_name, left_name);
      st.push_binary_pair(syms::left_file_id, left_fid.inner());
    }
  else
    {
      st.push_str_pair(syms::left_type, "renamed directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::left_name, left_name);
    }
}

static void
put_rename_conflict_right (basic_io::stanza & st,
                           content_merge_adaptor & adaptor,
                           node_id const nid)
{
  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
  boost::shared_ptr<roster_t const> ancestor_roster(db_adaptor.rosters[db_adaptor.lca]);
  I(0 != ancestor_roster);
  boost::shared_ptr<roster_t const> right_roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != right_roster);

  file_path ancestor_name;
  file_path right_name;

  ancestor_roster->get_name (nid, ancestor_name);
  right_roster->get_name (nid, right_name);

  if (file_type == get_type (*right_roster, nid))
    {
      st.push_str_pair(syms::right_type, "renamed file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      file_id right_fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, nid, right_fid);
      st.push_file_pair(syms::right_name, right_name);
      st.push_binary_pair(syms::right_file_id, right_fid.inner());
    }
  else
    {
      st.push_str_pair(syms::right_type, "renamed directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::right_name, right_name);
    }
}

static void
put_attr_state_left (basic_io::stanza & st, attribute_conflict const & conflict)
{
  if (conflict.left.first)
    st.push_str_pair(syms::left_attr_value, conflict.left.second());
  else
    st.push_str_pair(syms::left_attr_state, "dropped");
}

static void
put_attr_state_right (basic_io::stanza & st, attribute_conflict const & conflict)
{
  if (conflict.right.first)
    st.push_str_pair(syms::right_attr_value, conflict.right.second());
  else
    st.push_str_pair(syms::right_attr_state, "dropped");
}

static void
put_attr_conflict (basic_io::stanza & st,
                   content_merge_adaptor & adaptor,
                   attribute_conflict const & conflict)
{
  // Always report ancestor, left, and right information, for completeness

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));

  // This ensures that the ancestor roster is computed
  boost::shared_ptr<roster_t const> ancestor_roster;
  revision_id ancestor_rid;
  db_adaptor.get_ancestral_roster (conflict.nid, ancestor_rid, ancestor_roster);

  boost::shared_ptr<roster_t const> left_roster(db_adaptor.rosters[db_adaptor.left_rid]);
  I(0 != left_roster);
  boost::shared_ptr<roster_t const> right_roster(db_adaptor.rosters[db_adaptor.right_rid]);
  I(0 != right_roster);

  file_path ancestor_name;
  file_path left_name;
  file_path right_name;

  ancestor_roster->get_name (conflict.nid, ancestor_name);
  left_roster->get_name (conflict.nid, left_name);
  right_roster->get_name (conflict.nid, right_name);

  if (file_type == get_type (*ancestor_roster, conflict.nid))
    {
      st.push_str_pair(syms::node_type, "file");
      st.push_str_pair(syms::attr_name, conflict.key());
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, conflict.nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      file_id left_fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, conflict.nid, left_fid);
      st.push_file_pair(syms::left_name, left_name);
      st.push_binary_pair(syms::left_file_id, left_fid.inner());
      put_attr_state_left (st, conflict);
      file_id right_fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, conflict.nid, right_fid);
      st.push_file_pair(syms::right_name, right_name);
      st.push_binary_pair(syms::right_file_id, right_fid.inner());
      put_attr_state_right (st, conflict);
    }
  else
    {
      st.push_str_pair(syms::node_type, "directory");
      st.push_str_pair(syms::attr_name, conflict.key());
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::left_name, left_name);
      put_attr_state_left (st, conflict);
      st.push_file_pair(syms::right_name, right_name);
      put_attr_state_right (st, conflict);
    }
}

static void
put_content_conflict (basic_io::stanza & st,
                      roster_t const & left_roster,
                      roster_t const & right_roster,
                      content_merge_adaptor & adaptor,
                      file_content_conflict const & conflict)
{
  // Always report ancestor, left, and right information, for completeness

  node_id ancestor_nid;
  boost::shared_ptr<roster_t const> ancestor_roster;
  revision_id ancestor_rid;

  conflict.get_ancestor_roster(adaptor, ancestor_nid, ancestor_rid, ancestor_roster);

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));

  file_path ancestor_name;
  file_path left_name;
  file_path right_name;

  ancestor_roster->get_name (ancestor_nid, ancestor_name);
  left_roster.get_name (conflict.left_nid, left_name);
  right_roster.get_name (conflict.right_nid, right_name);

  if (file_type == get_type (*ancestor_roster, ancestor_nid))
    {
      st.push_str_pair(syms::node_type, "file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (ancestor_rid, ancestor_nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      st.push_file_pair(syms::left_name, left_name);
      st.push_binary_pair(syms::left_file_id, conflict.left.inner());
      st.push_file_pair(syms::right_name, right_name);
      st.push_binary_pair(syms::right_file_id, conflict.right.inner());

      switch (conflict.resolution.first)
        {
        case resolve_conflicts::none:
          break;

        case resolve_conflicts::content_internal:
          st.push_symbol(syms::resolved_internal);
          break;

        case resolve_conflicts::content_user:
          st.push_file_pair(syms::resolved_user, conflict.resolution.second);
          break;

        default:
          I(false);
        }
    }
  else
    {
      st.push_str_pair(syms::node_type, "directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::left_name, left_name);
      st.push_file_pair(syms::right_name, right_name);

      switch (conflict.resolution.first)
        {
        case resolve_conflicts::none:
          break;

        default:
          // not implemented yet
          I(false);
        }
    }
}

static void
put_stanza (basic_io::stanza & st,
            std::ostream & output)
{
  // We have to declare the printer here, rather than more globally,
  // because adaptor.get_ancestral_roster uses a basic_io::printer
  // internally, and there can only be one active at a time.
  basic_io::printer pr;
  output << "\n";
  pr.print_stanza(st);
  output.write(pr.buf.data(), pr.buf.size());
}

void
roster_merge_result::report_missing_root_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   content_merge_adaptor & adaptor,
                                                   bool const basic_io,
                                                   std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  if (missing_root_dir)
    {
      node_id left_root, right_root;
      left_root = left_roster.root()->self;
      right_root = right_roster.root()->self;

      // these must be different for this conflict to happen
      I(left_root != right_root);

      shared_ptr<roster_t const> left_lca_roster, right_lca_roster;
      revision_id left_lca_rid, right_lca_rid;
      file_path left_lca_name, right_lca_name;

      adaptor.get_ancestral_roster(left_root, left_lca_rid,
                                   left_lca_roster);
      adaptor.get_ancestral_roster(right_root, right_lca_rid,
                                   right_lca_roster);

      left_lca_roster->get_name(left_root, left_lca_name);
      right_lca_roster->get_name(right_root, right_lca_name);

      node_id left_lca_root = left_lca_roster->root()->self;
      node_id right_lca_root = right_lca_roster->root()->self;

      basic_io::stanza st;

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::missing_root);
      else
        P(F("conflict: missing root directory"));

      if (left_root != left_lca_root && right_root == right_lca_root)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the left") % left_lca_name);

          if (!right_roster.has_node(left_root))
            if (basic_io)
              {
                st.push_str_pair(syms::right_type, "deleted directory");
                st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
              }
            else
              P(F("directory '%s' deleted on the right") % left_lca_name);
        }
      else if (left_root == left_lca_root && right_root != right_lca_root)
        {
          if (!left_roster.has_node(right_root))
            if (basic_io)
              {
                st.push_str_pair(syms::left_type, "deleted directory");
                st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
              }
            else
              P(F("directory '%s' deleted on the left") % right_lca_name);

          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the right") % right_lca_name);
        }
      else if (left_root != left_lca_root && right_root != right_lca_root)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the left") % left_lca_name);

          if (!right_roster.has_node(left_root))
            if (basic_io)
              {
                st.push_str_pair(syms::right_type, "deleted directory");
                st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
              }
            else
              P(F("directory '%s' deleted on the right") % left_lca_name);

          if (!left_roster.has_node(right_root))
            if (basic_io)
              {
                st.push_str_pair(syms::left_type, "deleted directory");
                st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
              }
            else
              P(F("directory '%s' deleted on the left") % right_lca_name);

          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
            }
          else
            P(F("directory '%s' pivoted to root on the right") % right_lca_name);
        }
      // else
      // other conflicts can cause the root dir to be left detached
      // for example, merging two independently created projects
      // in these cases don't report anything about pivot_root

      if (basic_io)
        put_stanza (st, output);
    }
}

void
roster_merge_result::report_invalid_name_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   content_merge_adaptor & adaptor,
                                                   bool basic_io,
                                                   std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < invalid_name_conflicts.size(); ++i)
    {
      invalid_name_conflict const & conflict = invalid_name_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      shared_ptr<roster_t const> lca_roster, parent_lca_roster;
      revision_id lca_rid, parent_lca_rid;
      file_path lca_name, lca_parent_name;
      basic_io::stanza st;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      lca_roster->get_name(conflict.nid, lca_name);
      lca_roster->get_name(conflict.parent_name.first, lca_parent_name);

      adaptor.get_ancestral_roster(conflict.parent_name.first,
                                   parent_lca_rid, parent_lca_roster);

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::invalid_name);
      else
        P(F("conflict: invalid name _MTN in root directory"));

      if (left_roster.root()->self == conflict.parent_name.first)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, lca_parent_name.as_external());
            }
          else
            P(F("'%s' pivoted to root on the left")
              % lca_parent_name);

          file_path right_name;
          right_roster.get_name(conflict.nid, right_name);
          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_right (st, adaptor, conflict.nid);
              else
                P(F("'%s' renamed to '%s' on the right")
                  % lca_name % right_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_right (st, adaptor, conflict.nid);
              else
                P(F("'%s' added in revision %s on the right")
                  % right_name % lca_rid);
            }
        }
      else if (right_roster.root()->self == conflict.parent_name.first)
        {
          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "pivoted root");
              st.push_str_pair(syms::ancestor_name, lca_parent_name.as_external());
            }
          else
            P(F("'%s' pivoted to root on the right")
              % lca_parent_name);

          file_path left_name;
          left_roster.get_name(conflict.nid, left_name);
          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_left (st, adaptor, conflict.nid);
              else
                P(F("'%s' renamed to '%s' on the left")
                  % lca_name % left_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_left (st, adaptor, conflict.nid);
              else
                P(F("'%s' added in revision %s on the left")
                  % left_name % lca_rid);
            }
        }
      else
        I(false);

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_directory_loop_conflicts(roster_t const & left_roster,
                                                     roster_t const & right_roster,
                                                     content_merge_adaptor & adaptor,
                                                     bool basic_io,
                                                     std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < directory_loop_conflicts.size(); ++i)
    {
      directory_loop_conflict const & conflict = directory_loop_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      file_path left_name, right_name, left_parent_name, right_parent_name;

      left_roster.get_name(conflict.nid, left_name);
      right_roster.get_name(conflict.nid, right_name);

      left_roster.get_name(conflict.parent_name.first, left_parent_name);
      right_roster.get_name(conflict.parent_name.first, right_parent_name);

      shared_ptr<roster_t const> lca_roster;
      revision_id lca_rid;
      file_path lca_name, lca_parent_name;
      basic_io::stanza st;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      lca_roster->get_name(conflict.nid, lca_name);
      lca_roster->get_name(conflict.parent_name.first, lca_parent_name);

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::directory_loop_created);
      else
        P(F("conflict: directory loop created"));

      if (left_name != lca_name)
        if (basic_io)
          put_rename_conflict_left (st, adaptor, conflict.nid);
        else
          P(F("'%s' renamed to '%s' on the left")
            % lca_name % left_name);

      if (right_name != lca_name)
        if (basic_io)
          put_rename_conflict_right (st, adaptor, conflict.nid);
        else
          P(F("'%s' renamed to '%s' on the right")
            % lca_name % right_name);

      if (left_parent_name != lca_parent_name)
        if (basic_io)
          put_rename_conflict_left (st, adaptor, conflict.parent_name.first);
        else
          P(F("'%s' renamed to '%s' on the left")
            % lca_parent_name % left_parent_name);

      if (right_parent_name != lca_parent_name)
        if (basic_io)
          put_rename_conflict_right (st, adaptor, conflict.parent_name.first);
        else
          P(F("'%s' renamed to '%s' on the right")
            % lca_parent_name % right_parent_name);

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_orphaned_node_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor,
                                                    bool basic_io,
                                                    std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < orphaned_node_conflicts.size(); ++i)
    {
      orphaned_node_conflict const & conflict = orphaned_node_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      shared_ptr<roster_t const> lca_roster, parent_lca_roster;
      revision_id lca_rid, parent_lca_rid;
      file_path lca_name;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      adaptor.get_ancestral_roster(conflict.parent_name.first,
                                   parent_lca_rid, parent_lca_roster);

      lca_roster->get_name(conflict.nid, lca_name);

      node_type type = get_type(*lca_roster, conflict.nid);

      basic_io::stanza st;

      if (type == file_type)
          if (basic_io)
            st.push_str_pair(syms::conflict, syms::orphaned_file);
          else
            P(F("conflict: orphaned file '%s' from revision %s")
              % lca_name % lca_rid);
      else
        {
          if (basic_io)
            st.push_str_pair(syms::conflict, syms::orphaned_directory);
          else
            P(F("conflict: orphaned directory '%s' from revision %s")
              % lca_name % lca_rid);
        }

      if (left_roster.has_node(conflict.parent_name.first) &&
          !right_roster.has_node(conflict.parent_name.first))
        {
          file_path orphan_name, parent_name;
          left_roster.get_name(conflict.nid, orphan_name);
          left_roster.get_name(conflict.parent_name.first, parent_name);

          if (basic_io)
            {
              st.push_str_pair(syms::right_type, "deleted directory");
              st.push_str_pair(syms::ancestor_name, parent_name.as_external());
            }
          else
            P(F("parent directory '%s' was deleted on the right")
              % parent_name);

          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_left (st, adaptor, conflict.nid);
              else
                if (type == file_type)
                  P(F("file '%s' was renamed from '%s' on the left")
                    % orphan_name % lca_name);
                else
                  P(F("directory '%s' was renamed from '%s' on the left")
                    % orphan_name % lca_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_left (st, adaptor, conflict.nid);
              else
                {
                  if (type == file_type)
                    P(F("file '%s' was added on the left")
                      % orphan_name);
                  else
                    P(F("directory '%s' was added on the left")
                      % orphan_name);
                }
            }
        }
      else if (!left_roster.has_node(conflict.parent_name.first) &&
               right_roster.has_node(conflict.parent_name.first))
        {
          file_path orphan_name, parent_name;
          right_roster.get_name(conflict.nid, orphan_name);
          right_roster.get_name(conflict.parent_name.first, parent_name);

          if (basic_io)
            {
              st.push_str_pair(syms::left_type, "deleted directory");
              st.push_str_pair(syms::ancestor_name, parent_name.as_external());
            }
          else
            P(F("parent directory '%s' was deleted on the left")
              % parent_name);

          if (parent_lca_roster->has_node(conflict.nid))
            {
              if (basic_io)
                put_rename_conflict_right (st, adaptor, conflict.nid);
              else
                if (type == file_type)
                  P(F("file '%s' was renamed from '%s' on the right")
                    % orphan_name % lca_name);
                else
                  P(F("directory '%s' was renamed from '%s' on the right")
                    % orphan_name % lca_name);
            }
          else
            {
              if (basic_io)
                put_added_conflict_right (st, adaptor, conflict.nid);
              else
                if (type == file_type)
                  P(F("file '%s' was added on the right")
                    % orphan_name);
                else
                  P(F("directory '%s' was added on the right")
                    % orphan_name);
            }
        }
      else
        I(false);

      if (basic_io)
        put_stanza (st, output);
    }
}

void
roster_merge_result::report_multiple_name_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor,
                                                    bool basic_io,
                                                    std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < multiple_name_conflicts.size(); ++i)
    {
      multiple_name_conflict const & conflict = multiple_name_conflicts[i];
      MM(conflict);

      I(!roster.is_attached(conflict.nid));

      file_path left_name, right_name;

      left_roster.get_name(conflict.nid, left_name);
      right_roster.get_name(conflict.nid, right_name);

      shared_ptr<roster_t const> lca_roster;
      revision_id lca_rid;
      file_path lca_name;

      adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
      lca_roster->get_name(conflict.nid, lca_name);

      node_type type = get_type(*lca_roster, conflict.nid);

      basic_io::stanza st;

      if (basic_io)
        {
          st.push_str_pair(syms::conflict, syms::multiple_names);
          put_rename_conflict_left (st, adaptor, conflict.nid);
          put_rename_conflict_right (st, adaptor, conflict.nid);
        }
      else
        {
          if (type == file_type)
            P(F("conflict: multiple names for file '%s' from revision %s")
              % lca_name % lca_rid);
          else
            P(F("conflict: multiple names for directory '%s' from revision %s")
              % lca_name % lca_rid);

          P(F("renamed to '%s' on the left") % left_name);
          P(F("renamed to '%s' on the right") % right_name);
        }

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_duplicate_name_conflicts(roster_t const & left_roster,
                                                     roster_t const & right_roster,
                                                     content_merge_adaptor & adaptor,
                                                     bool const basic_io,
                                                     std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < duplicate_name_conflicts.size(); ++i)
    {
      duplicate_name_conflict const & conflict = duplicate_name_conflicts[i];
      MM(conflict);

      node_id left_nid, right_nid;

      left_nid = conflict.left_nid;
      right_nid = conflict.right_nid;

      I(!roster.is_attached(left_nid));
      I(!roster.is_attached(right_nid));

      file_path left_name, right_name;

      left_roster.get_name(left_nid, left_name);
      right_roster.get_name(right_nid, right_name);

      shared_ptr<roster_t const> left_lca_roster, right_lca_roster;
      revision_id left_lca_rid, right_lca_rid;

      adaptor.get_ancestral_roster(left_nid, left_lca_rid, left_lca_roster);
      adaptor.get_ancestral_roster(right_nid, right_lca_rid, right_lca_roster);

      // In most cases, the left_name equals the right_name. However, maybe
      // a parent directory got renamed on one side. In that case, the names
      // don't match, but it's still the same directory (by node id), to
      // which we want to add the same file (by name).

      basic_io::stanza st;

      if (basic_io)
        st.push_str_pair(syms::conflict, syms::duplicate_name);
      else
        {
          if (left_name == right_name)
            {
              file_path dir;
              path_component basename;
              left_name.dirname_basename(dir, basename);
              P(F("conflict: duplicate name '%s' for the directory '%s'") % basename % dir);
            }
          else
            {
              file_path left_dir, right_dir;
              path_component left_basename, right_basename;
              left_name.dirname_basename(left_dir, left_basename);
              right_name.dirname_basename(right_dir, right_basename);
              I(left_basename == right_basename);
              P(F("conflict: duplicate name '%s' for the directory\n"
                  "          named '%s' on the left and\n"
                  "          named '%s' on the right.")
                % left_basename % left_dir % right_dir);
            }
        }

      node_type left_type  = get_type(left_roster, left_nid);
      node_type right_type = get_type(right_roster, right_nid);

      if (!left_lca_roster->has_node(right_nid) &&
          !right_lca_roster->has_node(left_nid))
        {
          if (basic_io)
            put_added_conflict_left (st, adaptor, left_nid);
          else
            {
              if (left_type == file_type)
                P(F("added as a new file on the left"));
              else
                P(F("added as a new directory on the left"));
            }

          if (basic_io)
            put_added_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("added as a new file on the right"));
              else
                P(F("added as a new directory on the right"));
            }
         }
      else if (!left_lca_roster->has_node(right_nid) &&
               right_lca_roster->has_node(left_nid))
        {
          file_path left_lca_name;
          left_lca_roster->get_name(left_nid, left_lca_name);

          if (basic_io)
            put_rename_conflict_left (st, adaptor, left_nid);
          else
            if (left_type == file_type)
              P(F("renamed from file '%s' on the left") % left_lca_name);
            else
              P(F("renamed from directory '%s' on the left") % left_lca_name);

          if (basic_io)
            put_added_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("added as a new file on the right"));
              else
                P(F("added as a new directory on the right"));
            }
        }
      else if (left_lca_roster->has_node(right_nid) &&
               !right_lca_roster->has_node(left_nid))
        {
          file_path right_lca_name;
          right_lca_roster->get_name(right_nid, right_lca_name);

          if (basic_io)
            put_added_conflict_left (st, adaptor, left_nid);
          else
            {
              if (left_type == file_type)
                P(F("added as a new file on the left"));
              else
                P(F("added as a new directory on the left"));
            }

          if (basic_io)
            put_rename_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("renamed from file '%s' on the right") % right_lca_name);
              else
                P(F("renamed from directory '%s' on the right") % right_lca_name);
            }
        }
      else if (left_lca_roster->has_node(right_nid) &&
               right_lca_roster->has_node(left_nid))
        {
          file_path left_lca_name, right_lca_name;
          left_lca_roster->get_name(left_nid, left_lca_name);
          right_lca_roster->get_name(right_nid, right_lca_name);

          if (basic_io)
            put_rename_conflict_left (st, adaptor, left_nid);
          else
            {
              if (left_type == file_type)
                P(F("renamed from file '%s' on the left") % left_lca_name);
              else
                P(F("renamed from directory '%s' on the left") % left_lca_name);
            }

          if (basic_io)
            put_rename_conflict_right (st, adaptor, right_nid);
          else
            {
              if (right_type == file_type)
                P(F("renamed from file '%s' on the right") % right_lca_name);
              else
                P(F("renamed from directory '%s' on the right") % right_lca_name);
            }
        }
      else
        I(false);

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_content_drop_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   bool const basic_io,
                                                   std::ostream & output) const
{
  for (size_t i = 0; i < content_drop_conflicts.size(); ++i)
    {
      content_drop_conflict const & conflict = content_drop_conflicts[i];

      basic_io::stanza st;

      file_path name;

      switch (conflict.parent_side)
        {
        case resolve_conflicts::left_side:
          left_roster.get_name (conflict.nid, name);
          I(file_type == get_type (left_roster, conflict.nid));

          if (basic_io)
            {
              st.push_str_pair(syms::conflict, syms::content_drop);
              st.push_str_pair(syms::left_type, "file");
              st.push_file_pair(syms::left_name, name);
              st.push_binary_pair(syms::left_file_id, conflict.fid.inner());
            }
          else
            {
              P(F("conflict: file '%s' dropped on the right, changed on the left") % name);
            }

          break;

        case resolve_conflicts::right_side:
          right_roster.get_name (conflict.nid, name);
          I(file_type == get_type (right_roster, conflict.nid));

          if (basic_io)
            {
              st.push_str_pair(syms::conflict, syms::content_drop);
              st.push_str_pair(syms::right_type, "file");
              st.push_file_pair(syms::right_name, name);
              st.push_binary_pair(syms::right_file_id, conflict.fid.inner());
            }
          else
            {
              P(F("conflict: file '%s' dropped on the left, changed on the right") % name);
            }

          break;
        }

      if (basic_io)
        {
          switch (conflict.resolution.first)
            {
            case resolve_conflicts::none:
              break;

            case resolve_conflicts::ignore_drop:
              st.push_file_pair(syms::resolved_ignore_drop, conflict.resolution.second);
              break;

            case resolve_conflicts::respect_drop:
              st.push_symbol(syms::resolved_respect_drop);
              break;

            default:
              I(false);
            }

          put_stanza(st, output);
        }
    }
}

static void
push_node_id_set(roster_t const & roster,
                 basic_io::stanza & st,
                 symbol const & k,
                 set<node_id> nids)
{
  std::vector<std::string> string_nids;

  for (set<node_id>::const_iterator i = nids.begin(); i != nids.end(); i++)
    {
      string_nids.push_back(boost::lexical_cast<string>(*i));
    }
  st.push_str_multi(k, string_nids);
}

void
roster_merge_result::report_suture_drop_conflicts(roster_t const & left_roster,
                                                  roster_t const & right_roster,
                                                  bool const basic_io,
                                                  std::ostream & output) const
{
  for (size_t i = 0; i < suture_drop_conflicts.size(); ++i)
    {
      suture_drop_conflict const & conflict = suture_drop_conflicts[i];

      basic_io::stanza st;

      file_path name;

      switch (conflict.sutured_side)
        {
        case resolve_conflicts::left_side:
          left_roster.get_name (conflict.sutured_nid, name);
          I(file_type == get_type (left_roster, conflict.sutured_nid));

          if (basic_io)
            {
              st.push_str_pair(syms::conflict, syms::suture_drop);
              st.push_str_pair(syms::left_type, "file");
              st.push_file_pair(syms::left_name, name);
              push_node_id_set(left_roster, st, syms::dropped, conflict.dropped_nids);
            }
          else
            {
              P(F("conflict: file '%s' sutured on the left, some parents dropped on the right") % name);
              // It would be nice to print the names of the dropped nodes
              // here, but since they are not present in any of the rosters
              // we currently have access to, we'd have to retrieve the
              // revision containing their last name change to do that; not
              // worth it.
            }

          break;

        case resolve_conflicts::right_side:
          right_roster.get_name (conflict.sutured_nid, name);
          I(file_type == get_type (right_roster, conflict.sutured_nid));

          if (basic_io)
            {
              st.push_str_pair(syms::conflict, syms::suture_drop);
              st.push_str_pair(syms::right_type, "file");
              st.push_file_pair(syms::right_name, name);
              push_node_id_set(right_roster, st, syms::dropped, conflict.dropped_nids);
            }
          else
            {
              P(F("conflict: file '%s' sutured on the right, some parents dropped on the left") % name);
            }

          break;
        }

      if (basic_io)
        {
          switch (conflict.resolution.first)
            {
            case resolve_conflicts::none:
              break;

            case resolve_conflicts::ignore_drop:
              st.push_file_pair(syms::resolved_ignore_drop, conflict.resolution.second);
              break;

            case resolve_conflicts::respect_drop:
              st.push_symbol(syms::resolved_respect_drop);
              break;

            default:
              I(false);
            }

          put_stanza(st, output);
        }
    }
}

void
roster_merge_result::report_attribute_conflicts(roster_t const & left_roster,
                                                roster_t const & right_roster,
                                                content_merge_adaptor & adaptor,
                                                bool basic_io,
                                                std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < attribute_conflicts.size(); ++i)
    {
      attribute_conflict const & conflict = attribute_conflicts[i];
      MM(conflict);

      if (basic_io)
        {
          basic_io::stanza st;

          st.push_str_pair(syms::conflict, syms::attribute);
          put_attr_conflict (st, adaptor, conflict);
          put_stanza (st, output);
        }
      else
        {
          node_type type = get_type(roster, conflict.nid);

          if (roster.is_attached(conflict.nid))
            {
              file_path name;
              roster.get_name(conflict.nid, name);

              if (type == file_type)
                P(F("conflict: multiple values for attribute '%s' on file '%s'")
                  % conflict.key % name);
              else
                P(F("conflict: multiple values for attribute '%s' on directory '%s'")
                  % conflict.key % name);

              if (conflict.left.first)
                P(F("set to '%s' on the left") % conflict.left.second);
              else
                P(F("deleted on the left"));

              if (conflict.right.first)
                P(F("set to '%s' on the right") % conflict.right.second);
              else
                P(F("deleted on the right"));
            }
          else
            {
              // This node isn't attached in the merged roster, due to another
              // conflict (ie renamed to different names). So report the
              // ancestor name and the left and right names.

              file_path left_name, right_name;
              left_roster.get_name(conflict.nid, left_name);
              right_roster.get_name(conflict.nid, right_name);

              shared_ptr<roster_t const> lca_roster;
              revision_id lca_rid;
              file_path lca_name;

              adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
              lca_roster->get_name(conflict.nid, lca_name);

              if (type == file_type)
                P(F("conflict: multiple values for attribute '%s' on file '%s' from revision %s")
                  % conflict.key % lca_name % lca_rid);
              else
                P(F("conflict: multiple values for attribute '%s' on directory '%s' from revision %s")
                  % conflict.key % lca_name % lca_rid);

              if (conflict.left.first)
                {
                  if (type == file_type)
                    P(F("set to '%s' on left file '%s'")
                      % conflict.left.second % left_name);
                  else
                    P(F("set to '%s' on left directory '%s'")
                      % conflict.left.second % left_name);
                }
              else
                {
                  if (type == file_type)
                    P(F("deleted from left file '%s'")
                      % left_name);
                  else
                    P(F("deleted from left directory '%s'")
                      % left_name);
                }

              if (conflict.right.first)
                {
                  if (type == file_type)
                    P(F("set to '%s' on right file '%s'")
                      % conflict.right.second % right_name);
                  else
                    P(F("set to '%s' on right directory '%s'")
                      % conflict.right.second % right_name);
                }
              else
                {
                  if (type == file_type)
                    P(F("deleted from right file '%s'")
                      % right_name);
                  else
                    P(F("deleted from right directory '%s'")
                      % right_name);
                }
            }
        }
    }
}

void
file_content_conflict::get_ancestor_roster(content_merge_adaptor & adaptor,
                                           node_id & ancestor_nid,
                                           revision_id & ancestor_rid,
                                           shared_ptr<roster_t const> & ancestor_roster) const
{
  if (left_nid == right_nid)
    {
      // Either there is a least common ancestor, or we use the birth
      // revision for the node as the ancestor.
      ancestor_nid = left_nid;
    }
  else
    {
      // One side is a suture or split; it will have a larger node id. Use
      // the smaller nid to retrieve the least common ancestor.
      // FIXME_SUTURE: what if both sides are sutured? need to find
      // ancestor_nid via birth records; database_adaptor has those in the
      // marking maps. get_ancestral_roster needs to take left_nid,
      // right_nid.
      if (left_nid < right_nid)
        ancestor_nid = left_nid;
      else
        ancestor_nid = right_nid;
    };

  // This also sets adaptor.lca.
  adaptor.get_ancestral_roster (ancestor_nid, ancestor_rid, ancestor_roster);
}

namespace
{
  bool
  auto_merge_succeeds(lua_hooks & lua,
                      file_content_conflict conflict,
                      content_merge_adaptor & adaptor,
                      roster_t const & left_roster,
                      roster_t const & right_roster)
  {
    node_id ancestor_nid;
    revision_id ancestor_rid;
    shared_ptr<roster_t const> ancestor_roster;
    conflict.get_ancestor_roster(adaptor, ancestor_nid, ancestor_rid, ancestor_roster);

    I(ancestor_roster);
    I(ancestor_roster->has_node(ancestor_nid)); // this fails if there is no least common ancestor

    file_id anc_id, left_id, right_id;
    file_path anc_path, left_path, right_path;
    ancestor_roster->get_file_details(ancestor_nid, anc_id, anc_path);
    left_roster.get_file_details(conflict.left_nid, left_id, left_path);
    right_roster.get_file_details(conflict.right_nid, right_id, right_path);

    content_merger cm(lua, *ancestor_roster, left_roster, right_roster, adaptor);

    file_data left_data, right_data, merge_data;

    return cm.attempt_auto_merge(anc_path, left_path, right_path,
                                 anc_id, left_id, right_id,
                                 left_data, right_data, merge_data);
  }
}

void
roster_merge_result::report_file_content_conflicts(lua_hooks & lua,
                                                   roster_t const & left_roster,
                                                   roster_t const & right_roster,
                                                   content_merge_adaptor & adaptor,
                                                   bool basic_io,
                                                   std::ostream & output)
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < file_content_conflicts.size(); ++i)
    {
      file_content_conflict & conflict = file_content_conflicts[i];
      MM(conflict);

      if (basic_io)
        {
          basic_io::stanza st;

          if (auto_merge_succeeds(lua, conflict, adaptor, left_roster, right_roster))
            conflict.resolution = make_pair(resolve_conflicts::content_internal, file_path());

          st.push_str_pair(syms::conflict, syms::content);
          put_content_conflict (st, left_roster, right_roster, adaptor, conflict);
          put_stanza (st, output);
        }
      else
        {
          if (roster.is_attached(conflict.result_nid))
            {
              file_path name;
              roster.get_name(conflict.result_nid, name);

              P(F("conflict: content conflict on file '%s'")
                % name);
              P(F("content hash is %s on the left") % conflict.left);
              P(F("content hash is %s on the right") % conflict.right);
            }
          else
            {
              // this node isn't attached in the merged roster and there
              // isn't really a good name for it so report both the left
              // and right names using a slightly different format

              node_id ancestor_nid;
              boost::shared_ptr<roster_t const> ancestor_roster;
              revision_id ancestor_rid;

              conflict.get_ancestor_roster(adaptor, ancestor_nid, ancestor_rid, ancestor_roster);

              file_path left_name, right_name, ancestor_name;
              left_roster.get_name(conflict.left_nid, left_name);
              right_roster.get_name(conflict.right_nid, right_name);
              ancestor_roster->get_name(ancestor_nid, ancestor_name);

              P(F("conflict: content conflict on file '%s' from revision %s")
                % ancestor_name % ancestor_rid);
              P(F("content hash is %s on the left in file '%s'")
                % conflict.left % left_name);
              P(F("content hash is %s on the right in file '%s'")
                % conflict.right % right_name);
            }
        }
    }
}

// Resolving non-content conflicts

namespace resolve_conflicts
{
  bool
  do_auto_merge(lua_hooks & lua,
                file_content_conflict const & conflict,
                content_merge_adaptor & adaptor,
                roster_t const & left_roster,
                roster_t const & right_roster,
                roster_t const & result_roster,
                file_id & merged_id)
  {
    node_id ancestor_nid;
    revision_id ancestor_rid;
    shared_ptr<roster_t const> ancestor_roster;
    conflict.get_ancestor_roster(adaptor, ancestor_nid, ancestor_rid, ancestor_roster);

    I(ancestor_roster);
    I(ancestor_roster->has_node(ancestor_nid)); // this fails if there is no least common ancestor

    file_id anc_id, left_id, right_id;
    file_path anc_path, left_path, right_path, merged_path;
    ancestor_roster->get_file_details(ancestor_nid, anc_id, anc_path);
    left_roster.get_file_details(conflict.left_nid, left_id, left_path);
    right_roster.get_file_details(conflict.right_nid, right_id, right_path);
    result_roster.get_file_details(conflict.result_nid, merged_id, merged_path);

    content_merger cm(lua, *ancestor_roster, left_roster, right_roster, adaptor);

    return cm.try_auto_merge(anc_path, left_path, right_path, merged_path,
                             anc_id, left_id, right_id, merged_id);
  }
}

static void
parse_duplicate_name_conflicts(basic_io::parser & pars,
                               std::vector<duplicate_name_conflict> & conflicts,
                               roster_t const & left_roster,
                               roster_t const & right_roster)
{
  for (std::vector<duplicate_name_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      duplicate_name_conflict & conflict = *i;

      pars.esym(syms::duplicate_name);

      node_id left_nid, right_nid;
      string left_name, right_name;

      pars.esym(syms::left_type); pars.str();
      pars.esym (syms::left_name); pars.str(left_name);
      pars.esym(syms::left_file_id); pars.hex();

      pars.esym(syms::right_type); pars.str();
      pars.esym (syms::right_name); pars.str(right_name);
      pars.esym(syms::right_file_id); pars.hex();

      left_nid = left_roster.get_node (file_path_internal (left_name))->self;
      right_nid = right_roster.get_node (file_path_internal (right_name))->self;

      // Note that we cannot confirm the file ids.
      N(left_nid == conflict.left_nid & right_nid == conflict.right_nid,
        F("conflicts file does not match current conflicts: (duplicate_name, left %s, right %s")
        % left_name % right_name);

      // check for a resolution
      while ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
        {
          if (pars.symp (syms::resolved_suture))
            {
              conflict.left_resolution.first = resolve_conflicts::suture;
              conflict.right_resolution.first = resolve_conflicts::suture;
              pars.sym();
              conflict.left_resolution.second = file_path_internal (pars.token);
              pars.str();
            }
          else if (pars.symp (syms::resolved_rename_left))
            {
              conflict.left_resolution.first = resolve_conflicts::rename;
              pars.sym();
              conflict.left_resolution.second = file_path_internal (pars.token);
              pars.str();
            }
          else if (pars.symp (syms::resolved_rename_right))
            {
              conflict.right_resolution.first = resolve_conflicts::rename;
              pars.sym();
              conflict.right_resolution.second = file_path_internal (pars.token);
              pars.str();
            }
          else
            N(false, F("%s is not a supported conflict resolution for %s") % pars.token % "duplicate_name");
        }

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<duplicate_name_conflict>::iterator tmp = i;
          N(++tmp == conflicts.end(), F("conflicts file does not match current conflicts"));
        }
    }
} // parse_duplicate_name_conflicts

static void
parse_content_drop_conflicts(basic_io::parser & pars,
                             std::vector<content_drop_conflict> & conflicts,
                             roster_t const & left_roster,
                             roster_t const & right_roster)
{
  for (std::vector<content_drop_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      string tmp;
      string name;
      resolve_conflicts::side_t parent_side;
      node_id nid;
      string hex_fid;

      content_drop_conflict & conflict = *i;

      pars.esym(syms::content_drop);

      if (pars.symp (syms::left_type))
        {
          parent_side = resolve_conflicts::left_side;
          pars.sym();
          pars.str(tmp);
          I(tmp == "file");
          pars.esym(syms::left_name);
          pars.str(name);
          pars.esym(syms::left_file_id); pars.hex(hex_fid);
          nid = left_roster.get_node (file_path_internal (name))->self;
        }
      else
        {
          parent_side = resolve_conflicts::right_side;
          pars.sym();
          pars.str(tmp);
          I(tmp == "file");
          pars.esym (syms::right_name); pars.str(name);
          pars.esym(syms::right_file_id); pars.hex(hex_fid);
          nid = right_roster.get_node (file_path_internal (name))->self;
        }

      N(parent_side == conflict.parent_side && nid == conflict.nid && hex_fid == encode_hexenc(conflict.fid.inner()()),
        F("conflicts file does not match current conflicts: content_drop, name %s")
        % name);

      // check for a resolution
      if ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
        {
          if (pars.symp (syms::resolved_suture))
            {
              conflict.resolution.first = resolve_conflicts::suture;
              pars.sym();
              pars.str(tmp);
              conflict.resolution.second = file_path_internal(tmp);
            }
          else if (pars.symp (syms::resolved_ignore_drop))
            {
              conflict.resolution.first = resolve_conflicts::ignore_drop;
              pars.sym();
              pars.str(tmp);
              conflict.resolution.second = file_path_internal(tmp);
            }
          else if (pars.symp (syms::resolved_respect_drop))
            {
              conflict.resolution.first = resolve_conflicts::respect_drop;
              pars.sym();
            }
          else
            N(false, F("%s is not a supported conflict resolution for %s") % pars.token % "content_drop");
        }

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<content_drop_conflict>::iterator tmp = i;
          N(++tmp == conflicts.end(), F("conflicts file does not match current conflicts"));
        }
    }
} // parse_content_drop_conflicts

static void
parse_node_id_set(set<node_id> & dropped_nids,
                  int expected_count,
                  basic_io::parser & pars)
{
  string nid;

  for (int i = 0; i < expected_count; i++)
    {
      pars.str(nid);
      dropped_nids.insert(boost::lexical_cast<int>(nid));
    }
}

static void
parse_suture_drop_conflicts(basic_io::parser & pars,
                            std::vector<suture_drop_conflict> & conflicts,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  for (std::vector<suture_drop_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      string tmp;
      string name;
      resolve_conflicts::side_t sutured_side;
      node_id sutured_nid;
      set<node_id> dropped_nids;

      suture_drop_conflict & conflict = *i;

      pars.esym(syms::suture_drop);

      if (pars.symp (syms::left_type))
        {
          sutured_side = resolve_conflicts::left_side;
          pars.sym();
          pars.str(tmp);
          I(tmp == "file");
          pars.esym(syms::left_name); pars.str(name);

          pars.esym(syms::dropped);
          parse_node_id_set(dropped_nids, conflict.dropped_nids.size(), pars);
        }
      else
        {
          sutured_side = resolve_conflicts::right_side;
          pars.sym();
          pars.str(tmp);
          I(tmp == "file");
          pars.esym (syms::right_name); pars.str(name);

          parse_node_id_set(dropped_nids, conflict.dropped_nids.size(), pars);
        }

      N(sutured_side == conflict.sutured_side &&
        sutured_nid == conflict.sutured_nid &&
        dropped_nids == conflict.dropped_nids,
        F("conflicts file does not match current conflicts: suture_drop, name %s")
        % name);

      // check for a resolution
      if ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
        {
          if (pars.symp (syms::resolved_ignore_drop))
            {
              conflict.resolution.first = resolve_conflicts::ignore_drop;
              pars.sym();
              pars.str(tmp);
              conflict.resolution.second = file_path_internal(tmp);
            }
          else
            N(false, F("%s is not a supported conflict resolution for %s") % pars.token % "suture_drop");
        }

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<suture_drop_conflict>::iterator tmp = i;
          N(++tmp == conflicts.end(), F("conflicts file does not match current conflicts"));
        }
    }
} // parse_suture_drop_conflicts

static void
parse_file_content_conflicts(basic_io::parser & pars,
                               std::vector<file_content_conflict> & conflicts,
                               roster_t const & left_roster,
                               roster_t const & right_roster)
{
  for (std::vector<file_content_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      string tmp;
      node_id left_nid, right_nid;
      string left_name, right_name, result_name;

      file_content_conflict & conflict = *i;

      pars.esym(syms::content);

      pars.esym(syms::node_type);
      pars.str(tmp);
      I(tmp == "file");

      pars.esym (syms::ancestor_name); pars.str();
      pars.esym (syms::ancestor_file_id); pars.hex();

      pars.esym (syms::left_name); pars.str(left_name);
      pars.esym(syms::left_file_id); pars.hex();

      pars.esym (syms::right_name); pars.str(right_name);
      pars.esym(syms::right_file_id); pars.hex();

      left_nid = left_roster.get_node (file_path_internal (left_name))->self;
      right_nid = right_roster.get_node (file_path_internal (right_name))->self;

      N(left_nid == conflict.left_nid & right_nid == conflict.right_nid,
        F("conflicts file does not match current conflicts: (file_content, left %s, right %s")
        % left_name % right_name);

      // check for a resolution
      if ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
        {
          if (pars.symp (syms::resolved_internal))
            {
              conflict.resolution.first = resolve_conflicts::content_internal;
              pars.sym();
            }
          else if (pars.symp (syms::resolved_user))
            {
              conflict.resolution.first = resolve_conflicts::content_user;
              pars.sym();
              conflict.resolution.second = file_path_internal (pars.token);
              pars.str();
            }
          else
            N(false, F("%s is not a supported conflict resolution for %s") % pars.token % "file_content");
        }

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<file_content_conflict>::iterator tmp = i;
          N(++tmp == conflicts.end(), F("conflicts file does not match current conflicts"));
        }
    }
} // parse_file_content_conflicts

static void
parse_resolve_conflicts_str(basic_io::parser & pars, roster_merge_result & result)
{
  char const * error_message_1 = "can't specify a %s conflict resolution for more than one conflict";
  char const * error_message_2 = "conflict resolution %s is not appropriate for current conflicts";

  // We don't detect all cases of inappropriate resolutions here; that would
  // be too hard to maintain as more conflicts and/or resolutions are added.
  // If the single resolution specified is not appropriate for some
  // conflict, that conflict will not be resolved, which will be reported
  // later. Then the user will need to use a conflict resolution file.
  while (pars.tok.in.lookahead != EOF)
    {
      // resolution alphabetical order
      if (pars.symp (syms::resolved_ignore_drop))
        {
          pars.sym();

          N(result.content_drop_conflicts.size() + result.suture_drop_conflicts.size() > 0,
            F(error_message_2) % syms::resolved_ignore_drop);

          if (result.content_drop_conflicts.size() == 1)
            {
              content_drop_conflict & conflict = *result.content_drop_conflicts.begin();
              string tmp;
              pars.str(tmp);
              conflict.resolution = make_pair(resolve_conflicts::ignore_drop, file_path_internal(tmp));
            }
          else if (result.suture_drop_conflicts.size() == 1)
            {
              suture_drop_conflict & conflict = *result.suture_drop_conflicts.begin();
              string tmp;
              pars.str(tmp);
              conflict.resolution = make_pair(resolve_conflicts::ignore_drop, file_path_internal(tmp));
            }
          else
            N(false,
              F(error_message_1) % syms::resolved_ignore_drop);

        }
      else if (pars.symp (syms::resolved_rename_left))
        {
          N(result.duplicate_name_conflicts.size() == 1,
            F(error_message_1) % syms::resolved_rename_left);

          duplicate_name_conflict & conflict = *result.duplicate_name_conflicts.begin();

          conflict.left_resolution.first  = resolve_conflicts::rename;
          pars.sym();
          conflict.left_resolution.second = file_path_internal (pars.token);
          pars.str();
        }
      else if (pars.symp (syms::resolved_rename_right))
        {
          N(result.duplicate_name_conflicts.size() == 1,
            F(error_message_1) % syms::resolved_rename_right);

          duplicate_name_conflict & conflict = *result.duplicate_name_conflicts.begin();

          conflict.right_resolution.first  = resolve_conflicts::rename;
          pars.sym();
          conflict.right_resolution.second = file_path_internal (pars.token);
          pars.str();
        }
      else if (pars.symp (syms::resolved_respect_drop))
        {
          pars.sym();

          N(result.content_drop_conflicts.size() > 0,
            F(error_message_2) % syms::resolved_respect_drop % syms::content_drop);

          N(result.content_drop_conflicts.size() == 1,
            F(error_message_1) % syms::resolved_ignore_drop);

          content_drop_conflict & conflict = *result.content_drop_conflicts.begin();
          conflict.resolution = make_pair(resolve_conflicts::respect_drop, file_path());
        }
      else if (pars.symp (syms::resolved_suture))
        {
          if (result.duplicate_name_conflicts.size() == 1 &&
              result.content_drop_conflicts.size() == 1)
            {
              duplicate_name_conflict & dn_conflict = *result.duplicate_name_conflicts.begin();
              content_drop_conflict & cd_conflict = *result.content_drop_conflicts.begin();

              dn_conflict.left_resolution.first  = resolve_conflicts::suture;
              dn_conflict.right_resolution.first = resolve_conflicts::suture;
              pars.sym();
              dn_conflict.left_resolution.second = file_path_internal (pars.token);
              dn_conflict.right_resolution.second = dn_conflict.left_resolution.second;
              pars.str();

              cd_conflict.resolution.first = resolve_conflicts::suture;
              cd_conflict.resolution.second = dn_conflict.left_resolution.second;
            }
          else if (result.duplicate_name_conflicts.size() == 1 &&
                   result.content_drop_conflicts.size() == 0)
            {
              duplicate_name_conflict & conflict = *result.duplicate_name_conflicts.begin();

              conflict.left_resolution.first  = resolve_conflicts::suture;
              conflict.right_resolution.first = resolve_conflicts::suture;
              pars.sym();
              conflict.left_resolution.second = file_path_internal (pars.token);
              pars.str();
            }
          else if (result.duplicate_name_conflicts.size() == 0 &&
                   result.content_drop_conflicts.size() == 1)
            {
              content_drop_conflict & cd_conflict = *result.content_drop_conflicts.begin();

              cd_conflict.resolution.first = resolve_conflicts::suture;
              pars.sym();
              cd_conflict.resolution.second = file_path_internal (pars.token);
              pars.str();
            }
          else
            N(false, F(error_message_2) % syms::resolved_suture);
        }
      else if (pars.symp (syms::resolved_user))
        {
          N(result.file_content_conflicts.size() == 1,
            F(error_message_1) % syms::resolved_user);

          file_content_conflict & conflict = *result.file_content_conflicts.begin();

          conflict.resolution.first  = resolve_conflicts::content_user;
          pars.sym();
          conflict.resolution.second = file_path_internal (pars.token);
          pars.str();
        }
      else
        N(false, F("%s is not a supported conflict resolution") % pars.token);

    } // while
}

void
parse_resolve_conflicts_opts (options const & opts,
                              roster_t const & left_roster,
                              roster_t const & right_roster,
                              roster_merge_result & result,
                              bool & resolutions_given)
{
  if (opts.resolve_conflicts_given)
    {
      resolutions_given = true;

      basic_io::input_source src(opts.resolve_conflicts, "resolve_conflicts string");
      basic_io::tokenizer tok(src);
      basic_io::parser pars(tok);

      parse_resolve_conflicts_str(pars, result);

      if (src.lookahead != EOF)
        pars.err("invalid conflict resolution syntax");
    }
  else if (opts.resolve_conflicts_file_given)
    {
      resolutions_given = true;

      data dat;

      if (opts.resolve_conflicts_file().substr(0, 4) == "_MTN")
        read_data (bookkeeping_path(opts.resolve_conflicts_file()), dat);
      else
        read_data (file_path_external(opts.resolve_conflicts_file), dat);

      basic_io::input_source src(dat(), opts.resolve_conflicts_file());
      basic_io::tokenizer tok(src);
      basic_io::parser pars(tok);

      // Skip left, right, ancestor. FIXME_SUTURE: should check these! But don't
      // see how to access them right now.
      for (int i = 1; i <= 3; i++)
        {
          pars.sym();
          pars.hex();
        }

      // Get into the first conflict
      pars.esym (syms::conflict);

      // There must be one stanza in the file for each conflict; otherwise
      // something has changed since the file was regenerated. So we go thru
      // the conflicts in the same order they are generated; see merge.cc
      // resolve_merge_conflicts.

      // resolve_merge_conflicts should not call us if there are any
      // conflicts for which we don't currently support resolutions; assert
      // that

      I(!result.missing_root_dir);
      I(result.invalid_name_conflicts.size() == 0);
      I(result.directory_loop_conflicts.size() == 0);
      I(result.orphaned_node_conflicts.size() == 0);
      I(result.multiple_name_conflicts.size() == 0);
      I(result.attribute_conflicts.size() == 0);

      // These are the ones we know how to resolve.

      parse_duplicate_name_conflicts(pars, result.duplicate_name_conflicts, left_roster, right_roster);
      parse_content_drop_conflicts(pars, result.content_drop_conflicts, left_roster, right_roster);
      parse_suture_drop_conflicts(pars, result.suture_drop_conflicts, left_roster, right_roster);
      parse_file_content_conflicts(pars, result.file_content_conflicts, left_roster, right_roster);

      if (src.lookahead != EOF)
        pars.err("extra conflicts in file");
    }
  else
    resolutions_given = false;

} // parse_resolve_conflicts_opts

static void
attach_node (lua_hooks & lua,
                        roster_t & new_roster,
                        node_id nid,
                        file_path const target_path)
{
  // Simplified from workspace::perform_rename in work.cc

  I(!target_path.empty());

  N(!new_roster.has_node(target_path), F("%s already exists") % target_path.as_external());
  N(new_roster.has_node(target_path.dirname()),
    F("directory %s does not exist or is unknown") % target_path.dirname());

  new_roster.attach_node (nid, target_path);

  node_t node = new_roster.get_node (nid);
  for (full_attr_map_t::const_iterator attr = node->attrs.begin();
       attr != node->attrs.end();
       ++attr)
    lua.hook_apply_attribute (attr->first(), target_path, attr->second.second());

} // attach_node

void
roster_merge_result::resolve_duplicate_name_conflicts(lua_hooks & lua,
                                                      node_id_source & nis,
                                                      roster_t const & left_roster,
                                                      roster_t const & right_roster,
                                                      content_merge_adaptor & adaptor)
{
  MM(left_roster);
  MM(right_roster);
  MM(this->roster); // New roster

  // Conflict nodes are present but detached (without filenames) in the new
  // roster. The resolution is either to suture the two files together, or to
  // rename one or both.

  for (std::vector<duplicate_name_conflict>::const_iterator i = duplicate_name_conflicts.begin();
       i != duplicate_name_conflicts.end();
       ++i)
    {
      duplicate_name_conflict const & conflict = *i;
      MM(conflict);

      node_id left_nid = conflict.left_nid;
      node_id right_nid= conflict.right_nid;

      file_path left_name, right_name;

      left_roster.get_name(left_nid, left_name);
      right_roster.get_name(right_nid, right_name);

      switch (conflict.left_resolution.first)
      {
      case resolve_conflicts::suture:
        {
          I(conflict.right_resolution.first == resolve_conflicts::suture);

          // There's no inherent reason suturing directories can't be
          // supported; we just haven't worked on it yet.
          N(!is_dir_t(left_roster.get_node (left_nid)), F("can't suture directory : %s") % left_name);

          P(F("suturing %s, %s into %s") % left_name % right_name % conflict.left_resolution.second);

          // Create a single new node, delete the two old ones, set ancestors.
          node_id new_nid;

          file_path const new_file_name = conflict.left_resolution.second;

          file_t const left_node = downcast_to_file_t(left_roster.get_node (left_nid));
          file_t const right_node = downcast_to_file_t(right_roster.get_node (right_nid));

          N(path::file == get_path_status(new_file_name),
            F("%s does not exist or is a directory") % new_file_name);

          file_id const & left_file_id = left_node->content;
          file_id const & right_file_id = right_node->content;
          file_id new_file_id;
          data new_raw_data;
          read_data (new_file_name, new_raw_data);
          file_data new_data (new_raw_data);
          file_data left_data, right_data;

          adaptor.get_version(left_file_id, left_data);
          adaptor.get_version(right_file_id, right_data);
          calculate_ident (new_data, new_file_id);

          new_nid = roster.create_file_node (new_file_id, nis, make_pair(left_nid, right_nid));

          adaptor.record_merge(left_file_id, right_file_id, new_file_id, left_data, right_data, new_data);

          attach_node (lua, roster, new_nid, new_file_name);

          roster.drop_detached_node(left_nid);
          roster.drop_detached_node(right_nid);
        }
        break;

      case resolve_conflicts::rename:
        P(F("renaming %s to %s") % left_name % conflict.left_resolution.second);
        attach_node (lua, this->roster, left_nid, conflict.left_resolution.second);
        break;

      case resolve_conflicts::none:
        N(false, F("no resolution provided for duplicate_name %s") % left_name);
        break;

      default:
        N(false, F("%s: invalid resolution for this conflict") % image (conflict.left_resolution.first));
      }

      switch (conflict.right_resolution.first)
        {
        case resolve_conflicts::suture:
          I(conflict.left_resolution.first == resolve_conflicts::suture);
          // suture already done in left above
          break;

        case resolve_conflicts::rename:
          P(F("renaming %s to %s") % right_name % conflict.right_resolution.second);
          attach_node (lua, this->roster, right_nid, conflict.right_resolution.second);
          break;

        case resolve_conflicts::none:
          // Just keep current name
          this->roster.attach_node (right_nid, right_name);
          break;

        default:
          N(false, F("%s: invalid resolution for this conflict") % image (conflict.right_resolution.first));
        }
    } // end for

  duplicate_name_conflicts.clear();
}

void
roster_merge_result::resolve_content_drop_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster)
{
  MM(left_roster);
  MM(right_roster);
  MM(this->roster); // New roster

  // Conflict node is present but unattached in the new roster, with null
  // file content id. The resolution is to fill in or delete the node.

  for (std::vector<content_drop_conflict>::const_iterator i = content_drop_conflicts.begin();
       i != content_drop_conflicts.end();
       ++i)
    {
      content_drop_conflict const & conflict = *i;
      MM(conflict);

      file_path name;
      node_t old_n;

      switch (conflict.parent_side)
        {
        case resolve_conflicts::left_side:
          left_roster.get_name(conflict.nid, name);
          old_n = left_roster.get_node (conflict.nid);
          break;

        case resolve_conflicts::right_side:
          right_roster.get_name(conflict.nid, name);
          old_n = right_roster.get_node (conflict.nid);
          break;
        }

      switch (conflict.resolution.first)
        {
          case resolve_conflicts::none:
            N(false, F("no resolution specified for conflict: content_drop %s") % name);
            break;

          case resolve_conflicts::suture:
            {
              // Verify that conflict.nid was sutured in this merge
              node_t new_n = roster.get_node(conflict.resolution.second);

              N((new_n->ancestors.first == conflict.nid || new_n->ancestors.second == conflict.nid),
                F("%s was not sutured to %s in this merge") % name % conflict.resolution.second);
            }
            break;

          case resolve_conflicts::ignore_drop:
            {
              file_path dirname;
              path_component basename;

              N(roster.has_node(conflict.nid),
                F("%s was sutured in this merge; resolution must be 'resolved_suture'") % name);

              node_t new_n = roster.get_node(conflict.nid);

              P(F("ignoring drop of %s; new name %s") % name % conflict.resolution.second);
              N(!roster.has_node(conflict.resolution.second),
                F("%s already exists") % conflict.resolution.second);

              name.dirname_basename(dirname, basename);

              node_t dir_n = roster.get_node(dirname);

              N(dir_n != 0, F("%s directory does not exist") % dirname);

              // fill in node in result roster
              new_n->attrs = old_n->attrs;
              I(is_file_t(new_n));
              downcast_to_file_t(new_n)->content = downcast_to_file_t(old_n)->content;
              I(!roster.is_attached(conflict.nid));
              roster.attach_node(conflict.nid, dir_n->self, basename);
            }
            break;

          case resolve_conflicts::respect_drop:
            P(F("keeping drop of %s") % old_n->name);
            I(!roster.is_attached(conflict.nid));
            roster.drop_detached_node(conflict.nid);
            break;

        default:
          I(false);
        }

    } // end for

  content_drop_conflicts.clear();
}

void
roster_merge_result::resolve_suture_drop_conflicts(roster_t const & left_roster,
                                                   roster_t const & right_roster)
{
  MM(left_roster);
  MM(right_roster);
  MM(this->roster); // New roster

  // Conflict node is present but unattached in the new roster, with null
  // file content id. The resolution is to fill in or delete the node.

  for (std::vector<suture_drop_conflict>::const_iterator i = suture_drop_conflicts.begin();
       i != suture_drop_conflicts.end();
       ++i)
    {
      suture_drop_conflict const & conflict = *i;
      MM(conflict);

      file_path name;
      node_t old_n;

      switch (conflict.sutured_side)
        {
        case resolve_conflicts::left_side:
          left_roster.get_name(conflict.sutured_nid, name);
          old_n = left_roster.get_node (conflict.sutured_nid);
          break;

        case resolve_conflicts::right_side:
          right_roster.get_name(conflict.sutured_nid, name);
          old_n = right_roster.get_node (conflict.sutured_nid);
          break;
        }

      switch (conflict.resolution.first)
        {
          case resolve_conflicts::none:
            N(false, F("no resolution specified for conflict: suture_drop %s") % name);
            break;

          case resolve_conflicts::ignore_drop:
            {
              file_path dirname;
              path_component basename;

              N(roster.has_node(conflict.sutured_nid),
                F("%s was sutured in this merge; resolution must be 'resolved_suture'") % name);

              node_t new_n = roster.get_node(conflict.sutured_nid);

              P(F("ignoring drop of %s; new name %s") % name % conflict.resolution.second);
              N(!roster.has_node(conflict.resolution.second),
                F("%s already exists") % conflict.resolution.second);

              name.dirname_basename(dirname, basename);

              node_t dir_n = roster.get_node(dirname);

              N(dir_n != 0, F("%s directory does not exist") % dirname);

              // fill in node in result roster
              new_n->attrs = old_n->attrs;
              I(is_file_t(new_n));
              downcast_to_file_t(new_n)->content = downcast_to_file_t(old_n)->content;
              I(!roster.is_attached(conflict.sutured_nid));
              roster.attach_node(conflict.sutured_nid, dir_n->self, basename);
            }
            break;

        default:
          I(false);
        }

    } // end for

  suture_drop_conflicts.clear();
}

void
roster_merge_result::resolve_file_content_conflicts(lua_hooks & lua,
                                                    roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor)
{
  MM(left_roster);
  MM(right_roster);
  MM(this->roster); // New roster

  // Conflict node is present and attached in the new roster, with a null
  // file content id. The resolution is to enter the user specified file
  // content in the database and roster, or let the internal line merger
  // handle it.

  for (std::vector<file_content_conflict>::const_iterator i = file_content_conflicts.begin();
       i != file_content_conflicts.end();
       ++i)
    {
      file_content_conflict const & conflict = *i;
      MM(conflict);

      file_path left_name, right_name;

      left_roster.get_name(conflict.left_nid, left_name);
      right_roster.get_name(conflict.right_nid, right_name);

      switch (conflict.resolution.first)
        {
          case resolve_conflicts::content_internal:
          case resolve_conflicts::none:
            {
              file_id merged_id;

              N(resolve_conflicts::do_auto_merge(lua, conflict, adaptor, left_roster,
                                                 right_roster, this->roster, merged_id),
                F("merge of %s, %s failed") % left_name % right_name);

              P(F("merged %s, %s") % left_name % right_name);

              file_t result_node = downcast_to_file_t(roster.get_node(conflict.result_nid));
              result_node->content = merged_id;
            }
            break;

          case resolve_conflicts::content_user:
            {
              P(F("replacing content of %s, %s with %s") % left_name % right_name % conflict.resolution.second);

              file_id result_id;
              file_data left_data, right_data, result_data;
              data result_raw_data;
              adaptor.get_version(conflict.left, left_data);
              adaptor.get_version(conflict.right, right_data);
              read_data(conflict.resolution.second, result_raw_data);
              result_data = file_data(result_raw_data);
              calculate_ident(result_data, result_id);

              file_t result_node = downcast_to_file_t(roster.get_node(conflict.result_nid));
              result_node->content = result_id;

              adaptor.record_merge(conflict.left, conflict.right, result_id,
                                   left_data, right_data, result_data);

            }
            break;

        default:
          I(false);
        }

    } // end for

  file_content_conflicts.clear();
}

void
roster_merge_result::clear()
{
  missing_root_dir = false;
  invalid_name_conflicts.clear();
  directory_loop_conflicts.clear();

  orphaned_node_conflicts.clear();
  multiple_name_conflicts.clear();
  duplicate_name_conflicts.clear();
  content_drop_conflicts.clear();
  suture_drop_conflicts.clear();

  attribute_conflicts.clear();
  file_content_conflicts.clear();

  roster = roster_t();
}

namespace
{
  // a wins if *(b) > a.  Which is to say that all members of b_marks are
  // ancestors of a.  But all members of b_marks are ancestors of the
  // _b_, so the previous statement is the same as saying that _no_
  // members of b_marks is an _uncommon_ ancestor of _b_.
  bool
  a_wins(set<revision_id> const & b_marks,
         set<revision_id> const & b_uncommon_ancestors)
  {
    for (set<revision_id>::const_iterator i = b_marks.begin();
         i != b_marks.end(); ++i)
      if (b_uncommon_ancestors.find(*i) != b_uncommon_ancestors.end())
        return false;
    return true;
  }

  // returns true if merge was successful ('result' is valid), false otherwise
  // ('conflict_descriptor' is valid).
  template <typename T, typename C> bool
  merge_scalar(T const & left,
               set<revision_id> const & left_marks,
               set<revision_id> const & left_uncommon_ancestors,
               T const & right,
               set<revision_id> const & right_marks,
               set<revision_id> const & right_uncommon_ancestors,
               T & result,
               resolve_conflicts::side_t & side,
               C & conflict_descriptor)
  {
    if (left == right)
      {
        result = left;
        side = resolve_conflicts::left_side;
        return true;
      }
    MM(left_marks);
    MM(left_uncommon_ancestors);
    MM(right_marks);
    MM(right_uncommon_ancestors);
    bool left_wins = a_wins(right_marks, right_uncommon_ancestors);
    bool right_wins = a_wins(left_marks, left_uncommon_ancestors);
    // two bools means 4 cases:

    //     this is ambiguous clean merge, which is theoretically impossible.
    I(!(left_wins && right_wins));

    if (left_wins && !right_wins)
      {
        result = left;
        side = resolve_conflicts::left_side;
        return true;
      }

    if (!left_wins && right_wins)
      {
        result = right;
        side = resolve_conflicts::right_side;
        return true;
      }

    if (!left_wins && !right_wins)
      {
        conflict_descriptor.left = left;
        conflict_descriptor.right = right;
        side = resolve_conflicts::left_side;
        return false;
      }
    I(false);
  }

  inline void
  create_node_for(node_t const & n, roster_t & new_roster)
  {
    if (is_dir_t(n))
      new_roster.create_dir_node(n->self);
    else if (is_file_t(n))
      new_roster.create_file_node(file_id(), n->self);
    else
      I(false);
  }

  inline void
  create_node_for(node_t const & n, std::pair<node_id, node_id> const ancestors, roster_t & new_roster)
  {
    if (is_dir_t(n))
      I(false);
    else if (is_file_t(n))
      return new_roster.create_file_node(file_id(), n->self, ancestors);
    else
      I(false);
  }

  void
  find_common_ancestor_nodes(std::map<node_id, revision_id> const & birth_parents,
                             marking_map const & markings,
                             set<revision_id> const & uncommon_ancestors,
                             set<node_id> & result)
  {
    for (std::map<node_id, revision_id>::const_iterator i = birth_parents.begin();
         i != birth_parents.end();
         i++)
      {
        if (uncommon_ancestors.find(i->second) == uncommon_ancestors.end())
          {
            result.insert(i->first);
          }
      }
  }

  bool
  is_in(set<revision_id> query, set<revision_id> target)
  {
    for (set<revision_id>::const_iterator i = query.begin(); i != query.end(); i++)
      {
        if (target.find(*i) != target.end())
          return true;
      }
    return false;
  }

  bool
  is_in(std::map<attr_key, std::set<revision_id> > query, set<revision_id> target)
  {
    for (std::map<attr_key, std::set<revision_id> >::const_iterator i = query.begin(); i != query.end(); i++)
      {
        if (is_in(i->second, target))
          return true;
      }
    return false;
  }

  void
  check_scalars_modified(node_id sutured_node,
                         resolve_conflicts::side_t sutured_side,
                         set<node_id> const & common_parents,
                         marking_map const & other_markings,
                         set<revision_id> const & other_uncommon_ancestors,
                         roster_merge_result & result)
  {
    // A scalar is modified if its markings contain a revision in
    // other_uncommon_ancestors.
    set<node_id> conflict_nodes;

    for (set<node_id>::const_iterator i = common_parents.begin(); i != common_parents.end(); i++)
      {
        marking_t const marking = safe_get(other_markings, *i);

        if (is_in(marking.parent_name, other_uncommon_ancestors) ||
            is_in(marking.file_content, other_uncommon_ancestors) ||
            is_in(marking.attrs, other_uncommon_ancestors))
          {
            conflict_nodes.insert(*i);
          }
      }

    if (conflict_nodes.size() > 0)
      result.suture_scalar_conflicts.push_back
        (suture_scalar_conflict(sutured_node, sutured_side, common_parents, conflict_nodes));
  }

  bool
  operator==(std::map<node_id, revision_id> left, std::set<node_id> right)
  {
    std::set<node_id>::const_iterator r = right.begin();
    for (std::map<node_id, revision_id>::const_iterator l = left.begin(); l != left.end(); l++)
      {
        if (l->first != *r)
          return false;

        r++;
    }
    return true;
  }

  void
  insert_sutured(node_t const & n,
                 marking_t::birth_record_t const & birth_record,
                 marking_map const & parent_markings,
                 set<revision_id> const & uncommon_ancestors,
                 roster_t const & other_parent_roster,
                 marking_map const & other_markings,
                 set<revision_id> const & other_uncommon_ancestors,
                 resolve_conflicts::side_t parent_side,
                 node_id_source & nis,
                 roster_merge_result & result,
                 set<node_id> & already_handled)
  {
    set<node_id> common_parents;
    set<node_id> unfound_parents;
    set<node_id> conflict_nodes;
    set<node_id> extra_parents;

    MM(common_parents);
    MM(unfound_parents);
    MM(conflict_nodes);
    MM(extra_parents);

    bool partial_suture = false; // other_parent has sutured node with parents = subset of common_parents
    bool extra_suture   = false; // other_parent has sutured node with some parents in, some not in common_parents

    find_common_ancestor_nodes(birth_record.parents, parent_markings, uncommon_ancestors, common_parents);

    unfound_parents = common_parents;

    if (common_parents.size() == 1)
      {
        // exactly one common parent; case ib, ic, id
        if (!other_parent_roster.has_node(*common_parents.begin()))
          {
            // deleted; case ib
            result.suture_drop_conflicts.push_back(suture_drop_conflict(n->self, parent_side, common_parents));
          }

        already_handled.insert(*common_parents.begin());

        // Let mark-merge handle the rest. Set ancestors so mark-merge step
        // knows what to merge; it will also null the ancestors afterwards.
        switch (parent_side)
          {
          case resolve_conflicts::left_side:
            create_node_for(n, make_pair (n->self, *common_parents.begin()), result.roster);
            return;

          case resolve_conflicts::right_side:
            create_node_for(n, make_pair (*common_parents.begin(), n->self), result.roster);
            return;
          }
      }

    for (node_map::const_iterator i = other_parent_roster.all_nodes().begin();
         i != other_parent_roster.all_nodes().end() &&
           !unfound_parents.empty();
         i++)
      {
        marking_t::birth_record_t const & this_birth = safe_get(other_markings, i->first).birth_record;

        switch (this_birth.cause)
          {
          case marking_t::add:
            {
              set<node_id>::iterator pi = unfound_parents.find(i->first);

              if (pi != unfound_parents.end())
                {
                  unfound_parents.erase(pi);
                }
            }
            break;

          case marking_t::split:
            I(false); // FIXME_SPLIT: not supported yet
            break;

          case marking_t::suture:
            if (this_birth.parents == common_parents)
              {
                // case ie

                switch (parent_side)
                  {
                  case resolve_conflicts::left_side:
                    result.roster.create_file_node (file_id(), nis, make_pair(n->self, i->first));
                    break;

                  case resolve_conflicts::right_side:
                    result.roster.create_file_node (file_id(), nis, make_pair(i->first, n->self));
                    break;
                  }

                // We've now handled this node:
                already_handled.insert(i->first);

                // If the parent nodes have been sutured, we won't see them
                // later. If not, we will. So we add them to
                // already_handled, and delete suture parents from
                // already_handled when we encounter a suture node.
                //
                // But both of those cases are here. So if the parents are
                // in already_handled, delete them.
                for (std::map<node_id, revision_id>::const_iterator j = this_birth.parents.begin();
                     j != this_birth.parents.end();
                     j++)
                  {
                    if (already_handled.find(j->first) == already_handled.end())
                      already_handled.insert(j->first);
                    else
                      already_handled.erase(j->first);
                  }

                return;
              }
            else
              {
                conflict_nodes.insert(i->first);

                for (std::map<node_id, revision_id>::const_iterator j = this_birth.parents.begin();
                     j != this_birth.parents.end();
                     j++)
                  {
                    std::set<node_id>::iterator found = unfound_parents.find(j->first);
                    if (found == unfound_parents.end())
                      extra_parents.insert(j->first);
                    else
                      unfound_parents.erase(found);
                  }
              }
          }; // switch this_birth.cause
      }; // for all nodes in other_parent

    if (unfound_parents.empty())
      {
        if (conflict_nodes.size() > 0)
          {
            result.suture_suture_conflicts.push_back
              (suture_suture_conflict(n->self, parent_side, common_parents, conflict_nodes, extra_parents));
            create_node_for(n, result.roster);
          }
        else
          {
            create_node_for(n, result.roster);

            for (set<node_id>::const_iterator i = common_parents.begin(); i != common_parents.end(); i++)
              {
                already_handled.insert(*i);
              }

            check_scalars_modified
              (n->self, parent_side, common_parents, other_markings, other_uncommon_ancestors, result);
          }
      }
    else
      {
        result.suture_drop_conflicts.push_back (suture_drop_conflict(n->self, parent_side, unfound_parents));
        create_node_for(n, result.roster);
      }
  }

  inline void
  insert_if_unborn_or_sutured(node_t const & n,
                              roster_t const & parent_roster,
                              marking_map const & parent_markings,
                              set<revision_id> const & uncommon_ancestors,
                              roster_t const & other_parent_roster,
                              marking_map const & other_parent_markings,
                              set<revision_id> const & other_uncommon_ancestors,
                              resolve_conflicts::side_t parent_side, // n is in parent_side roster
                              node_id_source & nis,
                              roster_merge_result & result,
                              set<node_id> & already_handled)
  {
    MM(parent_markings);
    MM(uncommon_ancestors);
    // See ss-existence-merge.text for cases. n is either the left or
    // right parent node.

    // First we see if we've already handled this node.
    {
      set<node_id>::iterator i = already_handled.find(n->self);
      if (i != already_handled.end())
        {
          already_handled.erase(i);
          return;
        }
    }

    // We are in case i, iii or iv. We determine which by searching for the
    // birth revision of node n in uncommon_ancestors.

    revision_id const & birth = safe_get(parent_markings, n->self).birth_revision;

    set<revision_id>::const_iterator const uncommon_birth_i = uncommon_ancestors.find(birth);

    if (uncommon_birth_i != uncommon_ancestors.end())
      {
        // case i
        marking_t::birth_record_t const & birth_record = safe_get(parent_markings, n->self).birth_record;

        switch (birth_record.cause)
          {
          case marking_t::add:
            // case ia
            create_node_for(n, result.roster);
            break;

          case marking_t::split:
            I(false); // not supported yet
            break;

          case marking_t::suture:
            // case ib, ic, id, ie; check state of suture parents
            insert_sutured (n, birth_record, parent_markings, uncommon_ancestors,
                            other_parent_roster, other_parent_markings, other_uncommon_ancestors,
                            parent_side, nis, result, already_handled);
            break;
          }
      }
    else
      {
        // case iii or iv

        // FIXME: iii?

        // case iva or ivb

        // FIXME: consider other scalars conflicting with drop

        set<revision_id> const & content_marks = safe_get(parent_markings, n->self).file_content;
        for (set<revision_id>::const_iterator it = content_marks.begin(); it != content_marks.end(); it++)
          {
            if (uncommon_ancestors.find(*it) != uncommon_ancestors.end())
              {
                // case ivb
                result.content_drop_conflicts.push_back
                  (content_drop_conflict(n->self,
                                         downcast_to_file_t(parent_roster.get_node(n->self))->content,
                                         parent_side));
                create_node_for(n, result.roster);
                break;
              }
          }
      }
  }

  bool
  would_make_dir_loop(roster_t const & r, node_id nid, node_id parent)
  {
    // parent may not be fully attached yet; that's okay.  that just means
    // we'll run into a node with a null parent somewhere before we hit the
    // actual root; whether we hit the actual root or not, hitting a node
    // with a null parent will tell us that this particular attachment won't
    // create a loop.
    for (node_id curr = parent; !null_node(curr); curr = r.get_node(curr)->parent)
      {
        if (curr == nid)
          return true;
      }
    return false;
  }

  void
  assign_name(roster_merge_result & result,
              node_id nid,
              node_id parent,
              path_component name,
              resolve_conflicts::side_t side,
              node_id parent_nid)
  {
    // side indicates parent roster containing parent_nid. Note that nid is
    // in the child roster, and may be different from parent_nid for an
    // automatic suture

    // this function is reponsible for detecting structural conflicts.  by the
    // time we've gotten here, we have a node that's unambiguously decided on
    // a name; but it might be that that name does not exist (because the
    // parent dir is gone), or that it's already taken (by another node), or
    // that putting this node there would create a directory loop.  In all
    // such cases, rather than actually attach the node, we write a conflict
    // structure and leave it detached.

    // the root dir is somewhat special.  it can't be orphaned, and it can't
    // make a dir loop.  it can, however, have a name collision.
    if (null_node(parent))
      {
        I(name.empty());
        if (result.roster.has_root())
          {
            // see comments below about name collisions.
            duplicate_name_conflict c;
            // some other node has already been attached at the root location
            // so write a conflict structure with this node on the indicated
            // side of the merge and the attached node on the other side of
            // the merge. detach the previously attached node and leave both
            // conflicted nodes detached.
            switch (side)
              {
              case resolve_conflicts::left_side:
                c.left_nid = parent_nid;
                c.right_nid = result.roster.root()->self;
                break;
              case resolve_conflicts::right_side:
                c.left_nid = result.roster.root()->self;
                c.right_nid = parent_nid;
                break;
              }
            c.parent_name = make_pair(parent, name);
            result.roster.detach_node(file_path());
            result.duplicate_name_conflicts.push_back(c);
            return;
          }
      }
    else
      {
        // orphan:
        if (!result.roster.has_node(parent))
          {
            orphaned_node_conflict c;
            c.nid = parent_nid;
            c.parent_name = make_pair(parent, name);
            result.orphaned_node_conflicts.push_back(c);
            return;
          }

        dir_t p = downcast_to_dir_t(result.roster.get_node(parent));

        // duplicate name conflict:
        // see the comment in roster_merge.hh for the analysis showing that at
        // most two nodes can participate in a duplicate name conflict.  this code
        // exploits that; after this code runs, there will be no node at the given
        // location in the tree, which means that in principle, if there were a
        // third node that _also_ wanted to go here, when we got around to
        // attaching it we'd have no way to realize it should be a conflict.  but
        // that never happens, so we don't have to keep a lookaside set of
        // "poisoned locations" or anything.
        if (p->has_child(name))
          {
            duplicate_name_conflict c;
            // some other node has already been attached at the named location
            // so write a conflict structure with this node on the indicated
            // side of the merge and the attached node on the other side of
            // the merge. detach the previously attached node and leave both
            // conflicted nodes detached.
            switch (side)
              {
              case resolve_conflicts::left_side:
                c.left_nid = parent_nid;
                c.right_nid = p->get_child(name)->self;
                break;
              case resolve_conflicts::right_side:
                c.left_nid = p->get_child(name)->self;
                c.right_nid = parent_nid;
                break;
              }
            c.parent_name = make_pair(parent, name);
            p->detach_child(name);
            result.duplicate_name_conflicts.push_back(c);
            return;
          }

        if (would_make_dir_loop(result.roster, nid, parent))
          {
            directory_loop_conflict c;
            c.nid = parent_nid;
            c.parent_name = make_pair(parent, name);
            result.directory_loop_conflicts.push_back(c);
            return;
          }
      }
    // hey, we actually made it.  attach the node!
    result.roster.attach_node(nid, parent, name);
  }

  void
  copy_node_forward(roster_merge_result & result, node_t const & n,
                    node_t const & old_n, resolve_conflicts::side_t const & side)
  {
    I(n->self == old_n->self);
    n->attrs = old_n->attrs;
    if (is_file_t(n))
      downcast_to_file_t(n)->content = downcast_to_file_t(old_n)->content;
    assign_name(result, n->self, old_n->parent, old_n->name, side, n->self);
  }

  void
  merge_nodes(node_t const left_n,
              marking_t const & left_marking,
              set<revision_id> const & left_uncommon_ancestors,
              node_t const right_n,
              marking_t const & right_marking,
              set<revision_id> const & right_uncommon_ancestors,
              node_t const new_n,
              roster_merge_result & result)
    {
      // merge name
      pair<node_id, path_component> left_name, right_name, new_name;
      multiple_name_conflict conflict(new_n->self);
      left_name = make_pair(left_n->parent, left_n->name);
      right_name = make_pair(right_n->parent, right_n->name);
      resolve_conflicts::side_t side; // the side new_n is copied from
      if (merge_scalar(left_name,
                       left_marking.parent_name,
                       left_uncommon_ancestors,
                       right_name,
                       right_marking.parent_name,
                       right_uncommon_ancestors,
                       new_name, side, conflict))
        {
          switch (side)
            {
            case resolve_conflicts::left_side:
              assign_name(result, new_n->self,
                          new_name.first, new_name.second, side, left_n->self);
              break;

            case resolve_conflicts::right_side:
              assign_name(result, new_n->self,
                          new_name.first, new_name.second, side, right_n->self);
              break;
            }
        }
      else
        {
          // unsuccessful merge; leave node detached and save
          // conflict object
          result.multiple_name_conflicts.push_back(conflict);
        }
      // if a file, merge content
      if (is_file_t(new_n))
        {
          file_content_conflict conflict(left_n->self, right_n->self, new_n->self);
          if (merge_scalar(downcast_to_file_t(left_n)->content,
                           left_marking.file_content,
                           left_uncommon_ancestors,
                           downcast_to_file_t(right_n)->content,
                           right_marking.file_content,
                           right_uncommon_ancestors,
                           downcast_to_file_t(new_n)->content,
                           side, conflict))
            {
              // successful merge
            }
          else
            {
              downcast_to_file_t(new_n)->content = file_id();
              result.file_content_conflicts.push_back(conflict);
            }
        }
      // merge attributes
      {
        full_attr_map_t::const_iterator left_ai = left_n->attrs.begin();
        full_attr_map_t::const_iterator right_ai = right_n->attrs.begin();
        parallel::iter<full_attr_map_t> attr_i(left_n->attrs,
                                               right_n->attrs);
        while(attr_i.next())
          {
            switch (attr_i.state())
              {
              case parallel::invalid:
                I(false);
              case parallel::in_left:
                safe_insert(new_n->attrs, attr_i.left_value());
                break;
              case parallel::in_right:
                safe_insert(new_n->attrs, attr_i.right_value());
                break;
              case parallel::in_both:
                pair<bool, attr_value> new_value;
                attribute_conflict conflict(new_n->self);
                conflict.key = attr_i.left_key();
                I(conflict.key == attr_i.right_key());
                if (merge_scalar(attr_i.left_data(),
                                 safe_get(left_marking.attrs,
                                          attr_i.left_key()),
                                 left_uncommon_ancestors,
                                 attr_i.right_data(),
                                 safe_get(right_marking.attrs,
                                          attr_i.right_key()),
                                 right_uncommon_ancestors,
                                 new_value,
                                 side, conflict))
                  {
                    // successful merge
                    safe_insert(new_n->attrs,
                                make_pair(attr_i.left_key(),
                                          new_value));
                  }
                else
                  {
                    // unsuccessful merge
                    // leave out the attr entry entirely, and save the
                    // conflict
                    result.attribute_conflicts.push_back(conflict);
                  }
                break;
              }
          }
      }
    }
} // end anonymous namespace

void
roster_merge(roster_t const & left_parent,
             marking_map const & left_markings,
             set<revision_id> const & left_uncommon_ancestors,
             roster_t const & right_parent,
             marking_map const & right_markings,
             set<revision_id> const & right_uncommon_ancestors,
             node_id_source & nis,
             roster_merge_result & result)
{
  set<node_id> already_handled;
  MM (already_handled);

  L(FL("Performing a roster_merge"));

  result.clear();
  MM(left_parent);
  MM(left_markings);
  MM(left_uncommon_ancestors);
  MM(right_parent);
  MM(right_markings);
  MM(right_uncommon_ancestors);
  MM(result);

  // First handle existence merge (lifecycles). See ss-existence-merge.text.
  {
    // Iterate in reverse order so we see sutured nodes before the
    // corresponding non-sutured node; see ss-existence-merge.text.
    parallel::reverse_iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            // case ii, iii, iva, va, vc
            insert_if_unborn_or_sutured(i.left_data(),
                                        left_parent, left_markings, left_uncommon_ancestors,
                                        right_parent, right_markings, right_uncommon_ancestors,
                                        resolve_conflicts::left_side, nis, result, already_handled);
            break;

          case parallel::in_right:
            // case ii, iii, ivb, vb, vd
            insert_if_unborn_or_sutured(i.right_data(),
                                        right_parent, right_markings, right_uncommon_ancestors,
                                        left_parent, left_markings, left_uncommon_ancestors,
                                        resolve_conflicts::right_side, nis, result, already_handled);
            break;

          case parallel::in_both:
            create_node_for(i.left_data(), result.roster);
            break;
          }
      }
  }

  // okay, our roster now contains a bunch of empty, detached nodes.  fill
  // them in one at a time with *-merge.
  {
    node_map::const_iterator left_i, right_i;
    parallel::iter<node_map> i(left_parent.all_nodes(), right_parent.all_nodes());
    node_map::const_iterator new_i = result.roster.all_nodes().begin();
    marking_map::const_iterator left_mi = left_markings.begin();
    marking_map::const_iterator right_mi = right_markings.begin();
    while (i.next())
      {
        switch (i.state())
          {
          case parallel::invalid:
            I(false);

          case parallel::in_left:
            {
              node_t const left_n = i.left_data();
              // We skip nodes that aren't in the result roster (were
              // deleted in the existence step above).

              if (result.roster.has_node(left_n->self))
                {
                  node_t result_n = result.roster.get_node(left_n->self);

                  if (result_n->ancestors.second != the_null_node)
                    {
                      // This node was sutured in left_uncommon, and its right parent
                      // exists in right; merge with it.
                      I(new_i->first == result_n->self);
                      node_t right_n = right_parent.get_node(result_n->ancestors.second);
                      marking_map::const_iterator right_mi = right_markings.find(right_n->self);

                      // check that iterators are in sync.
                      I(new_i->first == i.left_key());
                      I(left_mi->first == i.left_key());

                      merge_nodes(left_n,
                                  left_mi->second, // left_marking
                                  left_uncommon_ancestors,
                                  right_n,
                                  right_mi->second,
                                  right_uncommon_ancestors,
                                  new_i->second,   // new_n
                                  result);

                      // Not a new suture, so set ancestors to null.
                      new_i->second->ancestors = null_ancestors;

                      ++new_i;
                    }
                  else
                    {
                      // Not sutured.
                      //
                      // Attach this node from the left roster. This may cause
                      // a name collision with a previously attached node from
                      // the other side of the merge.
                      I(new_i->first == result_n->self);
                      copy_node_forward(result, new_i->second, left_n, resolve_conflicts::left_side);
                      ++new_i;
                    }
                }
              ++left_mi;
              break;
            }

          case parallel::in_right:
            {
              node_t const & right_n = i.right_data();
              // We skip nodes that aren't in the result roster, unless they are
              // parents of a suture.

              if (result.roster.has_node(right_n->self))
                {
                  node_t result_n = result.roster.get_node(right_n->self);

                  if (result_n->ancestors.second != the_null_node)
                    {
                      // This node was sutured in right_uncommon, and its left parent
                      // exists in left; merge with it.
                      node_t left_n = left_parent.get_node(result_n->ancestors.first);
                      marking_map::const_iterator left_mi = left_markings.find(left_n->self);

                      // check that iterators are in sync.
                      I(new_i->first == i.right_key());
                      I(right_mi->first == i.right_key());

                      merge_nodes(left_n,
                                  left_mi->second,  // left_marking
                                  left_uncommon_ancestors,
                                  i.right_data(),   // right_n
                                  right_mi->second, // right_marking
                                  right_uncommon_ancestors,
                                  new_i->second,   // new_n
                                  result);

                      // Not a new suture, so set ancestors to null.
                      new_i->second->ancestors = null_ancestors;

                      ++new_i;
                    }
                  else
                    {
                      // Not sutured.
                      //
                      // Attach this node from the right roster. This may
                      // cause a name collision with a previously attached
                      // node from the other side of the merge.
                      copy_node_forward(result, new_i->second, right_n, resolve_conflicts::right_side);
                      ++new_i;
                    }
                }
              ++right_mi;
              break;
            }

          case parallel::in_both:
            {
              I(new_i->first == i.left_key());
              I(left_mi->first == i.left_key());
              I(right_mi->first == i.right_key());

              merge_nodes(i.left_data(),    // left_n
                          left_mi->second,  // left_marking
                          left_uncommon_ancestors,
                          i.right_data(),   // right_n
                          right_mi->second, // right_marking
                          right_uncommon_ancestors,
                          new_i->second,    // new_n
                          result);
            }
            ++left_mi;
            ++right_mi;
            ++new_i;
            break;
          }
      }
    // FIXME: failing
    // I(already_handled.size() == 0);

    I(left_mi == left_markings.end());
    I(right_mi == right_markings.end());

    // If we automatically sutured some nodes in the existence phase, handle
    // them now.
    for (; new_i != result.roster.all_nodes().end(); new_i++)
      {
        I(temp_node(new_i->first));

        node_t            result_n      = new_i->second;
        node_t            left_n        = left_parent.get_node(result_n->ancestors.first);
        marking_t const & left_marking  = safe_get(left_markings, left_n->self);
        node_t            right_n       = right_parent.get_node(result_n->ancestors.second);
        marking_t const & right_marking = safe_get(right_markings, right_n->self);

        merge_nodes(left_n,
                    left_marking,
                    left_uncommon_ancestors,
                    right_n,
                    right_marking,
                    right_uncommon_ancestors,
                    result_n,
                    result);
      }
  }

  // now check for the possible global problems
  if (!result.roster.has_root())
    result.missing_root_dir = true;
  else
    {
      // we can't have an illegal _MTN dir unless we have a root node in the
      // first place...
      dir_t result_root = result.roster.root();

      if (result_root->has_child(bookkeeping_root_component))
        {
          invalid_name_conflict conflict;
          node_t n = result_root->get_child(bookkeeping_root_component);
          conflict.nid = n->self;
          conflict.parent_name.first = n->parent;
          conflict.parent_name.second = n->name;
          I(n->name == bookkeeping_root_component);

          result.roster.detach_node(n->self);
          result.invalid_name_conflicts.push_back(conflict);
        }
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "constants.hh"
#include "roster_delta.hh"

// cases for testing:
//
// (DONE:)
//
// lifecycle, file and dir
//    alive in both
//    alive in one and unborn in other (left vs. right)
//    alive in one and dead in other (left vs. right)
//
// mark merge:
//   same in both, same mark
//   same in both, diff marks
//   different, left wins with 1 mark
//   different, right wins with 1 mark
//   different, conflict with 1 mark
//   different, left wins with 2 marks
//   different, right wins with 2 marks
//   different, conflict with 1 mark winning, 1 mark losing
//   different, conflict with 2 marks both conflicting
//
// for:
//   node name and parent, file and dir
//   node attr, file and dir
//   file content
//
// attr lifecycle:
//   seen in both -->mark merge cases, above
//   live in one and unseen in other -->live
//   dead in one and unseen in other -->dead
//
// two diff nodes with same name
// directory loops
// orphans
// illegal node ("_MTN")
// missing root dir
//
// (NEEDED:)
//
// interactions:
//   in-node name conflict prevents other problems:
//     in-node name conflict + possible between-node name conflict
//        a vs. b, plus a, b, exist in result
//        left: 1: a
//              2: b
//        right: 1: b
//               3: a
//     in-node name conflict + both possible names orphaned
//        a/foo vs. b/foo conflict, + a, b exist in parents but deleted in
//        children
//        left: 1: a
//              2: a/foo
//        right:
//              3: b
//              2: b/foo
//     in-node name conflict + directory loop conflict
//        a/bottom vs. b/bottom, with a and b both moved inside it
//     in-node name conflict + one name illegal
//        _MTN vs. foo
//   in-node name conflict causes other problems:
//     in-node name conflict + causes missing root dir
//        "" vs. foo and bar vs. ""
//   between-node name conflict prevents other problems:
//     between-node name conflict + both nodes orphaned
//        this is not possible
//     between-node name conflict + both nodes cause loop
//        this is not possible
//     between-node name conflict + both nodes illegal
//        two nodes that both merge to _MTN
//        this is not possible
//   between-node name conflict causes other problems:
//     between-node name conflict + causes missing root dir
//        two nodes that both want ""

typedef enum { scalar_a, scalar_b, scalar_conflict } scalar_val;

template <> void
dump(scalar_val const & v, string & out)
{
  switch (v)
    {
    case scalar_a:
      out = "scalar_a";
      break;
    case scalar_b:
      out = "scalar_b";
      break;
    case scalar_conflict:
      out = "scalar_conflict";
      break;
    }
}

void string_to_set(string const & from, set<revision_id> & to)
{
  to.clear();
  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      char label = (*i - '0') << 4 + (*i - '0');
      to.insert(revision_id(string(constants::idlen_bytes, label)));
    }
}


template <typename S> void
test_a_scalar_merge_impl(scalar_val left_val, string const & left_marks_str,
                         string const & left_uncommon_str,
                         scalar_val right_val, string const & right_marks_str,
                         string const & right_uncommon_str,
                         scalar_val expected_outcome)
{
  MM(left_val);
  MM(left_marks_str);
  MM(left_uncommon_str);
  MM(right_val);
  MM(right_marks_str);
  MM(right_uncommon_str);
  MM(expected_outcome);

  S scalar;
  roster_t left_parent, right_parent;
  marking_map left_markings, right_markings;
  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  roster_merge_result result;

  set<revision_id> left_marks, right_marks;

  MM(left_parent);
  MM(right_parent);
  MM(left_markings);
  MM(right_markings);
  MM(left_uncommon_ancestors);
  MM(right_uncommon_ancestors);
  MM(left_marks);
  MM(right_marks);
  MM(result);

  string_to_set(left_marks_str, left_marks);
  scalar.setup_parent(left_val, left_marks, left_parent, left_markings);
  string_to_set(right_marks_str, right_marks);
  scalar.setup_parent(right_val, right_marks, right_parent, right_markings);

  string_to_set(left_uncommon_str, left_uncommon_ancestors);
  string_to_set(right_uncommon_str, right_uncommon_ancestors);

  temp_node_id_source nis;
  roster_merge(left_parent, left_markings, left_uncommon_ancestors,
               right_parent, right_markings, right_uncommon_ancestors,
               nis, result);

  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(left_parent, left_markings, right_parent, right_markings);

  scalar.check_result(left_val, right_val, result, expected_outcome);
}

static const revision_id root_rid(string(constants::idlen_bytes, '\0'));
static const file_id arbitrary_file(string(constants::idlen_bytes, '\0'));

struct base_scalar
{
  testing_node_id_source nis;
  node_id root_nid;
  node_id thing_nid;
  base_scalar() : root_nid(nis.next()), thing_nid(nis.next())
  {}

  void
  make_dir(char const * name, node_id nid, roster_t & r, marking_map & markings)
  {
    r.create_dir_node(nid, null_ancestors);
    r.attach_node(nid, file_path_internal(name));
    marking_t marking;
    marking.birth_revision = root_rid;
    marking.parent_name.insert(root_rid);
    safe_insert(markings, make_pair(nid, marking));
  }

  void
  make_file(char const * name, node_id nid, roster_t & r, marking_map & markings)
  {
    r.create_file_node(arbitrary_file, nid, null_ancestors);
    r.attach_node(nid, file_path_internal(name));
    marking_t marking;
    marking.birth_revision = root_rid;
    marking.parent_name.insert(root_rid);
    marking.file_content.insert(root_rid);
    safe_insert(markings, make_pair(nid, marking));
  }

  void
  make_root(roster_t & r, marking_map & markings)
  {
    make_dir("", root_nid, r, markings);
  }
};

struct file_scalar : public virtual base_scalar
{
  file_path thing_name;
  file_scalar() : thing_name(file_path_internal("thing"))
  {}

  void
  make_thing(roster_t & r, marking_map & markings)
  {
    make_root(r, markings);
    make_file("thing", thing_nid, r, markings);
  }
};

struct dir_scalar : public virtual base_scalar
{
  file_path thing_name;
  dir_scalar() : thing_name(file_path_internal("thing"))
  {}

  void
  make_thing(roster_t & r, marking_map & markings)
  {
    make_root(r, markings);
    make_dir("thing", thing_nid, r, markings);
  }
};

struct name_shared_stuff : public virtual base_scalar
{
  virtual file_path path_for(scalar_val val) = 0;
  path_component pc_for(scalar_val val)
  {
    return path_for(val).basename();
  }
  virtual node_id parent_for(scalar_val val) = 0;

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        {
          file_path fp;
          result.roster.get_name(thing_nid, fp);
          I(fp == path_for(expected_val));
        }
        break;
      case scalar_conflict:
        multiple_name_conflict const & c = idx(result.multiple_name_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.left == make_pair(parent_for(left_val), pc_for(left_val)));
        I(c.right == make_pair(parent_for(right_val), pc_for(right_val)));
        I(null_node(result.roster.get_node(thing_nid)->parent));
        I(result.roster.get_node(thing_nid)->name.empty());
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        // attach implicitly checks that we were already detached
        result.roster.attach_node(thing_nid, file_path_internal("thing"));
        result.multiple_name_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }

  virtual ~name_shared_stuff() {};
};

template <typename T>
struct basename_scalar : public name_shared_stuff, public T
{
  virtual file_path path_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_path_internal((val == scalar_a) ? "a" : "b");
  }
  virtual node_id parent_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return root_nid;
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    r.detach_node(this->T::thing_name);
    r.attach_node(thing_nid, path_for(val));
    markings.find(thing_nid)->second.parent_name = marks;
  }

  virtual ~basename_scalar() {}
};

template <typename T>
struct parent_scalar : public virtual name_shared_stuff, public T
{
  node_id a_dir_nid, b_dir_nid;
  parent_scalar() : a_dir_nid(nis.next()), b_dir_nid(nis.next())
  {}

  virtual file_path path_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_path_internal((val == scalar_a) ? "a/thing" : "b/thing");
  }
  virtual node_id parent_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return ((val == scalar_a) ? a_dir_nid : b_dir_nid);
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    make_dir("a", a_dir_nid, r, markings);
    make_dir("b", b_dir_nid, r, markings);
    r.detach_node(this->T::thing_name);
    r.attach_node(thing_nid, path_for(val));
    markings.find(thing_nid)->second.parent_name = marks;
  }

  virtual ~parent_scalar() {}
};

template <typename T>
struct attr_scalar : public virtual base_scalar, public T
{
  attr_value attr_value_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return attr_value((val == scalar_a) ? "a" : "b");
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    this->T::make_thing(r, markings);
    r.set_attr(this->T::thing_name, attr_key("test_key"), attr_value_for(val));
    markings.find(thing_nid)->second.attrs[attr_key("test_key")] = marks;
  }

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        I(result.roster.get_node(thing_nid)->attrs[attr_key("test_key")]
          == make_pair(true, attr_value_for(expected_val)));
        break;
      case scalar_conflict:
        attribute_conflict const & c = idx(result.attribute_conflicts, 0);
        I(c.nid == thing_nid);
        I(c.key == attr_key("test_key"));
        I(c.left == make_pair(true, attr_value_for(left_val)));
        I(c.right == make_pair(true, attr_value_for(right_val)));
        full_attr_map_t const & attrs = result.roster.get_node(thing_nid)->attrs;
        I(attrs.find(attr_key("test_key")) == attrs.end());
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        result.roster.set_attr(this->T::thing_name, attr_key("test_key"),
                               attr_value("conflict -- RESOLVED"));
        result.attribute_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }
};

struct file_content_scalar : public virtual file_scalar
{
  file_id content_for(scalar_val val)
  {
    I(val != scalar_conflict);
    return file_id(string(constants::idlen_bytes,
                          (val == scalar_a) ? '\xaa' : '\xbb'));
  }

  void
  setup_parent(scalar_val val, set<revision_id> marks,
               roster_t & r, marking_map & markings)
  {
    make_thing(r, markings);
    downcast_to_file_t(r.get_node(thing_name))->content = content_for(val);
    markings.find(thing_nid)->second.file_content = marks;
  }

  void
  check_result(scalar_val left_val, scalar_val right_val,
               // NB result is writeable -- we can scribble on it
               roster_merge_result & result, scalar_val expected_val)
  {
    switch (expected_val)
      {
      case scalar_a: case scalar_b:
        I(downcast_to_file_t(result.roster.get_node(thing_nid))->content
          == content_for(expected_val));
        break;
      case scalar_conflict:
        file_content_conflict const & c = idx(result.file_content_conflicts, 0);
        I(c.left_nid == thing_nid);
        I(c.right_nid == thing_nid);
        I(c.result_nid == thing_nid);
        I(c.left == content_for(left_val));
        I(c.right == content_for(right_val));
        file_id & content = downcast_to_file_t(result.roster.get_node(thing_nid))->content;
        I(null_id(content));
        // resolve the conflict, thus making sure that resolution works and
        // that this was the only conflict signaled
        content = file_id(string(constants::idlen_bytes, '\xff'));
        result.file_content_conflicts.pop_back();
        break;
      }
    // by now, the merge should have been resolved cleanly, one way or another
    result.roster.check_sane();
    I(result.is_clean());
  }
};

void
test_a_scalar_merge(scalar_val left_val, string const & left_marks_str,
                    string const & left_uncommon_str,
                    scalar_val right_val, string const & right_marks_str,
                    string const & right_uncommon_str,
                    scalar_val expected_outcome)
{
  test_a_scalar_merge_impl<basename_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                          right_val, right_marks_str, right_uncommon_str,
                                                          expected_outcome);
  test_a_scalar_merge_impl<basename_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                         right_val, right_marks_str, right_uncommon_str,
                                                         expected_outcome);
  test_a_scalar_merge_impl<parent_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                        right_val, right_marks_str, right_uncommon_str,
                                                        expected_outcome);
  test_a_scalar_merge_impl<parent_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                       right_val, right_marks_str, right_uncommon_str,
                                                       expected_outcome);
  test_a_scalar_merge_impl<attr_scalar<file_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                      right_val, right_marks_str, right_uncommon_str,
                                                      expected_outcome);
  test_a_scalar_merge_impl<attr_scalar<dir_scalar> >(left_val, left_marks_str, left_uncommon_str,
                                                     right_val, right_marks_str, right_uncommon_str,
                                                     expected_outcome);
  test_a_scalar_merge_impl<file_content_scalar>(left_val, left_marks_str, left_uncommon_str,
                                                right_val, right_marks_str, right_uncommon_str,
                                                expected_outcome);
}

UNIT_TEST(roster_merge, scalar_merges)
{
  // Notation: a1* means, "value is a, this is node 1 in the graph, it is
  // marked".  ".2" means, "value is unimportant and different from either a
  // or b, this is node 2 in the graph, it is not marked".
  //
  // Backslashes with dots after them mean, the C++ line continuation rules
  // are annoying when it comes to drawing ascii graphs -- the dot is only to
  // stop the backslash from having special meaning to the parser.  So just
  // ignore them :-).

  //   same in both, same mark
  //               a1*
  //              / \.
  //             a2  a3
  test_a_scalar_merge(scalar_a, "1", "2", scalar_a, "1", "3", scalar_a);

  //   same in both, diff marks
  //               .1*
  //              / \.
  //             a2* a3*
  test_a_scalar_merge(scalar_a, "2", "2", scalar_a, "3", "3", scalar_a);

  //   different, left wins with 1 mark
  //               a1*
  //              / \.
  //             b2* a3
  test_a_scalar_merge(scalar_b, "2", "2", scalar_a, "1", "3", scalar_b);

  //   different, right wins with 1 mark
  //               a1*
  //              / \.
  //             a2  b3*
   test_a_scalar_merge(scalar_a, "1", "2", scalar_b, "3", "3", scalar_b);

  //   different, conflict with 1 mark
  //               .1*
  //              / \.
  //             a2* b3*
  test_a_scalar_merge(scalar_a, "2", "2", scalar_b, "3", "3", scalar_conflict);

  //   different, left wins with 2 marks
  //               a1*
  //              / \.
  //             a2  a3
  //            / \.
  //           b4* b5*
  //            \ /
  //             b6
  test_a_scalar_merge(scalar_b, "45", "2456", scalar_a, "1", "3", scalar_b);

  //   different, right wins with 2 marks
  //               a1*
  //              / \.
  //             a2  a3
  //                / \.
  //               b4* b5*
  //                \ /
  //                 b6
  test_a_scalar_merge(scalar_a, "1", "2", scalar_b, "45", "3456", scalar_b);

  //   different, conflict with 1 mark winning, 1 mark losing
  //               .1*
  //              / \.
  //             a2* a3*
  //              \ / \.
  //               a4  b5*
  test_a_scalar_merge(scalar_a, "23", "24", scalar_b, "5", "5", scalar_conflict);

  //
  //               .1*
  //              / \.
  //             a2* a3*
  //            / \ /
  //           b4* a5
  test_a_scalar_merge(scalar_b, "4", "4", scalar_a, "23", "35", scalar_conflict);

  //   different, conflict with 2 marks both conflicting
  //
  //               .1*
  //              / \.
  //             .2  a3*
  //            / \.
  //           b4* b5*
  //            \ /
  //             b6
  test_a_scalar_merge(scalar_b, "45", "2456", scalar_a, "3", "3", scalar_conflict);

  //
  //               .1*
  //              / \.
  //             a2* .3
  //                / \.
  //               b4* b5*
  //                \ /
  //                 b6
  test_a_scalar_merge(scalar_a, "2", "2", scalar_b, "45", "3456", scalar_conflict);

  //
  //               _.1*_
  //              /     \.
  //             .2      .3
  //            / \     / \.
  //           a4* a5* b6* b7*
  //            \ /     \ /
  //             a8      b9
  test_a_scalar_merge(scalar_a, "45", "2458", scalar_b, "67", "3679", scalar_conflict);
}

namespace
{
  const revision_id a_uncommon1(string(constants::idlen_bytes, '\xaa'));
  const revision_id a_uncommon2(string(constants::idlen_bytes, '\xbb'));
  const revision_id b_uncommon1(string(constants::idlen_bytes, '\xcc'));
  const revision_id b_uncommon2(string(constants::idlen_bytes, '\xdd'));
  const revision_id common1(string(constants::idlen_bytes, '\xee'));
  const revision_id common2(string(constants::idlen_bytes, '\xff'));

  const file_id fid1(string(constants::idlen_bytes, '\x11'));
  const file_id fid2(string(constants::idlen_bytes, '\x22'));
}

static void
make_dir(roster_t & r, marking_map & markings,
         revision_id const & birth_rid, revision_id const & parent_name_rid,
         string const & name, node_id nid)
{
  r.create_dir_node(nid, null_ancestors);
  r.attach_node(nid, file_path_internal(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  safe_insert(markings, make_pair(nid, marking));
}

static void
make_file(roster_t & r, marking_map & markings,
          revision_id const & birth_rid, revision_id const & parent_name_rid,
          revision_id const & file_content_rid,
          string const & name, file_id const & content,
          node_id nid)
{
  r.create_file_node(content, nid, null_ancestors);
  r.attach_node(nid, file_path_internal(name));
  marking_t marking;
  marking.birth_revision = birth_rid;
  marking.parent_name.insert(parent_name_rid);
  marking.file_content.insert(file_content_rid);
  safe_insert(markings, make_pair(nid, marking));
}

static void
make_node_lifecycle_objs(roster_t & r, marking_map & markings, revision_id const & uncommon,
                         string const & name, node_id common_dir_nid, node_id common_file_nid,
                         node_id & safe_dir_nid, node_id & safe_file_nid, node_id_source & nis)
{
  make_dir(r, markings, common1, common1, "common_old_dir", common_dir_nid);
  make_file(r, markings, common1, common1, common1, "common_old_file", fid1, common_file_nid);
  safe_dir_nid = nis.next();
  make_dir(r, markings, uncommon, uncommon, name + "_safe_dir", safe_dir_nid);
  safe_file_nid = nis.next();
  make_file(r, markings, uncommon, uncommon, uncommon, name + "_safe_file", fid1, safe_file_nid);
  make_dir(r, markings, common1, common1, name + "_dead_dir", nis.next());
  make_file(r, markings, common1, common1, common1, name + "_dead_file", fid1, nis.next());
}

UNIT_TEST(roster_merge, node_lifecycle)
{
  roster_t a_roster, b_roster;
  marking_map a_markings, b_markings;
  set<revision_id> a_uncommon, b_uncommon;
  // boilerplate to get uncommon revision sets...
  a_uncommon.insert(a_uncommon1);
  a_uncommon.insert(a_uncommon2);
  b_uncommon.insert(b_uncommon1);
  b_uncommon.insert(b_uncommon2);
  testing_node_id_source nis;
  // boilerplate to set up a root node...
  {
    node_id root_nid = nis.next();
    make_dir(a_roster, a_markings, common1, common1, "", root_nid);
    make_dir(b_roster, b_markings, common1, common1, "", root_nid);
  }
  // create some nodes on each side
  node_id common_dir_nid = nis.next();
  node_id common_file_nid = nis.next();
  node_id a_safe_dir_nid, a_safe_file_nid, b_safe_dir_nid, b_safe_file_nid;
  make_node_lifecycle_objs(a_roster, a_markings, a_uncommon1, "a", common_dir_nid, common_file_nid,
                           a_safe_dir_nid, a_safe_file_nid, nis);
  make_node_lifecycle_objs(b_roster, b_markings, b_uncommon1, "b", common_dir_nid, common_file_nid,
                           b_safe_dir_nid, b_safe_file_nid, nis);
  // do the merge
  roster_merge_result result;
  roster_merge(a_roster, a_markings, a_uncommon, b_roster, b_markings, b_uncommon, nis, result);
  I(result.is_clean());
  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(a_roster, a_markings, b_roster, b_markings);
  // 7 = 1 root + 2 common + 2 safe a + 2 safe b
  I(result.roster.all_nodes().size() == 7);
  // check that they're the right ones...
  MM(result.roster);
  MM(a_roster);
  MM(b_roster);
  I(shallow_equal(result.roster.get_node(common_dir_nid),
                  a_roster.get_node(common_dir_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(common_file_nid),
                  a_roster.get_node(common_file_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(common_dir_nid),
                  b_roster.get_node(common_dir_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(common_file_nid),
                  b_roster.get_node(common_file_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(a_safe_dir_nid),
                  a_roster.get_node(a_safe_dir_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(a_safe_file_nid),
                  a_roster.get_node(a_safe_file_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(b_safe_dir_nid),
                  b_roster.get_node(b_safe_dir_nid), false, true, false));
  I(shallow_equal(result.roster.get_node(b_safe_file_nid),
                  b_roster.get_node(b_safe_file_nid), false, true, false));
}

UNIT_TEST(roster_merge, attr_lifecycle)
{
  roster_t left_roster, right_roster;
  marking_map left_markings, right_markings;
  MM(left_roster);
  MM(left_markings);
  MM(right_roster);
  MM(right_markings);
  set<revision_id> old_revs, left_revs, right_revs;
  string_to_set("0", old_revs);
  string_to_set("1", left_revs);
  string_to_set("2", right_revs);
  revision_id old_rid = *old_revs.begin();
  testing_node_id_source nis;
  node_id dir_nid = nis.next();
  make_dir(left_roster, left_markings, old_rid, old_rid, "", dir_nid);
  make_dir(right_roster, right_markings, old_rid, old_rid, "", dir_nid);
  node_id file_nid = nis.next();
  make_file(left_roster, left_markings, old_rid, old_rid, old_rid, "thing", fid1, file_nid);
  make_file(right_roster, right_markings, old_rid, old_rid, old_rid, "thing", fid1, file_nid);

  // put one live and one dead attr on each thing on each side, with uncommon
  // marks on them
  safe_insert(left_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("left_live"), make_pair(true, attr_value("left_live"))));
  safe_insert(left_markings[dir_nid].attrs, make_pair(attr_key("left_live"), left_revs));
  safe_insert(left_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("left_dead"), make_pair(false, attr_value(""))));
  safe_insert(left_markings[dir_nid].attrs, make_pair(attr_key("left_dead"), left_revs));
  safe_insert(left_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("left_live"), make_pair(true, attr_value("left_live"))));
  safe_insert(left_markings[file_nid].attrs, make_pair(attr_key("left_live"), left_revs));
  safe_insert(left_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("left_dead"), make_pair(false, attr_value(""))));
  safe_insert(left_markings[file_nid].attrs, make_pair(attr_key("left_dead"), left_revs));

  safe_insert(right_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("right_live"), make_pair(true, attr_value("right_live"))));
  safe_insert(right_markings[dir_nid].attrs, make_pair(attr_key("right_live"), right_revs));
  safe_insert(right_roster.get_node(dir_nid)->attrs,
              make_pair(attr_key("right_dead"), make_pair(false, attr_value(""))));
  safe_insert(right_markings[dir_nid].attrs, make_pair(attr_key("right_dead"), right_revs));
  safe_insert(right_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("right_live"), make_pair(true, attr_value("right_live"))));
  safe_insert(right_markings[file_nid].attrs, make_pair(attr_key("right_live"), right_revs));
  safe_insert(right_roster.get_node(file_nid)->attrs,
              make_pair(attr_key("right_dead"), make_pair(false, attr_value(""))));
  safe_insert(right_markings[file_nid].attrs, make_pair(attr_key("right_dead"), right_revs));

  roster_merge_result result;
  MM(result);
  roster_merge(left_roster, left_markings, left_revs,
               right_roster, right_markings, right_revs,
               nis, result);
  // go ahead and check the roster_delta code too, while we're at it...
  test_roster_delta_on(left_roster, left_markings, right_roster, right_markings);
  I(result.roster.all_nodes().size() == 2);
  I(result.roster.get_node(dir_nid)->attrs.size() == 4);
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_live")) == make_pair(true, attr_value("left_live")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("right_live")) == make_pair(true, attr_value("right_live")));
  I(safe_get(result.roster.get_node(dir_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(result.roster.get_node(file_nid)->attrs.size() == 4);
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_live")) == make_pair(true, attr_value("left_live")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("right_live")) == make_pair(true, attr_value("right_live")));
  I(safe_get(result.roster.get_node(file_nid)->attrs, attr_key("left_dead")) == make_pair(false, attr_value("")));
}

struct structural_conflict_helper
{
  roster_t left_roster, right_roster;
  marking_map left_markings, right_markings;
  set<revision_id> old_revs, left_revs, right_revs;
  revision_id old_rid, left_rid, right_rid;
  testing_node_id_source nis;
  node_id root_nid;
  roster_merge_result result;

  virtual void setup() = 0;
  virtual void check() = 0;

  void test()
  {
    MM(left_roster);
    MM(left_markings);
    MM(right_roster);
    MM(right_markings);
    string_to_set("0", old_revs);
    string_to_set("1", left_revs);
    string_to_set("2", right_revs);
    old_rid = *old_revs.begin();
    left_rid = *left_revs.begin();
    right_rid = *right_revs.begin();
    root_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "", root_nid);
    make_dir(right_roster, right_markings, old_rid, old_rid, "", root_nid);

    setup();

    MM(result);
    roster_merge(left_roster, left_markings, left_revs,
                 right_roster, right_markings, right_revs,
                 nis, result);
    // go ahead and check the roster_delta code too, while we're at it...
    test_roster_delta_on(left_roster, left_markings, right_roster, right_markings);

    check();
  }

  virtual ~structural_conflict_helper() {}
};

// two diff nodes with same name
struct simple_duplicate_name_conflict : public structural_conflict_helper
{
  node_id left_nid, right_nid;
  virtual void setup()
  {
    left_nid = nis.next();
    make_dir(left_roster, left_markings, left_rid, left_rid, "thing", left_nid);
    right_nid = nis.next();
    make_dir(right_roster, right_markings, right_rid, right_rid, "thing", right_nid);
  }

  virtual void check()
  {
    I(!result.is_clean());
    duplicate_name_conflict const & c = idx(result.duplicate_name_conflicts, 0);
    I(c.left_nid == left_nid && c.right_nid == right_nid);
    I(c.parent_name == make_pair(root_nid, path_component("thing")));
    // this tests that they were detached, implicitly
    result.roster.attach_node(left_nid, file_path_internal("left"));
    result.roster.attach_node(right_nid, file_path_internal("right"));
    result.duplicate_name_conflicts.pop_back();
    I(result.is_clean());
    result.roster.check_sane();
  }
};

// directory loops
struct simple_dir_loop_conflict : public structural_conflict_helper
{
  node_id left_top_nid, right_top_nid;

  virtual void setup()
    {
      left_top_nid = nis.next();
      right_top_nid = nis.next();

      make_dir(left_roster, left_markings, old_rid, old_rid, "top", left_top_nid);
      make_dir(left_roster, left_markings, old_rid, left_rid, "top/bottom", right_top_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "top", right_top_nid);
      make_dir(right_roster, right_markings, old_rid, right_rid, "top/bottom", left_top_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      directory_loop_conflict const & c = idx(result.directory_loop_conflicts, 0);
      I((c.nid == left_top_nid && c.parent_name == make_pair(right_top_nid, path_component("bottom")))
        || (c.nid == right_top_nid && c.parent_name == make_pair(left_top_nid, path_component("bottom"))));
      // this tests it was detached, implicitly
      result.roster.attach_node(c.nid, file_path_internal("resolved"));
      result.directory_loop_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// orphans
struct simple_orphan_conflict : public structural_conflict_helper
{
  node_id a_dead_parent_nid, a_live_child_nid, b_dead_parent_nid, b_live_child_nid;

  // in ancestor, both parents are alive
  // in left, a_dead_parent is dead, and b_live_child is created
  // in right, b_dead_parent is dead, and a_live_child is created

  virtual void setup()
    {
      a_dead_parent_nid = nis.next();
      a_live_child_nid = nis.next();
      b_dead_parent_nid = nis.next();
      b_live_child_nid = nis.next();

      make_dir(left_roster, left_markings, old_rid, old_rid, "b_parent", b_dead_parent_nid);
      make_dir(left_roster, left_markings, left_rid, left_rid, "b_parent/b_child", b_live_child_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "a_parent", a_dead_parent_nid);
      make_dir(right_roster, right_markings, right_rid, right_rid, "a_parent/a_child", a_live_child_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      I(result.orphaned_node_conflicts.size() == 2);
      orphaned_node_conflict a, b;
      if (idx(result.orphaned_node_conflicts, 0).nid == a_live_child_nid)
        {
          a = idx(result.orphaned_node_conflicts, 0);
          b = idx(result.orphaned_node_conflicts, 1);
        }
      else
        {
          a = idx(result.orphaned_node_conflicts, 1);
          b = idx(result.orphaned_node_conflicts, 0);
        }
      I(a.nid == a_live_child_nid);
      I(a.parent_name == make_pair(a_dead_parent_nid, path_component("a_child")));
      I(b.nid == b_live_child_nid);
      I(b.parent_name == make_pair(b_dead_parent_nid, path_component("b_child")));
      // this tests it was detached, implicitly
      result.roster.attach_node(a.nid, file_path_internal("resolved_a"));
      result.roster.attach_node(b.nid, file_path_internal("resolved_b"));
      result.orphaned_node_conflicts.pop_back();
      result.orphaned_node_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// illegal node ("_MTN")
struct simple_invalid_name_conflict : public structural_conflict_helper
{
  node_id new_root_nid, bad_dir_nid;

  // in left, new_root is the root (it existed in old, but was renamed in left)
  // in right, new_root is still a subdir, the old root still exists, and a
  // new dir has been created

  virtual void setup()
    {
      new_root_nid = nis.next();
      bad_dir_nid = nis.next();

      left_roster.drop_detached_node(left_roster.detach_node(file_path()));
      safe_erase(left_markings, root_nid);
      make_dir(left_roster, left_markings, old_rid, left_rid, "", new_root_nid);

      make_dir(right_roster, right_markings, old_rid, old_rid, "root_to_be", new_root_nid);
      make_dir(right_roster, right_markings, right_rid, right_rid, "root_to_be/_MTN", bad_dir_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      invalid_name_conflict const & c = idx(result.invalid_name_conflicts, 0);
      I(c.nid == bad_dir_nid);
      I(c.parent_name == make_pair(new_root_nid, bookkeeping_root_component));
      // this tests it was detached, implicitly
      result.roster.attach_node(bad_dir_nid, file_path_internal("dir_formerly_known_as__MTN"));
      result.invalid_name_conflicts.pop_back();
      I(result.is_clean());
      result.roster.check_sane();
    }
};

// missing root dir
struct simple_missing_root_dir : public structural_conflict_helper
{
  node_id other_root_nid;

  // left and right each have different root nodes, and each has deleted the
  // other's root node

  virtual void setup()
    {
      other_root_nid = nis.next();

      left_roster.drop_detached_node(left_roster.detach_node(file_path()));
      safe_erase(left_markings, root_nid);
      make_dir(left_roster, left_markings, old_rid, old_rid, "", other_root_nid);
    }

  virtual void check()
    {
      I(!result.is_clean());
      I(result.missing_root_dir);
      result.roster.attach_node(result.roster.create_dir_node(nis), file_path());
      result.missing_root_dir = false;
      I(result.is_clean());
      result.roster.check_sane();
    }
};

UNIT_TEST(roster_merge, simple_structural_conflicts)
{
  {
    simple_duplicate_name_conflict t;
    t.test();
  }
  {
    simple_dir_loop_conflict t;
    t.test();
  }
  {
    simple_orphan_conflict t;
    t.test();
  }
  {
    simple_invalid_name_conflict t;
    t.test();
  }
  {
    simple_missing_root_dir t;
    t.test();
  }
}

struct multiple_name_plus_helper : public structural_conflict_helper
{
  node_id name_conflict_nid;
  node_id left_parent, right_parent;
  path_component left_name, right_name;
  void make_multiple_name_conflict(string const & left, string const & right)
  {
    file_path left_path = file_path_internal(left);
    file_path right_path = file_path_internal(right);
    name_conflict_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, left_rid, left, name_conflict_nid);
    left_parent = left_roster.get_node(left_path)->parent;
    left_name = left_roster.get_node(left_path)->name;
    make_dir(right_roster, right_markings, old_rid, right_rid, right, name_conflict_nid);
    right_parent = right_roster.get_node(right_path)->parent;
    right_name = right_roster.get_node(right_path)->name;
  }
  void check_multiple_name_conflict()
  {
    I(!result.is_clean());
    multiple_name_conflict const & c = idx(result.multiple_name_conflicts, 0);
    I(c.nid == name_conflict_nid);
    I(c.left == make_pair(left_parent, left_name));
    I(c.right == make_pair(right_parent, right_name));
    result.roster.attach_node(name_conflict_nid, file_path_internal("totally_other_name"));
    result.multiple_name_conflicts.pop_back();
    I(result.is_clean());
    result.roster.check_sane();
  }
};

struct multiple_name_plus_duplicate_name : public multiple_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_multiple_name_conflict("a", "b");
    make_dir(left_roster, left_markings, left_rid, left_rid, "b", b_nid);
    make_dir(right_roster, right_markings, right_rid, right_rid, "a", a_nid);
  }

  virtual void check()
  {
    // there should just be a single conflict on name_conflict_nid, and a and
    // b should have landed fine
    I(result.roster.get_node(file_path_internal("a"))->self == a_nid);
    I(result.roster.get_node(file_path_internal("b"))->self == b_nid);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_orphan : public multiple_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, left_rid, "a", a_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "b", b_nid);
    make_multiple_name_conflict("a/foo", "b/foo");
  }

  virtual void check()
  {
    I(result.roster.all_nodes().size() == 2);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_directory_loop : public multiple_name_plus_helper
{
  node_id a_nid, b_nid;

  virtual void setup()
  {
    a_nid = nis.next();
    b_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "a", a_nid);
    make_dir(right_roster, right_markings, old_rid, old_rid, "b", b_nid);
    make_multiple_name_conflict("a/foo", "b/foo");
    make_dir(left_roster, left_markings, old_rid, left_rid, "a/foo/b", b_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "b/foo/a", a_nid);
  }

  virtual void check()
  {
    I(downcast_to_dir_t(result.roster.get_node(name_conflict_nid))->children.size() == 2);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_invalid_name : public multiple_name_plus_helper
{
  node_id new_root_nid;

  virtual void setup()
  {
    new_root_nid = nis.next();
    make_dir(left_roster, left_markings, old_rid, old_rid, "new_root", new_root_nid);
    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "", new_root_nid);
    make_multiple_name_conflict("new_root/_MTN", "foo");
  }

  virtual void check()
  {
    I(result.roster.root()->self == new_root_nid);
    I(result.roster.all_nodes().size() == 2);
    check_multiple_name_conflict();
  }
};

struct multiple_name_plus_missing_root : public structural_conflict_helper
{
  node_id left_root_nid, right_root_nid;

  virtual void setup()
  {
    left_root_nid = nis.next();
    right_root_nid = nis.next();

    left_roster.drop_detached_node(left_roster.detach_node(file_path()));
    safe_erase(left_markings, root_nid);
    make_dir(left_roster, left_markings, old_rid, left_rid, "", left_root_nid);
    make_dir(left_roster, left_markings, old_rid, left_rid, "right_root", right_root_nid);

    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "", right_root_nid);
    make_dir(right_roster, right_markings, old_rid, right_rid, "left_root", left_root_nid);
  }
  void check_helper(multiple_name_conflict const & left_c,
                    multiple_name_conflict const & right_c)
  {
    I(left_c.nid == left_root_nid);
    I(left_c.left == make_pair(the_null_node, path_component()));
    I(left_c.right == make_pair(right_root_nid, path_component("left_root")));

    I(right_c.nid == right_root_nid);
    I(right_c.left == make_pair(left_root_nid, path_component("right_root")));
    I(right_c.right == make_pair(the_null_node, path_component()));
  }
  virtual void check()
  {
    I(!result.is_clean());
    I(result.multiple_name_conflicts.size() == 2);

    if (idx(result.multiple_name_conflicts, 0).nid == left_root_nid)
      check_helper(idx(result.multiple_name_conflicts, 0),
                   idx(result.multiple_name_conflicts, 1));
    else
      check_helper(idx(result.multiple_name_conflicts, 1),
                   idx(result.multiple_name_conflicts, 0));

    I(result.missing_root_dir);

    result.roster.attach_node(left_root_nid, file_path());
    result.roster.attach_node(right_root_nid, file_path_internal("totally_other_name"));
    result.multiple_name_conflicts.pop_back();
    result.multiple_name_conflicts.pop_back();
    result.missing_root_dir = false;
    I(result.is_clean());
    result.roster.check_sane();
  }
};

struct duplicate_name_plus_missing_root : public structural_conflict_helper
{
  node_id left_root_nid, right_root_nid;

  virtual void setup()
  {
    left_root_nid = nis.next();
    right_root_nid = nis.next();

    left_roster.drop_detached_node(left_roster.detach_node(file_path()));
    safe_erase(left_markings, root_nid);
    make_dir(left_roster, left_markings, left_rid, left_rid, "", left_root_nid);

    right_roster.drop_detached_node(right_roster.detach_node(file_path()));
    safe_erase(right_markings, root_nid);
    make_dir(right_roster, right_markings, right_rid, right_rid, "", right_root_nid);
  }
  virtual void check()
  {
    I(!result.is_clean());
    duplicate_name_conflict const & c = idx(result.duplicate_name_conflicts, 0);
    I(c.left_nid == left_root_nid && c.right_nid == right_root_nid);
    I(c.parent_name == make_pair(the_null_node, path_component()));

    I(result.missing_root_dir);

    // we can't just attach one of these as the root -- see the massive
    // comment on the old_locations member of roster_t, in roster.hh.
    result.roster.attach_node(result.roster.create_dir_node(nis), file_path());
    result.roster.attach_node(left_root_nid, file_path_internal("totally_left_name"));
    result.roster.attach_node(right_root_nid, file_path_internal("totally_right_name"));
    result.duplicate_name_conflicts.pop_back();
    result.missing_root_dir = false;
    I(result.is_clean());
    result.roster.check_sane();
  }
};

UNIT_TEST(roster_merge, complex_structural_conflicts)
{
  {
    multiple_name_plus_duplicate_name t;
    t.test();
  }
  {
    multiple_name_plus_orphan t;
    t.test();
  }
  {
    multiple_name_plus_directory_loop t;
    t.test();
  }
  {
    multiple_name_plus_invalid_name t;
    t.test();
  }
  {
    multiple_name_plus_missing_root t;
    t.test();
  }
  {
    duplicate_name_plus_missing_root t;
    t.test();
  }
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
