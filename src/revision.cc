// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <memory>

#include "revision.hh"
#include "roster.hh"

#include "sanity.hh"
#include "basic_io.hh"
#include "transforms.hh"

#include "safe_map.hh"

using std::move;
using std::swap;
using std::make_pair;
using std::make_shared;
using std::map;
using std::string;
using std::shared_ptr;

void revision_t::check_sane() const
{
  E(!null_id(new_manifest), made_from, F("revision has no manifest id"));

  if (edges.size() == 1)
    {
      // no particular checks to be done right now
    }
  else if (edges.size() == 2)
    {
      // merge nodes cannot have null revisions
      for (edge_map::const_iterator i = edges.begin(); i != edges.end(); ++i)
        E(!null_id(edge_old_revision(i)), made_from,
          F("merge revision has a null parent"));
    }
  else
    // revisions must always have either 1 or 2 edges
    E(false, made_from, F("revision has %d edges, not 1 or 2") % edges.size());

  // we used to also check that if there were multiple edges that had patches
  // for the same file, then the new hashes on each edge matched each other.
  // this is not ported over to roster-style revisions because it's an
  // inadequate check, and the real check, that the new manifest id is correct
  // (done in put_revision, for instance) covers this case automatically.
}

bool
revision_t::is_merge_node() const
{
  return edges.size() > 1;
}

bool
revision_t::is_nontrivial() const
{
  check_sane();
  // merge revisions are never trivial, because even if the resulting node
  // happens to be identical to both parents, the merge is still recording
  // that fact.
  if (is_merge_node())
    return true;
  else
    return !edge_changes(edges.begin()).empty();
}

revision_t
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              roster_t const & new_roster)
{
  shared_ptr<cset> cs = make_shared<cset>(old_roster, new_roster);

  revision_t rev;
  rev.new_manifest = calculate_ident(new_roster);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);

  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.made_for = made_for_database;

  return rev;
}

revision_t
make_revision(revision_id const & old_rev_id,
              roster_t const & old_roster,
              cset && changes)
{
  roster_t new_roster = old_roster;
  {
    temp_node_id_source nis;
    editable_roster_base er(new_roster, nis);
    changes.apply_to(er);
  }

  shared_ptr<cset> cs = make_shared<cset>(move(changes));

  revision_t rev;
  rev.new_manifest = calculate_ident(new_roster);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);

  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.made_for = made_for_database;

  return rev;
}

revision_t
make_revision(parent_map const & old_rosters,
              roster_t const & new_roster)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> cs = make_shared<cset>(parent_roster(i), new_roster);
      safe_insert(edges, make_pair(parent_id(i), cs));
    }

  revision_t rev;
  rev.edges = move(edges);
  rev.new_manifest = calculate_ident(new_roster);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);

  return rev;
}

static revision_t
recalculate_manifest_id_for_restricted_rev(parent_map const & old_rosters,
                                           edge_map && edges)
{
  // In order to get the correct manifest ID, recalculate the new roster
  // using one of the restricted csets.  It doesn't matter which of the
  // parent roster/cset pairs we use for this; by construction, they must
  // all produce the same result.
  revision_id rid = parent_id(old_rosters.begin());
  roster_t restricted_roster = *(safe_get(old_rosters, rid).first);

  temp_node_id_source nis;
  editable_roster_base er(restricted_roster, nis);
  safe_get(edges, rid)->apply_to(er);

  revision_t rev;
  rev.new_manifest = calculate_ident(restricted_roster);
  rev.edges = move(edges);

  if (global_sanity.debug_p())
    L(FL("new manifest_id is %s")
      % rev.new_manifest);

  return rev;
}

revision_t
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      roster_t restricted_roster;
      make_restricted_roster(parent_roster(i), new_roster,
                             restricted_roster, mask);

      shared_ptr<cset> included(make_shared<cset>(parent_roster(i),
                                                  restricted_roster));
      safe_insert(edges, make_pair(parent_id(i), included));
    }

  return recalculate_manifest_id_for_restricted_rev(old_rosters,
                                                    move(edges));
}

revision_t
make_restricted_revision(parent_map const & old_rosters,
                         roster_t const & new_roster,
                         node_restriction const & mask,
                         cset & excluded,
                         utf8 const & cmd_name)
{
  edge_map edges;
  bool no_excludes = true;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      roster_t restricted_roster;
      make_restricted_roster(parent_roster(i), new_roster,
                             restricted_roster, mask);

      shared_ptr<cset> included(make_shared<cset>(parent_roster(i),
                                                  restricted_roster));
      excluded = cset(restricted_roster, new_roster);
      safe_insert(edges, make_pair(parent_id(i), included));
      if (!excluded.empty())
        no_excludes = false;
    }

  E(old_rosters.size() == 1 || no_excludes, origin::user,
    F("the command '%s %s' cannot be restricted in a two-parent workspace")
    % prog_name % cmd_name);

  return recalculate_manifest_id_for_restricted_rev(old_rosters,
                                                    move(edges));
}

