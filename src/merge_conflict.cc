// Copyright (C) 2005, 2012 Nathaniel Smith <njs@pobox.com>
//               2008, 2009, 2012 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "merge_roster.hh"

#include "basic_io.hh"
#include "file_io.hh"
#include "lua_hooks.hh"
#include "options.hh"
#include "transforms.hh"

#include <sstream>

using std::make_pair;
using std::string;
using boost::shared_ptr;

namespace
{
  enum node_type { file_type, dir_type };

  node_type
  get_type(roster_t const & roster, node_id const nid)
  {
    const_node_t n = roster.get_node(nid);

    if (is_file_t(n))
      return file_type;
    else if (is_dir_t(n))
      return dir_type;
    else
      I(false);
  }

  namespace syms
  {
    symbol const ancestor("ancestor");
    symbol const ancestor_file_id("ancestor_file_id");
    symbol const ancestor_name("ancestor_name");
    symbol const attr_name("attr_name");
    symbol const attribute("attribute");
    symbol const conflict("conflict");
    symbol const content("content");
    symbol const directory_loop("directory_loop");
    symbol const dropped_modified("dropped_modified");
    symbol const duplicate_name("duplicate_name");
    symbol const invalid_name("invalid_name");
    symbol const left("left");
    symbol const left_attr_state("left_attr_state");
    symbol const left_attr_value("left_attr_value");
    symbol const left_file_id("left_file_id");
    symbol const left_name("left_name");
    symbol const left_rev("left_rev");
    symbol const left_type("left_type");
    symbol const missing_root("missing_root");
    symbol const multiple_names("multiple_names");
    symbol const node_type("node_type");
    symbol const orphaned_directory("orphaned_directory");
    symbol const orphaned_file("orphaned_file");
    symbol const resolved_drop_left("resolved_drop_left");
    symbol const resolved_drop_right("resolved_drop_right");
    symbol const resolved_keep_left("resolved_keep_left");
    symbol const resolved_keep_right("resolved_keep_right");
    symbol const resolved_internal("resolved_internal");
    symbol const resolved_rename_left("resolved_rename_left");
    symbol const resolved_rename_right("resolved_rename_right");
    symbol const resolved_user_left("resolved_user_left");
    symbol const resolved_user_right("resolved_user_right");
    symbol const right("right");
    symbol const right_attr_state("right_attr_state");
    symbol const right_attr_value("right_attr_value");
    symbol const right_file_id("right_file_id");
    symbol const right_name("right_name");
    symbol const right_rev("right_rev");
    symbol const right_type("right_type");
  }
}

namespace resolve_conflicts
{
  file_path file_path_external(string path)
  {
    return file_path_external(utf8(path, origin::user));
  };
}


static void
put_added_conflict_left(basic_io::stanza & st,
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
put_added_conflict_right(basic_io::stanza & st,
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
put_rename_conflict_left(basic_io::stanza & st,
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
get_nid_name_pair(roster_t const & roster,
                  string const & path,
                  node_id & nid,
                  std::pair<node_id, path_component> & name)
{
  const_node_t node = roster.get_node(file_path_external(utf8(path, origin::internal)));
  nid = node->self;
  name = make_pair (node->parent, node->name);
}

static void
read_added_rename_conflict_left(basic_io::parser & pars,
                                roster_t const & roster,
                                node_id & left_nid,
                                std::pair<node_id, path_component> & left_name)
{
  string tmp;

  pars.esym(syms::left_type);

  pars.str(tmp);

  if (tmp == "renamed file")
    {
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::ancestor_file_id); pars.hex();

      pars.esym(syms::left_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, left_nid, left_name);
      pars.esym(syms::left_file_id); pars.hex();
    }
  else if (tmp == "renamed directory")
    {
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::left_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, left_nid, left_name);
    }
  else if (tmp == "added file")
    {
      pars.esym(syms::left_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, left_nid, left_name);
      pars.esym(syms::left_file_id); pars.hex();
    }
  else if (tmp == "added directory")
    {
      pars.esym(syms::left_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, left_nid, left_name);
    }
} // read_added_rename_conflict_left

static void
read_added_rename_conflict_right(basic_io::parser & pars,
                                 roster_t const & roster,
                                 node_id & right_nid,
                                 std::pair<node_id, path_component> & right_name)
{
  string tmp;

  pars.esym(syms::right_type);

  pars.str(tmp);

  if (tmp == "renamed file")
    {
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::ancestor_file_id); pars.hex();

      pars.esym(syms::right_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, right_nid, right_name);
      pars.esym(syms::right_file_id); pars.hex();
    }
  else if (tmp == "renamed directory")
    {
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::right_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, right_nid, right_name);
    }
  else if (tmp == "added file")
    {
      pars.esym(syms::right_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, right_nid, right_name);
      pars.esym(syms::right_file_id); pars.hex();
    }
  else if (tmp == "added directory")
    {
      pars.esym(syms::right_name); pars.str(tmp);
      get_nid_name_pair(roster, tmp, right_nid, right_name);
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
put_file_resolution(basic_io::stanza &                           st,
                    resolve_conflicts::side_t                    side,
                    resolve_conflicts::file_resolution_t const & resolution)
{
  // We output any resolution for any conflict; only valid resolutions
  // should get into the data structures. To enforce that, when reading
  // resolutions from files we check that the resolution is valid for the
  // conflict. Hence there is no read_resolution.

  switch (resolution.resolution)
    {
    case resolve_conflicts::none:
      break;

    case resolve_conflicts::content_user:
      switch (side)
        {
        case resolve_conflicts::left_side:
          st.push_str_pair(syms::resolved_user_left, resolution.content->as_external());
          break;

        case resolve_conflicts::right_side:
          st.push_str_pair(syms::resolved_user_right, resolution.content->as_external());
          break;
        }
      break;

    case resolve_conflicts::content_user_rename:
      switch (side)
        {
        case resolve_conflicts::left_side:
          st.push_str_pair(syms::resolved_user_left, resolution.content->as_external());
          st.push_str_pair(syms::resolved_rename_left, resolution.rename.as_external());
          break;

        case resolve_conflicts::right_side:
          st.push_str_pair(syms::resolved_user_right, resolution.content->as_external());
          st.push_str_pair(syms::resolved_rename_right, resolution.rename.as_external());
          break;
        }
      break;

    case resolve_conflicts::content_internal:
      st.push_symbol(syms::resolved_internal);
      break;

    case resolve_conflicts::rename:
      switch (side)
        {
        case resolve_conflicts::left_side:
          st.push_str_pair(syms::resolved_rename_left, resolution.rename.as_external());
          break;

        case resolve_conflicts::right_side:
          st.push_str_pair(syms::resolved_rename_right, resolution.rename.as_external());
          break;
        }
      break;

    case resolve_conflicts::drop:
      switch (side)
        {
        case resolve_conflicts::left_side:
          st.push_symbol(syms::resolved_drop_left);
          break;

        case resolve_conflicts::right_side:
          st.push_symbol(syms::resolved_drop_right);
          break;
        }
      break;

    case resolve_conflicts::keep:
      switch (side)
        {
        case resolve_conflicts::left_side:
          st.push_symbol(syms::resolved_keep_left);
          break;

        case resolve_conflicts::right_side:
          st.push_symbol(syms::resolved_keep_right);
          break;
        }
      break;

    default:
      I(false);
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

  content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));

  // This ensures that the ancestor roster is computed
  boost::shared_ptr<roster_t const> ancestor_roster;
  revision_id ancestor_rid;
  db_adaptor.get_ancestral_roster (conflict.nid, ancestor_rid, ancestor_roster);

  file_path ancestor_name;
  file_path left_name;
  file_path right_name;

  ancestor_roster->get_name (conflict.nid, ancestor_name);
  left_roster.get_name (conflict.nid, left_name);
  right_roster.get_name (conflict.nid, right_name);

  if (file_type == get_type (*ancestor_roster, conflict.nid))
    {
      st.push_str_pair(syms::node_type, "file");
      file_id ancestor_fid;
      db_adaptor.db.get_file_content (db_adaptor.lca, conflict.nid, ancestor_fid);
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_binary_pair(syms::ancestor_file_id, ancestor_fid.inner());
      st.push_file_pair(syms::left_name, left_name);
      file_id left_fid;
      db_adaptor.db.get_file_content (db_adaptor.left_rid, conflict.nid, left_fid);
      st.push_binary_pair(syms::left_file_id, left_fid.inner());
      st.push_file_pair(syms::right_name, right_name);
      file_id right_fid;
      db_adaptor.db.get_file_content (db_adaptor.right_rid, conflict.nid, right_fid);
      st.push_binary_pair(syms::right_file_id, right_fid.inner());
    }
  else
    {
      st.push_str_pair(syms::node_type, "directory");
      st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
      st.push_file_pair(syms::left_name, left_name);
      st.push_file_pair(syms::right_name, right_name);
    }
  put_file_resolution (st, resolve_conflicts::left_side, conflict.resolution);
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

  if (missing_root_conflict)
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
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::right_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the right") % left_lca_name);
            }
        }
      else if (left_root == left_lca_root && right_root != right_lca_root)
        {
          if (!left_roster.has_node(right_root))
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::left_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the left") % right_lca_name);
            }

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
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::right_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, left_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the right") % left_lca_name);
            }

          if (!left_roster.has_node(right_root))
            {
              if (basic_io)
                {
                  st.push_str_pair(syms::left_type, "deleted directory");
                  st.push_str_pair(syms::ancestor_name, right_lca_name.as_external());
                }
              else
                P(F("directory '%s' deleted on the left") % right_lca_name);
            }

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
                                                   const bool basic_io,
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
        P(F("conflict: invalid name '_MTN' in root directory"));

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
                                                     const bool basic_io,
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
        st.push_str_pair(syms::conflict, syms::directory_loop);
      else
        P(F("conflict: directory loop created"));

      if (left_name != lca_name)
        {
          if (basic_io)
            put_rename_conflict_left (st, adaptor, conflict.nid);
          else
            P(F("'%s' renamed to '%s' on the left")
              % lca_name % left_name);
        }

      if (right_name != lca_name)
        {
          if (basic_io)
            put_rename_conflict_right (st, adaptor, conflict.nid);
          else
            P(F("'%s' renamed to '%s' on the right")
              % lca_name % right_name);
        }

      if (left_parent_name != lca_parent_name)
        {
          if (basic_io)
            put_rename_conflict_left (st, adaptor, conflict.parent_name.first);
          else
            P(F("'%s' renamed to '%s' on the left")
              % lca_parent_name % left_parent_name);
        }

      if (right_parent_name != lca_parent_name)
        {
          if (basic_io)
            put_rename_conflict_right (st, adaptor, conflict.parent_name.first);
          else
            P(F("'%s' renamed to '%s' on the right")
              % lca_parent_name % right_parent_name);
        }

      if (basic_io)
        put_stanza(st, output);
    }
}

