// Copyright (C) 2007 Lapo Luchini <lapo@lapo.it>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __ASCIIK_HH__
#define __ASCIIK_HH__

#include <set>
#include "vector.hh"
#include "vocab.hh"

class asciik
{
public:
  asciik(std::ostream & os, size_t min_width = 0);
  // Prints an ASCII-k chunk using the given revisions.
  // Multiple lines are supported in annotation (the graph will stretch
  // accordingly); empty newlines at the end will be removed.
  void print(revision_id const & rev,
             std::set<revision_id> const & parents,
             std::string const & annotation);
  //TODO: change set-of-parents to vector-of-successors
private:
  void links_cross(std::set<std::pair<size_t, size_t> > const & links,
                   std::set<size_t> & crosses) const;
  void draw(size_t const curr_items,
            size_t const next_items,
            size_t const curr_loc,
            std::set<std::pair<size_t, size_t> > const & links,
            std::set<size_t> const & curr_ghosts,
            std::string const & annotation) const;
  bool try_draw(std::vector<revision_id> const & next_row,
                size_t const curr_loc,
                std::set<revision_id> const & parents,
                std::string const & annotation) const;
  // internal state
  size_t width;
  std::ostream & output;
  std::vector<revision_id> curr_row;
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
