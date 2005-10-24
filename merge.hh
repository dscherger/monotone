#ifndef __MERGE_HH__
#define __MERGE_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <set>

#include "app_state.hh"
#include "vocab.hh"
#include "roster.hh"

// traditional resolve-all-conflicts-as-you-go style merging with 3-way merge
//   for file texts
// aborts if merge fails
// writes out resulting revision to the db, along with author and date certs
//   (but _not_ branch or changelog certs)
// this version can only be used to merge revisions that are in the db, and
//   that are written straight back out to the db; some refactoring would
//   probably be good
// 'update' requires some slightly different interface, to deal with the gunk
//   around the revision and its files not being in the db, and the resulting
//   revision and its merged files not being written back to the db
void
interactive_merge_and_store(revision_id const & left, revision_id const & right,
                            revision_id & merged, app_state & app);

#endif