void
roster_merge_result::report_orphaned_node_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor,
                                                    const bool basic_io,
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
        {
          put_file_resolution (st, resolve_conflicts::left_side, conflict.resolution);
          put_stanza (st, output);
        }
    }
}

void
roster_merge_result::report_multiple_name_conflicts(roster_t const & left_roster,
                                                    roster_t const & right_roster,
                                                    content_merge_adaptor & adaptor,
                                                    const bool basic_io,
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

void static
push_dropped_details(content_merge_database_adaptor & db_adaptor,
                     symbol                           rev_sym,
                     symbol                           name_sym,
                     symbol                           file_id_sym,
                     revision_id                      rev_id,
                     node_id                          nid,
                     basic_io::stanza &               st)
{
  revision_id dropped_rev_id;
  file_path   dropped_name;
  file_id     dropped_file_id;
  db_adaptor.get_dropped_details(rev_id, nid, dropped_rev_id, dropped_name, dropped_file_id);

  st.push_binary_pair(rev_sym, dropped_rev_id.inner());
  st.push_str_pair(name_sym, dropped_name.as_external());
  st.push_binary_pair(file_id_sym, dropped_file_id.inner());
}

void
roster_merge_result::report_dropped_modified_conflicts(roster_t const & left_roster,
                                                       roster_t const & right_roster,
                                                       content_merge_adaptor & adaptor,
                                                       const bool basic_io,
                                                       std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);

  for (size_t i = 0; i < dropped_modified_conflicts.size(); ++i)
    {
      dropped_modified_conflict const & conflict = dropped_modified_conflicts[i];
      MM(conflict);

      node_id nid;
      file_path modified_name;

      switch (conflict.dropped_side)
        {
        case resolve_conflicts::left_side:
          I(!roster.is_attached(conflict.right_nid));

          nid = conflict.right_nid;
          right_roster.get_name(conflict.right_nid, modified_name);
          break;

        case resolve_conflicts::right_side:
          I(!roster.is_attached(conflict.left_nid));

          nid = conflict.left_nid;
          left_roster.get_name(conflict.left_nid, modified_name);
          break;
        }

      shared_ptr<roster_t const> lca_roster;
      revision_id lca_rid;
      file_path ancestor_name;

      adaptor.get_ancestral_roster(nid, lca_rid, lca_roster);
      lca_roster->get_name(nid, ancestor_name);

      if (basic_io)
        {
          basic_io::stanza st;

          content_merge_database_adaptor & db_adaptor (dynamic_cast<content_merge_database_adaptor &>(adaptor));
          file_id fid;

          st.push_str_pair(syms::conflict, syms::dropped_modified);
          st.push_str_pair(syms::ancestor_name, ancestor_name.as_external());
          db_adaptor.db.get_file_content (db_adaptor.lca, nid, fid);
          st.push_binary_pair(syms::ancestor_file_id, fid.inner());

          switch (conflict.dropped_side)
            {
            case resolve_conflicts::left_side:
              if (conflict.orphaned)
                {
                   st.push_str_pair(syms::left_type, "orphaned file");
                   push_dropped_details(db_adaptor, syms::left_rev, syms::left_name, syms::left_file_id,
                                        db_adaptor.left_rid, nid, st);
                }
              else
                {
                  if (conflict.left_nid == the_null_node)
                    {
                      st.push_str_pair(syms::left_type, "dropped file");
                      push_dropped_details(db_adaptor, syms::left_rev, syms::left_name, syms::left_file_id,
                                           db_adaptor.left_rid, nid, st);
                    }
                  else
                    {
                      st.push_str_pair(syms::left_type, "recreated file");
                      st.push_str_pair(syms::left_name, modified_name.as_external());
                      db_adaptor.db.get_file_content (db_adaptor.left_rid, conflict.left_nid, fid);
                      st.push_binary_pair(syms::left_file_id, fid.inner());
                    }
                }

              st.push_str_pair(syms::right_type, "modified file");
              st.push_str_pair(syms::right_name, modified_name.as_external());
              db_adaptor.db.get_file_content (db_adaptor.right_rid, nid, fid);
              st.push_binary_pair(syms::right_file_id, fid.inner());
              break;

            case resolve_conflicts::right_side:
              st.push_str_pair(syms::left_type, "modified file");
              st.push_str_pair(syms::left_name, modified_name.as_external());
              db_adaptor.db.get_file_content (db_adaptor.left_rid, nid, fid);
              st.push_binary_pair(syms::left_file_id, fid.inner());

              if (conflict.orphaned)
                {
                  st.push_str_pair(syms::right_type, "orphaned file");
                  push_dropped_details(db_adaptor, syms::right_rev, syms::right_name, syms::right_file_id,
                                       db_adaptor.right_rid, nid, st);
                }
              else
                {
                  if (conflict.right_nid == the_null_node)
                    {
                      st.push_str_pair(syms::right_type, "dropped file");
                      push_dropped_details(db_adaptor, syms::right_rev, syms::right_name, syms::right_file_id,
                                           db_adaptor.right_rid, nid, st);
                    }
                  else
                    {
                      st.push_str_pair(syms::right_type, "recreated file");
                      st.push_str_pair(syms::right_name, modified_name.as_external());
                      db_adaptor.db.get_file_content (db_adaptor.right_rid, conflict.right_nid, fid);
                      st.push_binary_pair(syms::right_file_id, fid.inner());
                    }
                }
              break;
            }

          put_file_resolution (st, resolve_conflicts::left_side, conflict.left_resolution);
          put_file_resolution (st, resolve_conflicts::right_side, conflict.right_resolution);

          put_stanza(st, output);
        }
      else
        {
          P(F("conflict: file '%s' from revision %s") % ancestor_name % lca_rid);
          switch (conflict.dropped_side)
            {
            case resolve_conflicts::left_side:
              if (conflict.orphaned)
                {
                  P(F("orphaned on the left"));
                }
              else
                {
                  if (conflict.left_nid == the_null_node)
                    P(F("dropped on the left"));
                  else
                    P(F("dropped and recreated on the left"));
                }
              P(F("modified on the right, named %s") % modified_name);
              break;

            case resolve_conflicts::right_side:
              P(F("modified on the left, named %s") % modified_name);
              if (conflict.orphaned)
                {
                  P(F("orphaned on the right"));
                }
              else
                {
                  if (conflict.right_nid == the_null_node)
                    P(F("dropped on the right"));
                  else
                    P(F("dropped and recreated on the right"));
                }
              break;
            }

          // We can have a resolution from a mtn:resolve_conflict attribute
          // (so far, that only supports drop).
          switch (conflict.left_resolution.resolution)
            {
            case resolve_conflicts::none:
              break;

            case resolve_conflicts::content_user:
            case resolve_conflicts::content_user_rename:
            case resolve_conflicts::rename:
            case resolve_conflicts::keep:
            case resolve_conflicts::content_internal:
              I(false);
              break;

            case resolve_conflicts::drop:
              P(F("left_resolution: drop"));
              break;
            }
          switch (conflict.right_resolution.resolution)
            {
            case resolve_conflicts::none:
              break;

            case resolve_conflicts::content_user:
            case resolve_conflicts::content_user_rename:
            case resolve_conflicts::rename:
            case resolve_conflicts::keep:
            case resolve_conflicts::content_internal:
              I(false);
              break;

            case resolve_conflicts::drop:
              P(F("right_resolution: drop"));
              break;
            }
        }
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
        {
          put_file_resolution (st, resolve_conflicts::left_side, conflict.left_resolution);
          put_file_resolution (st, resolve_conflicts::right_side, conflict.right_resolution);
          put_stanza(st, output);
        }
    }
}

