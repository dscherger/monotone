#include <vector>
#include <string>
#include <map>
#include <set>

#include <boost/shared_ptr.hpp>

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
};

struct living_status
{
  map<string, vector<string> > overrides;

  living_status()
  {
    overrides.insert(make_pair("root", vector<string>()));
  }

  living_status(map<string, vector<string> > const & _overrides):
    overrides(_overrides)
  {}

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
struct file_state
{
  boost::shared_ptr<vector<pair<string, pair<string, int> > > > weave;
  map<pair<string, int>, living_status> states;

  file_state(boost::shared_ptr<vector<pair<string, pair<string, int> > > > _weave):
    weave(_weave)
  {}

  file_state(vector<string> const & initial, string const & rev);

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