// Workspace-only revisions, with fake rev.new_manifest and content
// changes suppressed.
revision_t
make_revision_for_workspace(revision_id const & old_rev_id,
                            cset && changes)
{
  MM(old_rev_id);
  MM(changes);
  shared_ptr<cset> cs = make_shared<cset>(move(changes));
  cs->deltas_applied.clear();

  revision_t rev;
  MM(rev);
  safe_insert(rev.edges, make_pair(old_rev_id, cs));
  rev.new_manifest = manifest_id(fake_id());
  rev.made_for = made_for_workspace;
  return rev;
}

revision_t
make_revision_for_workspace(revision_id const & old_rev_id,
                            roster_t const & old_roster,
                            roster_t const & new_roster)
{
  MM(old_rev_id);
  MM(old_roster);
  MM(new_roster);
  return make_revision_for_workspace(old_rev_id,
                                     cset(old_roster, new_roster));
}

revision_t
make_revision_for_workspace(parent_map const & old_rosters,
                            roster_t const & new_roster)
{
  edge_map edges;
  for (parent_map::const_iterator i = old_rosters.begin();
       i != old_rosters.end();
       i++)
    {
      shared_ptr<cset> cs = make_shared<cset>(parent_roster(i), new_roster);
      cs->deltas_applied.clear();
      safe_insert(edges, make_pair(parent_id(i), cs));
    }

  revision_t rev;
  rev.edges = move(edges);
  rev.new_manifest = manifest_id(fake_id());
  rev.made_for = made_for_workspace;
  return rev;
}

// i/o stuff

namespace
{
  namespace syms
  {
    symbol const format_version("format_version");
    symbol const old_revision("old_revision");
    symbol const new_manifest("new_manifest");
  }
}

void
print_edge(basic_io::printer & printer,
           edge_entry const & e)
{
  basic_io::stanza st;
  st.push_binary_pair(syms::old_revision, edge_old_revision(e).inner());
  printer.print_stanza(st);
  print_cset(printer, edge_changes(e));
}

static void
print_insane_revision(basic_io::printer & printer,
                      revision_t const & rev)
{

  basic_io::stanza format_stanza;
  format_stanza.push_str_pair(syms::format_version, "1");
  printer.print_stanza(format_stanza);

  basic_io::stanza manifest_stanza;
  manifest_stanza.push_binary_pair(syms::new_manifest, rev.new_manifest.inner());
  printer.print_stanza(manifest_stanza);

  for (edge_map::const_iterator edge = rev.edges.begin();
       edge != rev.edges.end(); ++edge)
    print_edge(printer, *edge);
}

void
print_revision(basic_io::printer & printer,
               revision_t const & rev)
{
  rev.check_sane();
  print_insane_revision(printer, rev);
}


void
parse_edge(basic_io::parser & parser,
           revision_t & rev)
{
  manifest_id old_man;
  revision_id old_rev;
  string tmp;

  parser.esym(syms::old_revision);
  parser.hex(tmp);
  old_rev = decode_hexenc_as<revision_id>(tmp, parser.tok.in.made_from);

  shared_ptr<cset> cs = make_shared<cset>(parse_cset(parser));
  rev.edges.insert(make_pair(old_rev, cs));
}


revision_t
parse_revision(basic_io::parser & parser)
{
  revision_t rev;
  MM(rev);
  rev.edges.clear();
  rev.made_for = made_for_database;
  rev.made_from = parser.tok.in.made_from;
  string tmp;
  parser.esym(syms::format_version);
  parser.str(tmp);
  E(tmp == "1", parser.tok.in.made_from,
    F("encountered a revision with unknown format, version %s.\n"
      "I only know how to understand the version 1 format.\n"
      "A newer version of monotone is required to complete this operation")
    % tmp);
  parser.esym(syms::new_manifest);
  parser.hex(tmp);
  rev.new_manifest = decode_hexenc_as<manifest_id>(tmp,
                                                   parser.tok.in.made_from);
  while (parser.symp(syms::old_revision))
    parse_edge(parser, rev);
  rev.check_sane();
  return rev;
}

revision_t
read_revision(revision_data const & dat)
{
  revision_t rev;
  MM(rev);
  basic_io::input_source src(dat.inner()(), "revision");
  src.made_from = dat.inner().made_from;
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  rev = parse_revision(pars);
  E(src.lookahead == EOF, rev.made_from,
    F("failed to parse revision"));
  return rev;
}

static data write_insane_revision(revision_t const & rev)
{
  basic_io::printer pr;
  print_insane_revision(pr, rev);
  return data(pr.buf, origin::internal);
}

template <> void
dump(revision_t const & rev, string & out)
{
  out = write_insane_revision(rev)();
}

revision_data
write_revision(revision_t const & rev)
{
  rev.check_sane();
  return revision_data(write_insane_revision(rev));
}

revision_id
calculate_ident(revision_t const & rev)
{
  revision_data tmp = write_revision(rev);
  return revision_id(calculate_ident(tmp));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