void
roster_merge_result::report_attribute_conflicts(roster_t const & left_roster,
                                                roster_t const & right_roster,
                                                content_merge_adaptor & adaptor,
                                                const bool basic_io,
                                                std::ostream & output) const
{
  MM(left_roster);
  MM(right_roster);
  MM(roster);

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
          // this->roster is null when we are called from 'conflicts
          // show_remaining'; treat as unattached in that case.
          node_type type = get_type(left_roster, conflict.nid);

          if (roster.all_nodes().size() > 0 && roster.is_attached(conflict.nid))
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

namespace
{
  bool
  auto_merge_succeeds(lua_hooks & lua,
                      file_content_conflict conflict,
                      content_merge_adaptor & adaptor,
                      roster_t const & left_roster,
                      roster_t const & right_roster)
  {
    revision_id ancestor_rid;
    shared_ptr<roster_t const> ancestor_roster;
    adaptor.get_ancestral_roster(conflict.nid, ancestor_rid, ancestor_roster);

    I(ancestor_roster);
    I(ancestor_roster->has_node(conflict.nid)); // this fails if there is no least common ancestor

    file_id anc_id, left_id, right_id;
    file_path anc_path, left_path, right_path;
    ancestor_roster->get_file_details(conflict.nid, anc_id, anc_path);
    left_roster.get_file_details(conflict.nid, left_id, left_path);
    right_roster.get_file_details(conflict.nid, right_id, right_path);

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
                                                   const bool basic_io,
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

          if (conflict.resolution.resolution == resolve_conflicts::none)
            if (auto_merge_succeeds(lua, conflict, adaptor, left_roster, right_roster))
              conflict.resolution.resolution = resolve_conflicts::content_internal;

          st.push_str_pair(syms::conflict, syms::content);
          put_content_conflict (st, left_roster, right_roster, adaptor, conflict);
          put_stanza (st, output);
        }
      else
        {
          if (roster.is_attached(conflict.nid))
            {
              file_path name;
              roster.get_name(conflict.nid, name);

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

              file_path left_name, right_name;
              left_roster.get_name(conflict.nid, left_name);
              right_roster.get_name(conflict.nid, right_name);

              shared_ptr<roster_t const> lca_roster;
              revision_id lca_rid;
              file_path lca_name;

              adaptor.get_ancestral_roster(conflict.nid, lca_rid, lca_roster);
              lca_roster->get_name(conflict.nid, lca_name);

              P(F("conflict: content conflict on file '%s' from revision %s")
                % lca_name % lca_rid);
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
    revision_id ancestor_rid;
    shared_ptr<roster_t const> ancestor_roster;
    adaptor.get_ancestral_roster(conflict.nid, ancestor_rid, ancestor_roster);

    I(ancestor_roster);
    I(ancestor_roster->has_node(conflict.nid)); // this fails if there is no least common ancestor

    file_id anc_id, left_id, right_id;
    file_path anc_path, left_path, right_path, merged_path;
    ancestor_roster->get_file_details(conflict.nid, anc_id, anc_path);
    left_roster.get_file_details(conflict.nid, left_id, left_path);
    right_roster.get_file_details(conflict.nid, right_id, right_path);
    result_roster.get_file_details(conflict.nid, merged_id, merged_path);

    content_merger cm(lua, *ancestor_roster, left_roster, right_roster, adaptor);

    return cm.try_auto_merge(anc_path, left_path, right_path, merged_path,
                             anc_id, left_id, right_id, merged_id);
  }
}

static char const * const conflicts_mismatch_msg = N_("conflicts file does not match current conflicts");
static char const * const conflict_resolution_not_supported_msg = N_("%s is not a supported conflict resolution for %s");
static char const * const conflict_extra = N_("extra chars at end of conflict");

static void
read_missing_root_conflicts(basic_io::parser & pars,
                            bool & missing_root_conflict,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  // There can be only one of these
  if (pars.tok.in.lookahead != EOF && pars.symp(syms::missing_root))
    {
      pars.sym();

      if (pars.symp(syms::left_type))
        {
          pars.sym(); pars.str();
          pars.esym(syms::ancestor_name); pars.str();
          pars.esym(syms::right_type); pars.str();
          pars.esym(syms::ancestor_name); pars.str();
        }
      // else unrelated projects (branches); nothing else output

      missing_root_conflict = true;

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
  else
    {
      missing_root_conflict = false;
    }
} // read_missing_root_conflicts

static void
read_invalid_name_conflict(basic_io::parser & pars,
                           invalid_name_conflict & conflict,
                           roster_t const & left_roster,
                           roster_t const & right_roster)
{
  if (pars.symp(syms::left_type))
    {
      pars.sym();
      pars.str(); // "pivoted root"
      pars.esym(syms::ancestor_name); pars.str(); // lca_parent_name
      read_added_rename_conflict_right (pars, right_roster, conflict.nid, conflict.parent_name);
    }
  else
    {
      pars.esym(syms::right_type);
      pars.str(); // "pivoted root"
      pars.esym(syms::ancestor_name); pars.str(); // lca_parent_name
      read_added_rename_conflict_left (pars, left_roster, conflict.nid, conflict.parent_name);
    }
} // read_invalid_name_conflict

static void
read_invalid_name_conflicts(basic_io::parser & pars,
                            std::vector<invalid_name_conflict> & conflicts,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::invalid_name))
    {
      invalid_name_conflict conflict;

      pars.sym();

      read_invalid_name_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_invalid_name_conflicts

static void
read_directory_loop_conflict(basic_io::parser & pars,
                             directory_loop_conflict & conflict,
                             roster_t const & left_roster,
                             roster_t const & right_roster)
{
  string tmp;

  // syms::directory_loop has been read

  if (pars.symp(syms::left_type))
    {
      read_added_rename_conflict_left(pars, left_roster, conflict.nid, conflict.parent_name);
    }
  if (pars.symp(syms::right_type))
    {
      read_added_rename_conflict_right(pars, right_roster, conflict.nid, conflict.parent_name);
    }

  if (pars.symp(syms::left_type))
    {
      pars.sym();
      pars.str(); // "renamed directory"
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::left_name); pars.str();
    }
  if (pars.symp(syms::right_type))
    {
      pars.sym();
      pars.str(); // "renamed directory"
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::right_name); pars.str();
    }

} // read_directory_loop_conflict

static void
read_directory_loop_conflicts(basic_io::parser & pars,
                              std::vector<directory_loop_conflict> & conflicts,
                              roster_t const & left_roster,
                              roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::directory_loop))
    {
      directory_loop_conflict conflict;

      pars.sym();

      read_directory_loop_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_directory_loop_conflicts


static void
read_orphaned_node_conflict(basic_io::parser & pars,
                            orphaned_node_conflict & conflict,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  if (pars.symp(syms::left_type))
    {
      pars.sym(); pars.str(); // "deleted directory | file"
      pars.esym(syms::ancestor_name); pars.str();
      read_added_rename_conflict_right(pars, right_roster, conflict.nid, conflict.parent_name);
    }
  else
    {
      pars.esym(syms::right_type);
      pars.str(); // "deleted directory | file"
      pars.esym(syms::ancestor_name); pars.str();
      read_added_rename_conflict_left(pars, left_roster, conflict.nid, conflict.parent_name);
    }

  // check for a resolution
  if ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
    {
      if (pars.symp (syms::resolved_drop_left))
        {
          conflict.resolution.resolution = resolve_conflicts::drop;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_rename_left))
        {
          conflict.resolution.resolution = resolve_conflicts::rename;
          pars.sym();
          conflict.resolution.rename = resolve_conflicts::file_path_external(pars.token);
          pars.str();
        }
      else
        E(false, origin::user,
          F(conflict_resolution_not_supported_msg) % pars.token % "orphaned_node");
    }

} // read_orphaned_node_conflict

