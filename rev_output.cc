// Copyright (C) 2010 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <map>
#include <set>
#include <sstream>

#include "cset.hh"
#include "dates.hh"
#include "rev_output.hh"
#include "revision.hh"

using std::map;
using std::pair;
using std::set;
using std::string;
using std::ostringstream;

void
revision_header(revision_id const rid, revision_t const & rev,
                string const & author, date_t const date,
                branch_name const & branch, utf8 & header)
{
  ostringstream out;
  int const width = 70;

  out << string(width, '-') << '\n'
      << _("Revision: ") << rid << _("       (uncommitted)") << '\n';

  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      revision_id parent = edge_old_revision(*i);
      out << _("Parent: ") << parent << '\n';
    }

  out << _("Author: ") << author << '\n'
      << _("Date: ") << date << '\n'
      << _("Branch: ") << branch << '\n'
      << _("ChangeLog: ") << "\n\n";

  header = utf8(out.str(), origin::internal);
}

void
revision_summary(revision_t const & rev, utf8 & summary)
{
  // We intentionally do not collapse the final \n into the format
  // strings here, for consistency with newline conventions used by most
  // other format strings.

  ostringstream out;
  revision_id rid;
  calculate_ident(rev, rid);

  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); ++i)
    {
      revision_id parent = edge_old_revision(*i);
      cset const & cs = edge_changes(*i);

      out << '\n';

      // A colon at the end of this string looked nicer, but it made
      // double-click copying from terminals annoying.
      if (!null_id(parent))
        out << _("Changes against parent ") << parent << "\n\n";

      // presumably a merge rev could have an empty edge if one side won
      if (cs.empty())
        out << _("no changes") << '\n';

      for (set<file_path>::const_iterator i = cs.nodes_deleted.begin();
            i != cs.nodes_deleted.end(); ++i)
        out << _("  dropped  ") << *i << '\n';

      for (map<file_path, file_path>::const_iterator
            i = cs.nodes_renamed.begin();
            i != cs.nodes_renamed.end(); ++i)
        out << _("  renamed  ") << i->first
            << _("       to  ") << i->second << '\n';

      for (set<file_path>::const_iterator i = cs.dirs_added.begin();
            i != cs.dirs_added.end(); ++i)
        out << _("  added    ") << *i << '\n';

      for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
            i != cs.files_added.end(); ++i)
        out << _("  added    ") << i->first << '\n';

      for (map<file_path, pair<file_id, file_id> >::const_iterator
              i = cs.deltas_applied.begin(); i != cs.deltas_applied.end(); ++i)
        out << _("  patched  ") << i->first << '\n';

      for (map<pair<file_path, attr_key>, attr_value >::const_iterator
             i = cs.attrs_set.begin(); i != cs.attrs_set.end(); ++i)
        out << _("  attr on  ") << i->first.first << '\n'
            << _("      set  ") << i->first.second << '\n'
            << _("       to  ") << i->second << '\n';

      // FIXME: naming here could not be more inconsistent
      // the cset calls it attrs_cleared
      // the command is attr drop
      // here it is called unset
      // the revision text uses attr clear 

      for (set<pair<file_path, attr_key> >::const_iterator
             i = cs.attrs_cleared.begin(); i != cs.attrs_cleared.end(); ++i)
        out << _("  attr on  ") << i->first << '\n'
            << _("    unset  ") << i->second << '\n';
    }
  summary = utf8(out.str(), origin::internal);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
