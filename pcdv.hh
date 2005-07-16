#ifndef __PCDV_HH__
#define __PCDV_HH__

#include <vector>
#include <string>
#include <map>
#include <set>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include "interner.hh"

using std::vector;
using std::string;
using std::map;
using std::pair;
using std::make_pair;
using std::set;


struct merge_section
{
  bool split;
  vector<string> left;
  vector<string> right;

  merge_section(string const & s):
  split(false)
  {
    left.push_back(s);
  }

  merge_section(vector<string> const & c):
  split(false), left(c) {}

  merge_section(vector<string> const & l, vector<string> const & r):
  split(true), left(l), right(r) {}

  bool
  operator==(merge_section const & other) const
  {
    return split == other.split && left == other.left && right == other.right;
  }
};

vector<merge_section>
consolidate(vector<merge_section> const & in);

void
show_conflict(vector<merge_section> const & result);

typedef int revid;
typedef int line_contents;

// This is a const object type; there are no modifiers.
// There are likely to be many, many copies of each object. Since objects
// don't change, share internal data between copies.
struct living_status
{
  typedef map<revid, vector<revid> > line_data;
  // Shared for all versions of a given line
  boost::shared_ptr<line_data> overrides;
  // Shared for all copies of this version of this line
  boost::shared_ptr<vector<revid> > leaves;
  boost::shared_ptr<pair<bool, bool> > precomp;

  living_status();
  living_status(boost::shared_ptr<line_data> ovr);
  living_status(living_status const & x);

  living_status const &
  operator=(living_status const & x);

  ~living_status();

  living_status const
  new_version(vector<revid> const & _leaves) const;

  living_status const
  new_version(vector<revid> const & _leaves, bool living_hint) const;

  living_status
  merge(living_status const & other) const;

  bool
  is_living() const;

  bool
  _makes_living(revid key) const;

  living_status
  set_living(revid rev, bool new_status) const;
};

struct line_id
{
  revid rev;
  int pos;

  line_id(){}
  line_id(revid const & r, int p);
};

// keep this small, we have a vector of them that gets things inserted
// in the middle fairly often. make that need as little copying as possible.
struct weave_line
{
  line_contents line;
  line_id id;
  boost::shared_ptr<living_status::line_data> versions;

  weave_line()
  {}
  
  weave_line(line_contents const & l, revid const & v, int n);
};

//a.mash(b).resolve(c) -> "a and b were merged, with result c"
//a.mash(b).conflict() -> "merge a and b"
//a.resolve(b) -> "b is a child of a"

// This is a const object type; there are no modifiers.
struct file_state
{
  boost::shared_ptr<vector<weave_line> > weave;
  boost::shared_ptr<std::pair<interner<line_contents>,
                              interner<revid> > > itx;
  boost::shared_ptr<map<line_id, living_status> > states;

  file_state(boost::shared_ptr<vector<weave_line> > _weave,
             boost::shared_ptr<std::pair<interner<line_contents>,
                                         interner<revid> > > _itx);
  file_state();
  file_state(vector<string> const & initial, string rev);

  ~file_state();

  // combine line states between two versions of a file
  file_state
  mash(file_state const & other) const;

  // get the list of live lines in this version of the file
  vector<string>
  current() const;

  // merge; return a list of sections which either automerge or conflict
  vector<merge_section>
  conflict(file_state const & other) const;

  // add a descendent of this version to the weave, and return it
  file_state
  resolve(vector<string> const & result, string revision) const;
};

void
pcdv_test();

#endif