static void
read_orphaned_node_conflicts(basic_io::parser & pars,
                            std::vector<orphaned_node_conflict> & conflicts,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && (pars.symp(syms::orphaned_directory) || pars.symp(syms::orphaned_file)))
    {
      orphaned_node_conflict conflict;

      pars.sym();

      read_orphaned_node_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_orphaned_node_conflicts

static void
validate_orphaned_node_conflicts(basic_io::parser & pars,
                                 std::vector<orphaned_node_conflict> & conflicts,
                                 roster_t const & left_roster,
                                 roster_t const & right_roster)
{
  for (std::vector<orphaned_node_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      orphaned_node_conflict & merge_conflict = *i;
      orphaned_node_conflict file_conflict;

      if (pars.symp (syms::orphaned_directory) || pars.symp (syms::orphaned_file))
        {
          pars.sym();
          read_orphaned_node_conflict(pars, file_conflict, left_roster, right_roster);
        }
      else
        E(false, origin::user, F("expected orphaned_directory or orphaned_file, found %s") % pars.token);

      E(merge_conflict.nid == file_conflict.nid, origin::user,
        F(conflicts_mismatch_msg));

      merge_conflict.resolution = file_conflict.resolution;

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<orphaned_node_conflict>::iterator tmp = i;
          E(++tmp == conflicts.end(), origin::user,
            F("conflicts file does not match current conflicts"));
        }
    }
} // validate_orphaned_node_conflicts


static void
read_multiple_name_conflict(basic_io::parser & pars,
                            multiple_name_conflict & conflict,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  read_added_rename_conflict_left(pars, left_roster, conflict.nid, conflict.left);
  read_added_rename_conflict_right(pars, right_roster, conflict.nid, conflict.right);
} // read_multiple_name_conflict

static void
read_multiple_name_conflicts(basic_io::parser & pars,
                             std::vector<multiple_name_conflict> & conflicts,
                             roster_t const & left_roster,
                             roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::multiple_names))
    {
      multiple_name_conflict conflict(the_null_node);

      pars.sym();

      read_multiple_name_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_multiple_name_conflicts

static void
read_dropped_modified_conflict(basic_io::parser &          pars,
                               dropped_modified_conflict & conflict,
                               revision_id                 left_rid,
                               roster_t const &            left_roster,
                               revision_id                 right_rid,
                               roster_t const &            right_roster)
{
  string tmp;

  pars.esym(syms::ancestor_name); pars.str();
  pars.esym(syms::ancestor_file_id); pars.hex();

  pars.esym(syms::left_type);
  pars.str(tmp);

  if (tmp == "dropped file")
    {
      conflict.dropped_side = resolve_conflicts::left_side;

      pars.esym(syms::left_rev); pars.hex(tmp);
      conflict.left_rid = decode_hexenc_as<revision_id>(tmp, pars.tok.in.made_from);
      pars.esym(syms::left_name); pars.str();
      pars.esym(syms::left_file_id); pars.hex();
    }
  else if (tmp == "orphaned file")
    {
      conflict.dropped_side = resolve_conflicts::left_side;
      conflict.orphaned = true;

      pars.esym(syms::left_rev); pars.hex(tmp);
      conflict.left_rid = decode_hexenc_as<revision_id>(tmp, pars.tok.in.made_from);
      pars.esym(syms::left_name); pars.str();
      pars.esym(syms::left_file_id); pars.hex();
    }
  else if (tmp == "recreated file")
    {
      conflict.dropped_side = resolve_conflicts::left_side;
      conflict.left_rid = left_rid;

      pars.esym(syms::left_name); pars.str(tmp);
      conflict.left_nid = left_roster.get_node(resolve_conflicts::file_path_external(tmp))->self;
      pars.esym(syms::left_file_id); pars.hex();
    }
  else if (tmp == "modified file")
    {
      conflict.left_rid = left_rid;

      pars.esym(syms::left_name); pars.str(tmp);
      conflict.left_nid = left_roster.get_node(resolve_conflicts::file_path_external(tmp))->self;
      pars.esym(syms::left_file_id); pars.hex();
    }
  else
    I(false);

  pars.esym(syms::right_type);
  pars.str(tmp);

  if (tmp == "dropped file")
    {
      conflict.dropped_side = resolve_conflicts::right_side;

      pars.esym(syms::right_rev); pars.hex(tmp);
      conflict.right_rid = decode_hexenc_as<revision_id>(tmp, pars.tok.in.made_from);
      pars.esym(syms::right_name); pars.str();
      pars.esym(syms::right_file_id); pars.hex();
    }
  else if (tmp == "orphaned file")
    {
      conflict.dropped_side = resolve_conflicts::right_side;
      conflict.orphaned = true;

      pars.esym(syms::right_rev); pars.hex(tmp);
      conflict.right_rid = decode_hexenc_as<revision_id>(tmp, pars.tok.in.made_from);
      pars.esym(syms::right_name); pars.str();
      pars.esym(syms::right_file_id); pars.hex();
    }
  else if (tmp == "recreated file")
    {
      conflict.dropped_side = resolve_conflicts::right_side;
      conflict.right_rid = right_rid;

      pars.esym(syms::right_name); pars.str(tmp);
      conflict.right_nid = right_roster.get_node(resolve_conflicts::file_path_external(tmp))->self;
      pars.esym(syms::right_file_id); pars.hex();
    }
  else if (tmp == "modified file")
    {
      conflict.right_rid = right_rid;

      pars.esym(syms::right_name); pars.str(tmp);
      conflict.right_nid = right_roster.get_node(resolve_conflicts::file_path_external(tmp))->self;
      pars.esym(syms::right_file_id); pars.hex();
    }
  else
    I(false);

  // check for resolutions
  while ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
    {
      if (pars.symp (syms::resolved_drop_left))
        {
          conflict.left_resolution.resolution = resolve_conflicts::drop;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_drop_right))
        {
          conflict.right_resolution.resolution = resolve_conflicts::drop;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_keep_left))
        {
          E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

          conflict.left_resolution.resolution = resolve_conflicts::keep;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_keep_right))
        {
          E(!conflict.orphaned, origin::user, F("orphaned files must be renamed"));

          conflict.right_resolution.resolution = resolve_conflicts::keep;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_rename_left))
        {
          if (conflict.left_resolution.resolution == resolve_conflicts::content_user)
            conflict.left_resolution.resolution = resolve_conflicts::content_user_rename;
          else
            conflict.left_resolution.resolution = resolve_conflicts::rename;
          pars.sym();
          conflict.left_resolution.rename = resolve_conflicts::file_path_external(pars.token);
          pars.str();
        }
      else if (pars.symp (syms::resolved_rename_right))
        {
          if (conflict.right_resolution.resolution == resolve_conflicts::content_user)
            conflict.right_resolution.resolution = resolve_conflicts::content_user_rename;
          else
            conflict.right_resolution.resolution = resolve_conflicts::rename;
          pars.sym();
          conflict.right_resolution.rename = resolve_conflicts::file_path_external(pars.token);
          pars.str();
        }
      else if (pars.symp (syms::resolved_user_left))
        {
          if (conflict.left_resolution.resolution == resolve_conflicts::rename)
            conflict.left_resolution.resolution = resolve_conflicts::content_user_rename;
          else
            conflict.left_resolution.resolution = resolve_conflicts::content_user;
          pars.sym();
          conflict.left_resolution.content = new_optimal_path(pars.token, false);
          pars.str();
        }
      else if (pars.symp (syms::resolved_user_right))
        {
          if (conflict.right_resolution.resolution == resolve_conflicts::rename)
            conflict.right_resolution.resolution = resolve_conflicts::content_user_rename;
          else
            conflict.right_resolution.resolution = resolve_conflicts::content_user;
          pars.sym();
          conflict.right_resolution.content = new_optimal_path(pars.token, false);
          pars.str();
        }
      else
        E(false, origin::user,
          F(conflict_resolution_not_supported_msg) % pars.token % "dropped_modified");
    }
} // read_dropped_modified_conflict

