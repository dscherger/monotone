#ifndef __PCDV_HH__
#define __PCDV_HH__

#include <vector>
#include <string>
#include <map>
#include <set>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

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

// This is a const object type; there are no modifiers.
struct living_status
{
  boost::shared_ptr<map<string, vector<string> > > overrides;
  boost::scoped_ptr<pair<bool, bool> > precomp;

  living_status():
   overrides(new map<string, vector<string> >()),
   precomp(new pair<bool, bool>(false, false))
  {
    overrides->insert(make_pair("root", vector<string>()));
  }

  living_status(boost::shared_ptr<map<string, vector<string> > > _overrides):
    overrides(_overrides),
    precomp(new pair<bool, bool>(false, false))
  {}

  living_status(boost::shared_ptr<map<string, vector<string> > > _overrides,
                bool living_hint):
    overrides(_overrides),
    precomp(new pair<bool, bool>(true, living_hint))
  {}

  living_status(living_status const & x):
   overrides(x.overrides),
   precomp(new pair<bool, bool>(*x.precomp))
  {}

  living_status const &
  operator=(living_status const & x)
  {
    overrides = x.overrides;
    precomp.reset(new pair<bool, bool>(*x.precomp));
    return *this;
  }

  ~living_status();

  living_status
  merge(living_status const & other) const;

  bool
  is_living() const;

  bool
  _makes_living(string key) const;

  living_status
  set_living(string rev, bool new_status) const;
};

//a.mash(b).resolve(c) -> "a and b were merged, with result c"
//a.mash(b).conflict() -> "merge a and b"
//a.resolve(b) -> "b is a child of a"

// This is a const object type; there are no modifiers.
struct file_state
{
  boost::shared_ptr<vector<pair<string, pair<string, int> > > > weave;
  boost::shared_ptr<map<pair<string, int>, living_status> > states;

  file_state(boost::shared_ptr<vector<pair<string, pair<string, int> > > > _weave):
    weave(_weave), states(new map<pair<string, int>, living_status>())
  {}

  file_state(vector<string> const & initial, string const & rev);

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
