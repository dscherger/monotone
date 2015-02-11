// Copyright (C) 2010 Derek Scherger <derek@echologic.com>
//               2015 Markus Wanner <markus@bluegap.ch>
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
#include <vector>

#include "cert.hh"
#include "cset.hh"
#include "dates.hh"
#include "parallel_iter.hh"
#include "project.hh"
#include "rev_output.hh"
#include "revision.hh"
#include "roster.hh"
#include "transforms.hh"

using std::map;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

void
revision_header(revision_id const rid, revision_t const & rev,
                string const & author, date_t const date,
                branch_name const & branch, utf8 const & changelog,
                string const & date_fmt, colorizer const & color,
                utf8 & header)
{
  vector<cert> certs;
  key_id empty_key;
  certs.push_back(cert(rid, author_cert_name,
                       cert_value(author, origin::user), empty_key));
  certs.push_back(cert(rid, date_cert_name,
                       cert_value(date.as_iso_8601_extended(), origin::user),
                       empty_key));
  certs.push_back(cert(rid, branch_cert_name,
                       cert_value(branch(), origin::user), empty_key));

  if (!changelog().empty())
    certs.push_back(cert(rid, changelog_cert_name,
                         cert_value(changelog(), origin::user), empty_key));

  revision_header(rid, rev, certs, date_fmt, color, header);
}

void
revision_header(revision_id const rid, revision_t const & rev,
                vector<cert> const & certs, string const & date_fmt,
                colorizer const & color, utf8 & header)
{
  ostringstream out;

  out << color.colorize(string(70, '-'), colorizer::separator) << '\n'
      << color.colorize(_("Revision: "), colorizer::rev_header)
      << color.colorize(encode_hexenc(rid.inner()(),
                                      rid.inner().made_from),
                        colorizer::rev_id) << '\n';

  for (edge_entry const & e : rev.edges)
    {
      revision_id parent = edge_old_revision(e);
      if (!null_id(parent))
        out << color.colorize(_("Parent:   "), colorizer::rev_header)
            << parent << '\n';
    }

  cert_name const author(author_cert_name);
  cert_name const date(date_cert_name);
  cert_name const branch(branch_cert_name);
  cert_name const tag(tag_cert_name);
  cert_name const changelog(changelog_cert_name);
  cert_name const comment(comment_cert_name);

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    if (i->name == author)
      out << color.colorize(_("Author:   "), colorizer::rev_header)
          << i->value << '\n';

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    if (i->name == date)
      {
        if (date_fmt.empty())
          out << color.colorize(_("Date:     "), colorizer::rev_header)
              << i->value << '\n';
        else
          {
            date_t date(i->value());
            out << color.colorize(_("Date:     "), colorizer::rev_header)
                << date.as_formatted_localtime(date_fmt) << '\n';
          }
      }

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    if (i->name == branch)
      out << color.colorize(_("Branch:   "), colorizer::rev_header)
          << color.colorize(i->value(), colorizer::branch) << '\n';

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    if (i->name == tag)
      out << color.colorize(_("Tag:      "), colorizer::rev_header)
          << i->value << '\n';

  // Output "custom" certs if we have any, under a heading of "Other certs"
  bool need_to_output_heading = true;
  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      if (i->name != author && i->name != branch && i->name != changelog &&
          i->name != comment && i->name != date && i->name != tag)
        {
          if (need_to_output_heading)
            {
              out << color.colorize(_("Other certs:"),
                                    colorizer::rev_header) << '\n';
              need_to_output_heading = false;
            }

          out << "  " << i->name << ": " << i->value << '\n';
        }
    }

  out << "\n";

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    if (i->name == changelog)
      {
        out << color.colorize(_("Changelog: "), colorizer::rev_header)
            << "\n\n" << i->value << '\n';
        if (!i->value().empty() && i->value()[i->value().length()-1] != '\n')
          out << '\n';
      }

  for (vector<cert>::const_iterator i = certs.begin(); i != certs.end(); ++i)
    if (i->name == comment)
      {
        out << color.colorize(_("Comments: "), colorizer::rev_header)
            << "\n\n" << i->value << '\n';
        if (!i->value().empty() && i->value()[i->value().length()-1] != '\n')
          out << '\n';
      }

  header = utf8(out.str(), origin::internal);
}

void
cset_summary(cset const & cs, colorizer const & color, ostringstream & out)
{
  // We intentionally do not collapse the final \n into the format
  // strings here, for consistency with newline conventions used by most
  // other format strings.

  // presumably a merge rev could have an empty edge if one side won
  if (cs.empty())
    out << _("  no changes") << '\n';

  for (file_path const & path : cs.nodes_deleted)
    out << (F("  dropped  %s")
            % color.colorize((F("%s") % path).str(),
                             colorizer::remove)).str()
        << '\n';

  for (pair<file_path, file_path> const & p : cs.nodes_renamed)
    out << (F("  renamed  %s\n"
              "       to  %s")
            % color.colorize((F("%s") % p.first).str(),
                             colorizer::rename)
            % color.colorize((F("%s") % p.second).str(),
                             colorizer::rename)).str()
        << '\n';

  for (file_path const & path : cs.dirs_added)
    out << (F("  added    %s")
            % color.colorize((F("%s") % path).str(), colorizer::add)).str()
        << '\n';

  for (pair<file_path, file_id> const & p : cs.files_added)
    out << (F("  added    %s")
            % color.colorize((F("%s") % p.first).str(),
                             colorizer::add)).str()
        << '\n';

  for (pair<file_path, pair<file_id, file_id>> const & p : cs.deltas_applied)
    out << (F("  patched  %s")
            % color.colorize((F("%s") % p.first).str(),
                             colorizer::change)).str()
        << '\n';

  for (pair<pair<file_path, attr_key>, attr_value> const & p : cs.attrs_set)
    out << (F("  attr on  %s\n"
              "      set  %s\n"
              "       to  %s")
            % p.first.first
            % color.colorize(p.first.second(), colorizer::add)
            % color.colorize(p.second(), colorizer::add)).str() << '\n';

  // FIXME: naming here could not be more inconsistent:
  //  * the cset calls it attrs_cleared
  //  * the command is attr drop
  //  * here it is called unset
  //  * the revision text uses attr clear

  for (pair<file_path, attr_key> const & p : cs.attrs_cleared)
    out << (F("  attr on  %s\n"
              "    unset  %s")
            % p.first
            % color.colorize(p.second(), colorizer::remove)).str() << '\n';

  out << '\n';
}

void
revision_summary(revision_t const & rev, colorizer const & color,
                 utf8 & summary)
{
  ostringstream out;
  revision_id rid = calculate_ident(rev);

  for (edge_entry const & edge : rev.edges)
    {
      revision_id parent = edge_old_revision(edge);
      cset const & cs = edge_changes(edge);

      // A colon at the end of this string looked nicer, but it made
      // double-click copying from terminals annoying.
      if (null_id(parent))
        out << _("Changes") << "\n\n";
      else
        out << _("Changes against parent ")
            << color.colorize(encode_hexenc(parent.inner()(),
                                            parent.inner().made_from),
                              colorizer::rev_id) << "\n\n";

      cset_summary(cs, color, out);
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