static void
read_dropped_modified_conflicts(basic_io::parser &                       pars,
                                std::vector<dropped_modified_conflict> & conflicts,
                                revision_id                              left_rid,
                                roster_t const &                         left_roster,
                                revision_id                              right_rid,
                                roster_t const &                         right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::dropped_modified))
    {
      dropped_modified_conflict conflict;

      pars.sym();

      read_dropped_modified_conflict(pars, conflict, left_rid, left_roster, right_rid, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_dropped_modified_conflicts

static void
validate_dropped_modified_conflicts(basic_io::parser &                       pars,
                                    std::vector<dropped_modified_conflict> & conflicts,
                                    revision_id                              left_rid,
                                    roster_t const &                         left_roster,
                                    revision_id                              right_rid,
                                    roster_t const &                         right_roster)
{
  for (std::vector<dropped_modified_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      dropped_modified_conflict & merge_conflict = *i;
      dropped_modified_conflict file_conflict (the_null_node, the_null_node);

      pars.esym(syms::dropped_modified);

      read_dropped_modified_conflict(pars, file_conflict, left_rid, left_roster, right_rid, right_roster);

      // Note that we do not confirm the file ids.
      E(merge_conflict.dropped_side == file_conflict.dropped_side &&
        merge_conflict.left_nid == file_conflict.left_nid &&
        merge_conflict.right_nid == file_conflict.right_nid,
        origin::user,
        F(conflicts_mismatch_msg));

      merge_conflict.left_rid         = file_conflict.left_rid;
      merge_conflict.right_rid        = file_conflict.right_rid;
      merge_conflict.left_resolution  = file_conflict.left_resolution;
      merge_conflict.right_resolution = file_conflict.right_resolution;

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<dropped_modified_conflict>::iterator tmp = i;
          E(++tmp == conflicts.end(), origin::user,
             F(conflicts_mismatch_msg));
        }
    }
} // validate_dropped_modified_conflicts

static void
read_duplicate_name_conflict(basic_io::parser & pars,
                             duplicate_name_conflict & conflict,
                             roster_t const & left_roster,
                             roster_t const & right_roster)
{
  read_added_rename_conflict_left(pars, left_roster, conflict.left_nid, conflict.parent_name);
  read_added_rename_conflict_right(pars, right_roster, conflict.right_nid, conflict.parent_name);

  // check for a resolution
  while ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
    {
      if (pars.symp (syms::resolved_drop_left))
        {
          conflict.left_resolution.resolution = resolve_conflicts::drop;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_drop_right))
        {
          conflict.right_resolution.resolution = resolve_conflicts::drop;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_keep_left))
        {
          conflict.left_resolution.resolution = resolve_conflicts::keep;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_keep_right))
        {
          conflict.right_resolution.resolution = resolve_conflicts::keep;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_rename_left))
        {
          conflict.left_resolution.resolution = resolve_conflicts::rename;
          pars.sym();
          conflict.left_resolution.rename = resolve_conflicts::file_path_external(pars.token);
          pars.str();
        }
      else if (pars.symp (syms::resolved_rename_right))
        {
          conflict.right_resolution.resolution = resolve_conflicts::rename;
          pars.sym();
          conflict.right_resolution.rename = resolve_conflicts::file_path_external(pars.token);
          pars.str();
        }
      else if (pars.symp (syms::resolved_user_left))
        {
          conflict.left_resolution.resolution = resolve_conflicts::content_user;
          pars.sym();
          conflict.left_resolution.content = new_optimal_path(pars.token, true);
          pars.str();
        }
      else if (pars.symp (syms::resolved_user_right))
        {
          conflict.right_resolution.resolution = resolve_conflicts::content_user;
          pars.sym();
          conflict.right_resolution.content = new_optimal_path(pars.token, true);
          pars.str();
        }
      else
        E(false, origin::user,
          F(conflict_resolution_not_supported_msg) % pars.token % "duplicate_name");
    }

} // read_duplicate_name_conflict

static void
read_duplicate_name_conflicts(basic_io::parser & pars,
                              std::vector<duplicate_name_conflict> & conflicts,
                              roster_t const & left_roster,
                              roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::duplicate_name))
    {
      duplicate_name_conflict conflict;

      pars.sym();

      read_duplicate_name_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_duplicate_name_conflicts
static void
validate_duplicate_name_conflicts(basic_io::parser & pars,
                                  std::vector<duplicate_name_conflict> & conflicts,
                                  roster_t const & left_roster,
                                  roster_t const & right_roster)
{
  for (std::vector<duplicate_name_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      duplicate_name_conflict & merge_conflict = *i;
      duplicate_name_conflict file_conflict;

      pars.esym(syms::duplicate_name);

      read_duplicate_name_conflict(pars, file_conflict, left_roster, right_roster);

      // Note that we do not confirm the file ids.
      E(merge_conflict.left_nid == file_conflict.left_nid &&
        merge_conflict.right_nid == file_conflict.right_nid,
        origin::user,
        F(conflicts_mismatch_msg));

      merge_conflict.left_resolution = file_conflict.left_resolution;
      merge_conflict.right_resolution = file_conflict.right_resolution;

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<duplicate_name_conflict>::iterator tmp = i;
          E(++tmp == conflicts.end(), origin::user,
             F(conflicts_mismatch_msg));
        }
    }
} // validate_duplicate_name_conflicts

static void
read_attr_state_left(basic_io::parser & pars,
                     std::pair<bool, attr_value> & value)
{
  string tmp;

  if (pars.symp(syms::left_attr_value))
    {
      pars.sym();
      value.first = true;
      pars.str(tmp);
      value.second = attr_value(tmp, pars.tok.in.made_from);
    }
  else
    {
      pars.esym(syms::left_attr_state);
      pars.str(tmp);
      I(tmp == "dropped");
      value.first = false;
    }
} // read_attr_state_left

static void
read_attr_state_right(basic_io::parser & pars,
                      std::pair<bool, attr_value> & value)
{
  string tmp;

  if (pars.symp(syms::right_attr_value))
    {
      pars.sym();
      value.first = true;
      pars.str(tmp);
      value.second = attr_value(tmp, pars.tok.in.made_from);
    }
  else
    {
      pars.esym(syms::right_attr_state);
      pars.str(tmp);
      I(tmp == "dropped");
      value.first = false;
    }
} // read_attr_state_right

static void
read_attribute_conflict(basic_io::parser & pars,
                        attribute_conflict & conflict,
                        roster_t const & left_roster,
                        roster_t const & right_roster)
{
  string tmp;

  pars.esym(syms::node_type);

  pars.str(tmp);

  if (tmp == "file")
    {
      pars.esym(syms::attr_name); pars.str(tmp);
      conflict.key = attr_key(tmp, pars.tok.in.made_from);
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::ancestor_file_id); pars.hex();
      pars.esym(syms::left_name); pars.str(tmp);
      conflict.nid = left_roster.get_node(file_path_external(utf8(tmp, pars.tok.in.made_from)))->self;
      pars.esym(syms::left_file_id); pars.hex();
      read_attr_state_left(pars, conflict.left);
      pars.esym(syms::right_name); pars.str();
      pars.esym(syms::right_file_id); pars.hex();
      read_attr_state_right(pars, conflict.right);
    }
  else if (tmp == "directory")
    {
      pars.esym(syms::attr_name); pars.str(tmp);
      conflict.key = attr_key(tmp, pars.tok.in.made_from);
      pars.esym(syms::ancestor_name); pars.str();
      pars.esym(syms::left_name); pars.str(tmp);
      conflict.nid = left_roster.get_node(file_path_external(utf8(tmp, pars.tok.in.made_from)))->self;
      read_attr_state_left(pars, conflict.left);
      pars.esym(syms::right_name); pars.str();
      read_attr_state_right(pars, conflict.right);
    }
  else
    I(false);

} // read_attribute_conflict

