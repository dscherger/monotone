#ifndef __PCDV_HH__
#define __PCDV_HH__

#include <vector>
#include <string>
#include <map>
#include <set>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include "interner.hh"
#include "change_set.hh"
#include "path_component.hh"

using std::vector;
using std::string;
using std::map;
using std::pair;
using std::make_pair;
using std::set;

// pcdv (history-aware merge) for files (line state is {alive, dead} binary)

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

  living_status
  copy() const;

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

void test_file_state();//for friend decl.

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

private:
  file_state();
  file_state(boost::shared_ptr<vector<weave_line> > _weave,
             boost::shared_ptr<std::pair<interner<line_contents>,
                                         interner<revid> > > _itx);
  file_state(vector<string> const & initial, string rev,
             boost::shared_ptr<vector<weave_line> > _weave,
             boost::shared_ptr<std::pair<interner<line_contents>,
                                         interner<revid> > > _itx);
public:

  ~file_state();

  static file_state
  new_file() {return file_state();}

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

  friend void test_file_state();
};

void
pcdv_test();


// history-aware directory merge (line state is a (parent+string))
// multiple lines (files/directories) cannot have the same state (filename)

typedef int item_id;

struct path_conflict
{
  enum what {split, collision};
  what type;
  std::vector<item_id> items;
  std::vector<file_path> lnames;
  std::vector<file_path> rnames;
  std::string name;
  typedef std::pair<item_id, std::string> resolution;
};

struct item_status
{
  typedef std::pair<item_id, path_component> item_state;
  typedef std::map<revid, std::pair<item_state, vector<revid> > > item_data;
  // shared for all versions of this item
  boost::shared_ptr<item_data> versions;
  // shared between all copies of this version of this item
  boost::shared_ptr<std::vector<revid> const > leaves;
  bool is_dir;

  item_status();
  item_status(boost::shared_ptr<item_data> ver);
  item_status(item_status const & x);

  ~item_status();

  item_status const
  new_version(vector<revid> const & _leaves) const;

  item_status
  merge(item_status const & other) const;

  item_status
  suture(item_status const & other) const;

  std::set<item_state>
  current_names() const;

  item_status
  rename(revid rev, item_id new_parent, path_component new_name) const;

  item_status
  copy() const;
};


// This is a const object type; there are no modifiers.
// Usage:
//   for a->b
//     a.rearrange(<changes>, 'b')
//   for (a, b)->c (merge in history)
//     x = a.rearrange(<changes>, 'c')
//     y = b.rearrange(<changes>, 'c')
//     x.mash(y)
//   for merge(a, b)
//     x = a.mash(b)
//     <conflict> = x.get_conflicts()
//     x.resolve(<conflict_resolution>)
//     x.get_changes_from(a)
//     x.get_changes_from(b)
class tree_state
{
  typedef int fpid;
  boost::shared_ptr<vector<boost::shared_ptr<item_status::item_data> > > items;
  boost::shared_ptr<std::map<item_id, item_status> > states;
  boost::shared_ptr<interner<revid> > itx;

  tree_state(boost::shared_ptr<vector<boost::shared_ptr<
                                        item_status::item_data> > > _items,
             boost::shared_ptr<interner<revid> > _itx);
  tree_state();
public:

  ~tree_state();

  static tree_state
  new_tree() {return tree_state();}

  static tree_state
  merge_with_rearrangement(std::vector<tree_state> const & trees,
        std::vector<change_set::path_rearrangement> const & changes,
        std::string revision);

  std::vector<path_conflict>
  conflict(tree_state const & other) const;
  
  bool
  is_clean()
  {return conflict(*this).empty();}

  std::vector<std::pair<item_id, file_path> >
  current() const;

  // get the changes along edge this->merged for merged=merge(revs)
  // note that revs should include *this
  void
  get_changes_for_merge(tree_state const & merged,
                        change_set::path_rearrangement & changes) const;

  static tree_state
  merge_with_resolution(std::vector<tree_state> const & revs,
                        std::set<path_conflict::resolution> const & res,
                        std::string const & revision);
private:
  file_path
  get_full_name(item_status::item_state x) const;

  file_path
  get_full_name(item_status x) const;

  file_path
  try_get_full_name(item_status::item_state x, int & d) const;

  std::string
  get_ambiguous_full_name(item_status::item_state x) const;

  tree_state
  mash(tree_state const & other) const;

  static tree_state
  mash(std::vector<tree_state> const & trees);

  void
  ensure_dir_exists(std::vector<path_component> const & parts,
                    std::map<fpid, item_id> & outmap,
                    interner<fpid> & cit,
                    std::string const & revision);
};

void
dirmerge_test();

#endif