static void
read_attribute_conflicts(basic_io::parser & pars,
                         std::vector<attribute_conflict> & conflicts,
                         roster_t const & left_roster,
                         roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::attribute))
    {
      attribute_conflict conflict(the_null_node);

      pars.sym();

      read_attribute_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_attribute_conflicts

static void
read_file_content_conflict(basic_io::parser & pars,
                           file_content_conflict & conflict,
                           roster_t const & left_roster,
                           roster_t const & right_roster)
{
  string tmp;
  string left_name, right_name, result_name;

  pars.esym(syms::node_type); pars.str(tmp); I(tmp == "file");

  pars.esym (syms::ancestor_name); pars.str();
  pars.esym (syms::ancestor_file_id); pars.hex(tmp);
  conflict.ancestor = decode_hexenc_as<file_id>(tmp, pars.tok.in.made_from);

  pars.esym (syms::left_name); pars.str(left_name);
  pars.esym(syms::left_file_id); pars.hex(tmp);
  conflict.left = decode_hexenc_as<file_id>(tmp, pars.tok.in.made_from);

  pars.esym (syms::right_name); pars.str(right_name);
  pars.esym(syms::right_file_id); pars.hex(tmp);
  conflict.right = decode_hexenc_as<file_id>(tmp, pars.tok.in.made_from);

  conflict.nid = left_roster.get_node (file_path_internal (left_name))->self;
  I(conflict.nid = right_roster.get_node (file_path_internal (right_name))->self);

  // check for a resolution
  if ((!pars.symp (syms::conflict)) && pars.tok.in.lookahead != EOF)
    {
      if (pars.symp (syms::resolved_internal))
        {
          conflict.resolution.resolution = resolve_conflicts::content_internal;
          pars.sym();
        }
      else if (pars.symp (syms::resolved_user_left))
        {
          conflict.resolution.resolution = resolve_conflicts::content_user;
          pars.sym();
          conflict.resolution.content = new_optimal_path(pars.token, true);
          pars.str();
        }
      else
        E(false, origin::user,
          F(conflict_resolution_not_supported_msg) % pars.token % "file_content");
    }

} // read_file_content_conflict

static void
read_file_content_conflicts(basic_io::parser & pars,
                            std::vector<file_content_conflict> & conflicts,
                            roster_t const & left_roster,
                            roster_t const & right_roster)
{
  while (pars.tok.in.lookahead != EOF && pars.symp(syms::content))
    {
      file_content_conflict conflict;

      pars.sym();

      read_file_content_conflict(pars, conflict, left_roster, right_roster);

      conflicts.push_back(conflict);

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
    }
} // read_file_content_conflicts

static void
validate_file_content_conflicts(basic_io::parser & pars,
                                std::vector<file_content_conflict> & conflicts,
                                roster_t const & left_roster,
                                roster_t const & right_roster)
{
  for (std::vector<file_content_conflict>::iterator i = conflicts.begin();
       i != conflicts.end();
       ++i)
    {
      file_content_conflict & merge_conflict = *i;
      file_content_conflict file_conflict;

      pars.esym(syms::content);

      read_file_content_conflict(pars, file_conflict, left_roster, right_roster);

      E(merge_conflict.nid == file_conflict.nid, origin::user,
        F(conflicts_mismatch_msg));

      merge_conflict.resolution = file_conflict.resolution;

      if (pars.tok.in.lookahead != EOF)
        pars.esym (syms::conflict);
      else
        {
          std::vector<file_content_conflict>::iterator tmp = i;
          E(++tmp == conflicts.end(), origin::user,
            F("conflicts file does not match current conflicts"));
        }
    }
} // validate_file_content_conflicts

static void
read_conflict_file_core(basic_io::parser pars,
                        revision_id left_rid,
                        roster_t const & left_roster,
                        revision_id right_rid,
                        roster_t const & right_roster,
                        roster_merge_result & result,
                        bool validate)
{
  pars.esym (syms::conflict);

  // If we are validating, there must be one stanza in the file for each
  // conflict; otherwise something has changed since the file was
  // regenerated. So we go thru the conflicts in the same order they are
  // generated; see merge_content.cc resolve_merge_conflicts.

  if (validate)
    {
      // resolve_merge_conflicts should not call us if there are any conflicts
      // for which we don't currently support resolutions; assert that.

      I(!result.missing_root_conflict);
      I(result.invalid_name_conflicts.size() == 0);
      I(result.directory_loop_conflicts.size() == 0);
      I(result.multiple_name_conflicts.size() == 0);
      I(result.attribute_conflicts.size() == 0);

      // These are the ones we know how to resolve. They must be in the same
      // order as non-validate, below.

      validate_orphaned_node_conflicts(pars, result.orphaned_node_conflicts, left_roster, right_roster);
      validate_dropped_modified_conflicts
        (pars, result.dropped_modified_conflicts, left_rid, left_roster, right_rid, right_roster);
      validate_duplicate_name_conflicts(pars, result.duplicate_name_conflicts, left_roster, right_roster);
      validate_file_content_conflicts(pars, result.file_content_conflicts, left_roster, right_roster);
    }
  else
    {
      // Read in the ones we know how to resolve. Also read in the ones we
      // don't know how to resolve, so we can report them.
      read_missing_root_conflicts(pars, result.missing_root_conflict, left_roster, right_roster);
      read_invalid_name_conflicts(pars, result.invalid_name_conflicts, left_roster, right_roster);
      read_directory_loop_conflicts(pars, result.directory_loop_conflicts, left_roster, right_roster);
      read_orphaned_node_conflicts(pars, result.orphaned_node_conflicts, left_roster, right_roster);
      read_multiple_name_conflicts(pars, result.multiple_name_conflicts, left_roster, right_roster);
      read_dropped_modified_conflicts
        (pars, result.dropped_modified_conflicts, left_rid, left_roster, right_rid, right_roster);
      read_duplicate_name_conflicts(pars, result.duplicate_name_conflicts, left_roster, right_roster);
      read_attribute_conflicts(pars, result.attribute_conflicts, left_roster, right_roster);
      read_file_content_conflicts(pars, result.file_content_conflicts, left_roster, right_roster);
    }

  E(pars.tok.in.lookahead == EOF, pars.tok.in.made_from,
    F("extra data in file"));
} // read_conflict_file_core

void
roster_merge_result::read_conflict_file(database & db,
                                        bookkeeping_path const & file_name,
                                        revision_id & ancestor_rid,
                                        revision_id & left_rid,
                                        revision_id & right_rid,
                                        roster_t & left_roster,
                                        marking_map & left_marking,
                                        roster_t & right_roster,
                                        marking_map & right_marking)
{
  data dat;

  read_data (file_name, dat);

  basic_io::input_source src(dat(), file_name.as_external());
  src.made_from = origin::user;
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  std::string temp;

  // Read left, right, ancestor.
  pars.esym(syms::left);
  pars.hex(temp);
  left_rid = decode_hexenc_as<revision_id>(temp, src.made_from);
  pars.esym(syms::right);
  pars.hex(temp);
  right_rid = decode_hexenc_as<revision_id>(temp, src.made_from);

  if (pars.symp(syms::ancestor))
    {
      pars.sym();
      pars.hex(temp);
      ancestor_rid = decode_hexenc_as<revision_id>(temp, src.made_from);

      // we don't fetch the ancestor roster here, because not every function
      // needs it.
      db.get_roster(left_rid, left_roster, left_marking);
      db.get_roster(right_rid, right_roster, right_marking);

      read_conflict_file_core(pars, left_rid, left_roster, right_rid, right_roster, *this, false);
    }
  // else no conflicts

} // roster_merge_result::read_conflict_file

void
roster_merge_result::write_conflict_file(database & db,
                                         lua_hooks & lua,
                                         bookkeeping_path const & file_name,
                                         revision_id const & ancestor_rid,
                                         revision_id const & left_rid,
                                         revision_id const & right_rid,
                                         boost::shared_ptr<roster_t> left_roster,
                                         marking_map const & left_marking,
                                         boost::shared_ptr<roster_t> right_roster,
                                         marking_map const & right_marking)
{
  std::ostringstream output;

  content_merge_database_adaptor adaptor(db, left_rid, right_rid,
                                         left_marking, right_marking);

  adaptor.cache_roster (left_rid, left_roster);
  adaptor.cache_roster (right_rid, right_roster);
  {
    // match format in cmd_merging.cc show_conflicts_core
    basic_io::stanza st;
    basic_io::printer pr;
    st.push_binary_pair(syms::left, left_rid.inner());
    st.push_binary_pair(syms::right, right_rid.inner());
    st.push_binary_pair(syms::ancestor, adaptor.lca.inner());
    pr.print_stanza(st);
    output.write(pr.buf.data(), pr.buf.size());
  }

  report_missing_root_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_invalid_name_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_directory_loop_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_orphaned_node_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_multiple_name_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_dropped_modified_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_duplicate_name_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_attribute_conflicts(*left_roster, *right_roster, adaptor, true, output);
  report_file_content_conflicts(lua, *left_roster, *right_roster, adaptor, true, output);

  data dat(output.str(), origin::internal);
  write_data(file_name, dat);

} // roster_merge_result::write_conflict_file

void
parse_resolve_conflicts_opts (options const & opts,
                              revision_id const & left_rid,
                              roster_t const & left_roster,
                              revision_id const & right_rid,
                              roster_t const & right_roster,
                              roster_merge_result & result,
                              bool & resolutions_given)
{
  if (opts.resolve_conflicts)
    {
      resolutions_given = true;

      if (!file_exists(system_path(opts.resolve_conflicts_file)))
        {
          // user may specify --resolve-conflicts to enable attr
          // mtn:resolve_conflict, without _MTN/conflicts.
          return;
        }

      data dat;

      read_data (system_path(opts.resolve_conflicts_file), dat);

      basic_io::input_source src(dat(), opts.resolve_conflicts_file.as_external());
      src.made_from = origin::user;
      basic_io::tokenizer tok(src);
      basic_io::parser pars(tok);
      std::string temp;

      pars.esym(syms::left);
      pars.hex(temp);
      E(left_rid == decode_hexenc_as<revision_id>(temp, src.made_from),
        origin::user,
        F("left revision id does not match conflict file"));

      pars.esym(syms::right);
      pars.hex(temp);
      E(right_rid == decode_hexenc_as<revision_id>(temp, src.made_from),
        origin::user,
        F("right revision id does not match conflict file"));

      if (pars.symp(syms::ancestor))
        {
          pars.sym();
          pars.hex(temp);

          read_conflict_file_core (pars, left_rid, left_roster, right_rid, right_roster, result, true);
        }
      else
        {
          // if there is no ancestor revision, then left is an ancestor of
          // right, or vice versa, and there can be no conflicts.
        }
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

  E(!new_roster.has_node(target_path), origin::user,
    F("'%s' already exists") % target_path.as_external());
  E(new_roster.has_node(target_path.dirname()), origin::user,
    F("directory '%s' does not exist or is unknown") % target_path.dirname());

  new_roster.attach_node (nid, target_path);

  const_node_t node = new_roster.get_node (nid);
  for (attr_map_t::const_iterator attr = node->attrs.begin();
       attr != node->attrs.end();
       ++attr)
    lua.hook_set_attribute(attr->first(), target_path, attr->second.second());

} // attach_node

void
roster_merge_result::resolve_orphaned_node_conflicts(lua_hooks & lua,
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

  for (std::vector<orphaned_node_conflict>::const_iterator i = orphaned_node_conflicts.begin();
       i != orphaned_node_conflicts.end();
       ++i)
    {
      orphaned_node_conflict const & conflict = *i;
      MM(conflict);

      file_path name;

      if (left_roster.has_node(conflict.nid))
        {
          left_roster.get_name(conflict.nid, name);
        }
      else
        {
          right_roster.get_name(conflict.nid, name);
        }

      switch (conflict.resolution.resolution)
        {
        case resolve_conflicts::drop:
          if (is_dir_t(roster.get_node(conflict.nid)))
            {
              E(downcast_to_dir_t(roster.get_node(conflict.nid))->children.empty(), origin::user,
                F("can't drop directory '%s'; it is not empty") % name);
            }

          P(F("dropping '%s'") % name);
          roster.drop_detached_node(conflict.nid);
          break;

        case resolve_conflicts::rename:
          P(F("renaming '%s' to '%s'") % name % conflict.resolution.rename);
          attach_node (lua, roster, conflict.nid, conflict.resolution.rename);
          break;

        case resolve_conflicts::none:
          E(false, origin::user,
            F("no resolution provided for orphaned_node '%s'") % name);
          break;

        default:
          E(false, origin::user,
            F("invalid resolution for orphaned_node '%s'") % name);
        }
    } // end for

  orphaned_node_conflicts.clear();
}

static node_id
create_new_node(roster_t const &            parent_roster,
                string const &              side_image,
                node_id const &             parent_nid,
                roster_t &                  result_roster,
                boost::shared_ptr<any_path> new_content,
                content_merge_adaptor &     adaptor,
                temp_node_id_source &       nis)
{
  file_path parent_name;
  file_id   parent_fid;
  file_data parent_data;

  parent_roster.get_file_details(parent_nid, parent_fid, parent_name);
  adaptor.get_version(parent_fid, parent_data);

  P(F("replacing content of '%s' from %s with '%s'") % parent_name % side_image % new_content->as_external());

  // FIXME: factor out 'history lost' msg
  P(F("history for '%s' from %s will be lost; see user manual Merge Conflicts section") % parent_name % side_image);

  data result_raw_data;
  read_data(*new_content, result_raw_data);

  file_data result_data = file_data(result_raw_data);
  file_id result_fid;
  calculate_ident(result_data, result_fid);

  // User could specify no changes in content
  if (result_fid != parent_fid)
    {
      adaptor.record_file(parent_fid, result_fid, parent_data, result_data);
    }

  return result_roster.create_file_node(result_fid, nis);
}

static void
replace_content(roster_t const &            parent_roster,
                string const &              side_image,
                node_id  const &            nid,
                roster_t &                  result_roster,
                boost::shared_ptr<any_path> new_content,
                content_merge_adaptor &     adaptor)
{
  file_path parent_name;
  file_id   parent_fid;

  parent_roster.get_file_details(nid, parent_fid, parent_name);

  P(F("replacing content of '%s' from %s with '%s'") % parent_name % side_image % new_content->as_external());

  file_data parent_data;
  adaptor.get_version(parent_fid, parent_data);

  data result_raw_data;
  read_data(*new_content, result_raw_data);

  file_data result_data = file_data(result_raw_data);
  file_id result_fid;
  calculate_ident(result_data, result_fid);

  file_t result_node = downcast_to_file_t(result_roster.get_node_for_update(nid));
  result_node->content = result_fid;

  // User could specify no changes in content
  if (result_fid != parent_fid)
    {
      adaptor.record_file(parent_fid, result_fid, parent_data, result_data);
    }
}

static void
resolve_dropped_modified_one(lua_hooks &                                  lua,
                             string                                       side_image,
                             bool                                         handling_dropped_side,
                             resolve_conflicts::file_resolution_t const & resolution,
                             resolve_conflicts::file_resolution_t const & other_resolution,
                             roster_t const &                             side_roster,
                             file_path const &                            name,
                             file_id const &                              fid,
                             node_id const                                nid,
                             content_merge_database_adaptor &             adaptor,
                             temp_node_id_source &                        nis,
                             roster_t &                                   result_roster)
{
  if (nid == the_null_node)
    {
      E(resolution.resolution == resolve_conflicts::none, origin::user,
        F("extra %s_resolution provided for dropped_modified '%s'") % side_image % name);
      return;
    }
  else
    {
      E(resolution.resolution != resolve_conflicts::none, origin::user,
        (other_resolution.resolution == resolve_conflicts::none) ?
        F("no resolution provided for dropped_modified '%s'") % name :
        F("no %s_resolution provided for dropped_modified '%s'") % side_image % name);
    }

  switch (resolution.resolution)
    {
    case resolve_conflicts::none:
      // handled above; can't get here
      break;

    case resolve_conflicts::content_user:
      // FIXME: check other_resolution for consistency
      if (handling_dropped_side)
        {
          // recreated; replace the contents of the recreated node
          replace_content(side_roster, side_image, nid, result_roster, resolution.content, adaptor);
          attach_node(lua, result_roster, nid, name);
        }
      else
        {
          // modified; drop and create a new node
          // See comments in keep below on why we drop first
          result_roster.drop_detached_node(nid);

          node_id new_nid = create_new_node
            (side_roster, side_image, nid, result_roster, resolution.content, adaptor, nis);

          attach_node(lua, result_roster, new_nid, name);
        }
      break;

    case resolve_conflicts::content_internal:
      // not valid for dropped_modified
      I(false);

    case resolve_conflicts::drop:
      // The node is either modified, recreated or duplicate name; in
      // any case, it is present but detached in the result roster, so drop it
      P(F("dropping '%s' from %s") % name % side_image);
      result_roster.drop_detached_node(nid);
      break;

    case resolve_conflicts::keep:
      if (handling_dropped_side)
        {
          // recreated; keep the recreated contents
          P(F("keeping '%s' from %s") % name % side_image);
          attach_node(lua, result_roster, nid, name);
        }
      else
        {
          // modified; keep the modified contents

          P(F("keeping '%s' from %s") % name % side_image);
          P(F("history for '%s' from %s will be lost; see user manual Merge Conflicts section") %
            name % side_image);

          // We'd like to just attach_node here, but that violates a
          // fundamental design principle of mtn; nodes are born once,
          // and die once. If we attach here, the node is born, died,
          // and then born again.
          //
          // So we have to drop the old node, and create a new node with
          // the same contents. That loses history; 'mtn log <path>'
          // will end here, not showing the history of the original
          // node.
          result_roster.drop_detached_node(nid);
          node_id nid = result_roster.create_file_node(fid, nis);
          attach_node (lua, result_roster, nid, name);
        }
      break;

    case resolve_conflicts::rename:
      if (handling_dropped_side)
        {
          // recreated; rename the recreated contents
          P(F("renaming '%s' from %s to '%s'") % name % side_image % resolution.rename.as_external());
          attach_node(lua, result_roster, nid, resolution.rename);
        }
      else
        {
          // modified; drop, create new node with the modified contents, rename
          // See comment in keep above on why we drop first.
          result_roster.drop_detached_node(nid);

          P(F("renaming '%s' from %s to '%s'") % name % side_image % resolution.rename.as_external());
          P(F("history for '%s' from %s will be lost; see user manual Merge Conflicts section") % name % side_image);

          node_id new_nid = result_roster.create_file_node(fid, nis);
          attach_node (lua, result_roster, new_nid, resolution.rename);
        }
      break;

    case resolve_conflicts::content_user_rename:
      if (handling_dropped_side)
        {
          // recreated; rename and replace the recreated contents
          replace_content(side_roster, side_image, nid, result_roster, resolution.content, adaptor);

          P(F("renaming '%s' from %s to '%s'") % name % side_image % resolution.rename.as_external());

          attach_node (lua, result_roster, nid, resolution.rename);
        }
      else
        {
          // modified; drop, rename and replace the modified contents
          result_roster.drop_detached_node(nid);

          node_id new_nid = create_new_node
            (side_roster, side_image, nid, result_roster, resolution.content, adaptor, nis);

          P(F("renaming '%s' from %s to '%s'") % name % side_image % resolution.rename.as_external());

          attach_node(lua, result_roster, new_nid, resolution.rename);
        }
      break;
    }
} // resolve_dropped_modified_one

void
roster_merge_result::resolve_dropped_modified_conflicts(lua_hooks & lua,
                                                        roster_t const & left_roster,
                                                        roster_t const & right_roster,
                                                        content_merge_database_adaptor & adaptor,
                                                        temp_node_id_source & nis)
{
  MM(left_roster);
  MM(right_roster);
  MM(this->roster); // New roster

  for (std::vector<dropped_modified_conflict>::const_iterator i = dropped_modified_conflicts.begin();
       i != dropped_modified_conflicts.end();
       ++i)
    {
      dropped_modified_conflict const & conflict = *i;
      MM(conflict);

      file_path left_name;
      file_id   left_fid;
      file_path right_name;
      file_id   right_fid;

      if (conflict.left_nid != the_null_node)
        {
          if (conflict.left_rid == adaptor.left_rid)
            {
              left_roster.get_file_details(conflict.left_nid, left_fid, left_name);
            }
          else
            {
              roster_t tmp;
              adaptor.db.get_roster(conflict.left_rid, tmp);
              tmp.get_file_details(conflict.left_nid, left_fid, left_name);
            }
        }

      if (conflict.right_nid != the_null_node)
        {
          if (conflict.right_rid == adaptor.right_rid)
            {
              right_roster.get_file_details(conflict.right_nid, right_fid, right_name);
            }
          else
            {
              roster_t tmp;
              adaptor.db.get_roster(conflict.right_rid, tmp);
              tmp.get_file_details(conflict.right_nid, right_fid, right_name);
            }
        }

      resolve_dropped_modified_one (lua,
                                    string("left"),
                                    conflict.dropped_side == resolve_conflicts::left_side,
                                    conflict.left_resolution,
                                    conflict.right_resolution,
                                    left_roster,
                                    left_name,
                                    left_fid,
                                    conflict.left_nid,
                                    adaptor,
                                    nis,
                                    roster);

      resolve_dropped_modified_one (lua,
                                    string("right"),
                                    conflict.dropped_side == resolve_conflicts::right_side,
                                    conflict.right_resolution,
                                    conflict.left_resolution,
                                    right_roster,
                                    right_name,
                                    right_fid,
                                    conflict.right_nid,
                                    adaptor,
                                    nis,
                                    roster);

    } // end for

  dropped_modified_conflicts.clear();
}

static void
resolve_duplicate_name_one_side(lua_hooks & lua,
                                resolve_conflicts::file_resolution_t const & resolution,
                                resolve_conflicts::file_resolution_t const & other_resolution,
                                file_path const & name,
                                file_id const & fid,
                                node_id const nid,
                                content_merge_adaptor & adaptor,
                                roster_t & result_roster)
{
  switch (resolution.resolution)
    {
    case resolve_conflicts::content_user:
      {
        E(other_resolution.resolution == resolve_conflicts::drop ||
          other_resolution.resolution == resolve_conflicts::rename,
          origin::user,
          F("inconsistent left/right resolutions for '%s'") % name);

        P(F("replacing content of '%s' with '%s'") % name % resolution.content->as_external());

        file_id result_fid;
        file_data parent_data, result_data;
        data result_raw_data;
        adaptor.get_version(fid, parent_data);

        read_data(*resolution.content, result_raw_data);

        result_data = file_data(result_raw_data);
        calculate_ident(result_data, result_fid);

        file_t result_node = downcast_to_file_t(result_roster.get_node_for_update(nid));
        result_node->content = result_fid;

        adaptor.record_file(fid, result_fid, parent_data, result_data);

        attach_node(lua, result_roster, nid, name);
      }
      break;

    case resolve_conflicts::drop:
      P(F("dropping '%s'") % name);

      if (is_dir_t(result_roster.get_node(nid)))
        {
          const_dir_t n = downcast_to_dir_t(result_roster.get_node(nid));
          E(n->children.empty(), origin::user, F("can't drop '%s'; not empty") % name);
        }
      result_roster.drop_detached_node(nid);
      break;

    case resolve_conflicts::keep:
      E(other_resolution.resolution == resolve_conflicts::drop ||
        other_resolution.resolution == resolve_conflicts::rename,
        origin::user,
        F("inconsistent left/right resolutions for '%s'") % name);

      P(F("keeping '%s'") % name);
      attach_node (lua, result_roster, nid, name);
      break;

    case resolve_conflicts::rename:
      P(F("renaming '%s' to '%s'") % name % resolution.rename);
      attach_node (lua, result_roster, nid, resolution.rename);
      break;

    case resolve_conflicts::none:
      E(false, origin::user,
        F("no resolution provided for duplicate_name '%s'") % name);
      break;

    default:
      I(false);
    }
} // resolve_duplicate_name_one_side

void
roster_merge_result::resolve_duplicate_name_conflicts(lua_hooks & lua,
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
      file_id left_fid, right_fid;

      if (is_file_t(left_roster.get_node(left_nid)))
        {
          left_roster.get_file_details(left_nid, left_fid, left_name);
        }
      else
        {
          left_roster.get_name(left_nid, left_name);
        }

      if (is_file_t(right_roster.get_node(right_nid)))
        {
          right_roster.get_file_details(right_nid, right_fid, right_name);
        }
      else
        {
          right_roster.get_name(right_nid, right_name);
        }

      resolve_duplicate_name_one_side
        (lua, conflict.left_resolution, conflict.right_resolution, left_name, left_fid, left_nid, adaptor, roster);

      resolve_duplicate_name_one_side
        (lua, conflict.right_resolution, conflict.left_resolution, right_name, right_fid, right_nid, adaptor, roster);
    } // end for

  duplicate_name_conflicts.clear();
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

      left_roster.get_name(conflict.nid, left_name);
      right_roster.get_name(conflict.nid, right_name);

      switch (conflict.resolution.resolution)
        {
          case resolve_conflicts::content_internal:
          case resolve_conflicts::none:
            {
              file_id merged_id;

              E(resolve_conflicts::do_auto_merge(lua, conflict, adaptor, left_roster,
                                                 right_roster, this->roster, merged_id),
                origin::user,
                F("merge of '%s', '%s' failed") % left_name % right_name);

              P(F("merged '%s', '%s'") % left_name % right_name);

              file_t result_node = downcast_to_file_t(roster.get_node_for_update(conflict.nid));
              result_node->content = merged_id;
            }
            break;

          case resolve_conflicts::content_user:
            {
              P(F("replacing content of '%s', '%s' with '%s'") %
                left_name % right_name % conflict.resolution.content->as_external());

              file_id result_id;
              file_data left_data, right_data, result_data;
              data result_raw_data;
              adaptor.get_version(conflict.left, left_data);
              adaptor.get_version(conflict.right, right_data);

              read_data(*conflict.resolution.content, result_raw_data);

              result_data = file_data(result_raw_data);
              calculate_ident(result_data, result_id);

              file_t result_node = downcast_to_file_t(roster.get_node_for_update(conflict.nid));
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
