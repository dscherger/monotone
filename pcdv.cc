#include <set>
#include <map>
#include <deque>
#include <boost/tuple/tuple_comparison.hpp>

#include "sanity.hh"
#include "pcdv.hh"

#include <iostream>

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
//////////////// History-aware file merge (pcdv) ////////////////////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////


// This stuff is to provide data for size optimization.
unsigned int biggest_living_status=0;
unsigned int sum_living_status=0;
unsigned int num_living_status=0;

living_status::~living_status()
{
/*
  if (overrides.unique())
    {
      ++num_living_status;
      sum_living_status+=overrides->size();
      if (overrides->size() > biggest_living_status)
        biggest_living_status = overrides->size();
    }
*/
}

file_state::~file_state()
{
/*
  if (weave.unique())
    {
      std::cout<<"Destroyed file_state of "<<weave->size()
               <<" lines."<<std::endl;
      weave.reset();
      states.reset();
      std::cout<<"Average living_status size: "
               <<float(sum_living_status)/float(num_living_status)<<std::endl;
      std::cout<<"Max living_status size: "<<biggest_living_status<<std::endl;
      std::cout<<"Destroyed "<<num_living_status
               <<" living_status so far."<<std::endl;
    }
*/
}
// end optimization data generation code

// find lines that occur exactly once in each of a and b
void
unique_lcs(vector<line_contents> const & a,
           vector<line_contents> const & b,
           int alo,
           int blo,
           int ahi,
           int bhi,
           vector<pair<int, int> > & res)
{
  res.clear();
  if (alo == ahi || blo == bhi)
    return;
  // index[line in a] = position of line
  // if line is a duplicate, index[line] = -1
  map<line_contents, int> index;
  for (int i = 0; i < ahi - alo; ++i)
    {
      map<line_contents, int>::iterator j = index.find(idx(a,i + alo));
      if (j != index.end())
        j->second=-1;
      else
        index.insert(make_pair(idx(a,i + alo), i));
    }
  // btoa[i] = a.find(b[i]), if b[i] is unique in both
  // otherwise, btoa[i] = -1
  map<line_contents, int> index2;
  vector<int> btoa(bhi - blo, -1);
  for (int i = 0; i < bhi - blo; ++i)
    {
      map<line_contents, int>::iterator j = index.find(idx(b,i + blo));
      if (j != index.end())
        {
          map<line_contents, int>::iterator k = index2.find(idx(b,i + blo));
          if (k != index2.end())
            {
              btoa[k->second] = -1;
              index.erase(j);
            }
          else
            {
              index2.insert(make_pair(idx(b,i + blo), i));
              btoa[i] = j->second;
            }
        }
    }
  // Patience sorting
  // http://en.wikipedia.org/wiki/Patience_sorting
  vector<int> backpointers(bhi - blo, -1);
  vector<int> stacks;
  vector<int> lasts;
  int k = 0;
  for (int bpos = 0; (unsigned int)(bpos) < btoa.size(); ++bpos)
    {
      int apos = idx(btoa, bpos);
      if (apos == -1)
        continue;
      // optimize: check if next line comes at the end
      if (stacks.size() && stacks.back() < apos)
        k = stacks.size();
      // optimize: check if next line comes after prev line
      else if (stacks.size()
               && stacks[k] < apos
               && ((unsigned int)(k) == stacks.size()-1
                   || idx(stacks,k+1) > apos))
        ++k;
      else
        {
//          k = bisect(stacks, apos);
          for (int x = 0; (unsigned int)(x) < stacks.size(); ++x)
            if (idx(stacks,x) > apos)
              {
                k = x;
                break;
              }
        }
      if (k > 0)
        idx(backpointers, bpos) = idx(lasts, k-1);
      if ((unsigned int)(k) < stacks.size())
        {
          idx(stacks, k) = apos;
          idx(lasts, k) = bpos;
        }
      else
        {
          stacks.push_back(apos);
          lasts.push_back(bpos);
        }
    }
  if (lasts.empty())
    return;
  k = lasts.back();
  while (k != -1)
    {
      res.push_back(make_pair(idx(btoa, k) + alo, k + blo));
      k = idx(backpointers, k);
    }
  reverse(res.begin(), res.end());
  return;
}

void
recurse_matches(vector<line_contents> const & a,
                vector<line_contents> const & b,
                int alo,
                int blo,
                int ahi,
                int bhi,
                vector<pair<int, int> > & answer,
                int maxrecursion)
{
  if (maxrecursion < 0)
    return;
  unsigned int oldlength = answer.size();
  // extend line matches into section matches
  vector<pair<int, int> > linematches;
  unique_lcs(a, b, alo, blo, ahi, bhi, linematches);
  for (vector<pair<int, int> >::iterator i = linematches.begin();
       i != linematches.end(); ++i)
    {
      int apos = i->first;
      int bpos = i->second;
      int lasta = -1, lastb = -1;
      if (answer.size())
        {
          lasta = answer.back().first;
          lastb = answer.back().second;
        }
      // don't overlap with an existing match
      if (apos <= lasta || bpos <= lastb)
        continue;
      // extend as far back as possible
      while (apos > lasta + 1 && bpos > lastb + 1)
        {
          int newapos = apos - 1;
          while (newapos > lasta && idx(a, newapos) == -1)
            --newapos;
          if (newapos == lasta || idx(a, newapos) != idx(b, bpos-1))
            break;
          apos = newapos;
          --bpos;
        }
      recurse_matches(a, b, ((lasta==-1)?0:lasta), ((lastb==-1)?0:lastb),
                      apos, bpos, answer, maxrecursion-1);
      answer.push_back(make_pair(apos, bpos));
      // extend as far forward as possible
      while (apos < ahi - 1 && bpos < bhi - 1)
        {
          int newapos = apos + 1;
          while (newapos < ahi - 1 && idx(a, newapos) == -1)
            ++newapos;
          if (newapos == ahi || idx(a, newapos) != idx(b, bpos + 1))
            break;
          apos = newapos;
          ++bpos;
          answer.push_back(make_pair(apos, bpos));
        }
    }
  if (answer.size() > oldlength)
    // find matches between the last match and the end
    recurse_matches(a, b, answer.back().first, answer.back().second,
                    ahi, bhi, answer, maxrecursion - 1);
}

////////////////////////////////////////////
/////////////// living_status //////////////
////////////////////////////////////////////

living_status::living_status():
               overrides(new map<revid, vector<revid> >()),
               leaves(new vector<revid>()),
               precomp(new pair<bool, bool>(false, false))
{
  overrides->insert(make_pair(revid(-1), vector<revid>()));
  leaves->push_back(revid(-1));
}

living_status::living_status(boost::shared_ptr<line_data> ovr):
               overrides(ovr),
               leaves(new vector<revid>()),
               precomp(new pair<bool, bool>(false, false))
{
  leaves->push_back(revid(-1));
}

living_status::living_status(living_status const & x):
               overrides(x.overrides),
               leaves(x.leaves),
               precomp(x.precomp)
{}

living_status const &
living_status::operator=(living_status const & x)
{
  overrides = x.overrides;
  leaves = x.leaves;
  precomp = x.precomp;
  return *this;
}

living_status
living_status::copy() const
{
  living_status out(*this);
  out.leaves.reset(new std::vector<revid>(*leaves));
  return out;
}

living_status const
living_status::new_version(vector<revid> const & _leaves) const
{
  living_status out(*this);
  out.leaves.reset(new vector<revid>(_leaves));
  out.precomp.reset(new pair<bool, bool>(false, false));
  return out;
}

living_status const
living_status::new_version(vector<revid> const & _leaves,
                           bool living_hint) const
{
  living_status out(new_version(_leaves));
  out.precomp->first = true;
  out.precomp->second = living_hint;
  return out;
}

living_status
living_status::merge(living_status const & other) const
{
  I(overrides == other.overrides);
  set<revid> leafset, done;
  std::deque<revid> todo;
  for (vector<revid>::const_iterator i = leaves->begin();
       i != leaves->end(); ++i)
    leafset.insert(*i);
  for (vector<revid>::const_iterator i = other.leaves->begin();
       i != other.leaves->end(); ++i)
    leafset.insert(*i);
  for (set<revid>::const_iterator i = leafset.begin();
       i != leafset.end(); ++i)
    todo.push_back(*i);
  while (todo.size())
    {
      line_data::const_iterator i = overrides->find(todo.front());
      I(i != overrides->end());
      for (vector<revid>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        {
          if (done.find(*j) != done.end())
            continue;
          set<revid>::iterator l = leafset.find(*j);
          if (l != leafset.end())
            {
              leafset.erase(l);
              continue;
            }
          done.insert(*j);
          todo.push_back(*j);
        }
      todo.pop_front();
    }

  vector<revid> newleaves;
  newleaves.reserve(leafset.size());
  for (set<revid>::const_iterator i = leafset.begin();
       i != leafset.end(); ++i)
    newleaves.push_back(*i);
  if (newleaves == *leaves)
    return *this;
  if (newleaves == *other.leaves)
    return other;
  return new_version(newleaves);
}

bool
living_status::is_living() const
{
  if (precomp->first)
    return precomp->second;
  set<revid> oldworking, newworking, ref;
  std::deque<revid> todo;
  for (vector<revid>::const_iterator i = leaves->begin();
       i != leaves->end(); ++i)
    todo.push_back(*i);
  while (todo.size())
    {
      unsigned int s = ref.size();
      ref.insert(todo.front());
      if (s != ref.size())
        {
          line_data::const_iterator i = overrides->find(todo.front());
          I(i != overrides->end());
          for (vector<revid>::const_iterator j = i->second.begin();
               j != i->second.end(); ++j)
            todo.push_back(*j);
        }
      todo.pop_front();
    }
  newworking = ref;
  while (oldworking != newworking)
    {
      oldworking = newworking;
      newworking = ref;
      for (set<revid>::const_iterator k = oldworking.begin();
            k != oldworking.end(); ++k)
        {
          line_data::const_iterator x = overrides->find(*k);
          for (vector<revid>::const_iterator j = x->second.begin();
                j != x->second.end(); ++j)
            newworking.erase(*j);
        }
    }
  precomp->first = true;
  return precomp->second = (newworking.find(revid(-1)) == newworking.end());
}

bool
living_status::_makes_living(revid key) const
{
  bool result = false;
  line_data::const_iterator i;
  while (key != revid(-1))
    {
      result = !result;
      i = overrides->find(key);
      if (i == overrides->end() || i->second.empty())
        break;
      key = idx(i->second, 0);
    }
  return result;
}

living_status
living_status::set_living(revid rev, bool new_status) const
{
  if (new_status == is_living())
    return *this;
  vector<revid> newleaves;
  line_data::iterator res =
      overrides->insert(make_pair(rev, vector<revid>())).first;
  bool in = false;
  for (vector<revid>::iterator i = leaves->begin();
        i != leaves->end(); ++i)
    {
      if (!in && *i > rev)
        {
          in = true;
          newleaves.push_back(rev);
        }
      if (_makes_living(*i) != new_status)
        res->second.push_back(*i);
      else
        newleaves.push_back(*i);
    }
  if (!in)
    newleaves.push_back(rev);
  return new_version(newleaves, new_status);
}

/////////// line_id, weave_line /////////////////////

line_id::line_id(revid const & r, int p):
 rev(r), pos(p)
{}

bool operator<(line_id const l, line_id const r)
{
  return l.rev < r.rev || (l.rev == r.rev && l.pos < r.pos);
}

bool operator>(line_id const l, line_id const r)
{
  return l.rev > r.rev || (l.rev == r.rev && l.pos > r.pos);
}

bool operator!=(line_id const l, line_id const r)
{
  return l.rev != r.rev && l.pos != r.pos;
}

bool operator==(line_id const l, line_id const r)
{
  return l.rev == r.rev && l.pos == r.pos;
}

weave_line::weave_line(line_contents const & l, revid const & v, int n):
            line(l),
            id(line_id(v,n)),
            versions(new living_status::line_data())
{
  versions->insert(make_pair(revid(-1), vector<revid>()));
}

////////////////////////////////////////////////////
//////////////////// file_state ////////////////////
////////////////////////////////////////////////////

file_state::file_state(boost::shared_ptr<vector<weave_line> > _weave,
                       boost::shared_ptr<std::pair<interner<line_contents>,
                                                   interner<revid> > > _itx):
            weave(_weave),
            itx(_itx),
            states(new map<line_id, living_status>())
{}

file_state::file_state():
            weave(new vector<weave_line>()),
            itx(new std::pair<interner<line_contents>,
                              interner<revid> >()),
            states(new map<line_id, living_status>())
{}

file_state::file_state(vector<string> const & initial, string rev,
                       boost::shared_ptr<vector<weave_line> > _weave,
                       boost::shared_ptr<std::pair<interner<line_contents>,
                                                   interner<revid> > > _itx):
            weave(_weave),
            itx(_itx),
            states(new map<line_id, living_status>())
{
  revid r(itx->second.intern(rev));
  for (int i = 0; (unsigned int)(i) < initial.size(); ++i)
    {
      line_contents l(itx->first.intern(idx(initial, i)));
      weave->push_back(weave_line(l, r, i));
      living_status ls(living_status(weave->back().versions));
      states->insert(make_pair(weave->back().id, ls.set_living(r, true)));
    }
}

// combine line states between two versions of a file
file_state
file_state::mash(file_state const & other) const
{
  I(weave == other.weave);
  file_state newstate(weave, itx);
  map<line_id, living_status>::const_iterator l, r;
  l = states->begin();
  r = other.states->begin();
  while (l != states->end() || r != other.states->end())
    {
      if (l == states->end())
        newstate.states->insert(make_pair(r->first,r->second.copy())), ++r;
      else if (r == other.states->end())
        newstate.states->insert(make_pair(l->first,l->second.copy())), ++l;
      else if (l->first > r->first)
        newstate.states->insert(make_pair(r->first,r->second.copy())), ++r;
      else if (r->first > l->first)
        newstate.states->insert(make_pair(l->first,l->second.copy())), ++l;
      else
        {
          newstate.states->insert(make_pair(l->first,
                                            l->second.merge(r->second)));
          ++l, ++r;
        }
    }
  return newstate;
}

// get the list of live lines in this version of the file
vector<string>
file_state::current() const
{
  vector<string> res;
  for (vector<weave_line>::iterator i
        = weave->begin(); i != weave->end(); ++i)
    {
      map<line_id, living_status>::const_iterator j
        = states->find(i->id);
      if (j != states->end() && j->second.is_living())
        res.push_back(itx->first.lookup(i->line));
    }
  return res;
}

// merge; return a list of sections which either automerge or conflict
vector<merge_section>
file_state::conflict(file_state const & other) const
{
  I(weave == other.weave);
  vector<merge_section> result;
  vector<string> left, right, clean;
  bool mustleft = false, mustright = false;
  for (vector<weave_line>::const_iterator i
        = weave->begin(); i != weave->end(); ++i)
    {
      std::string line(itx->first.lookup(i->line));
      map<line_id, living_status>::const_iterator m = states->find(i->id);
      map<line_id, living_status>::const_iterator o = other.states->find(i->id);
      bool mm(m != states->end());
      bool oo(o != other.states->end());
      bool mehave=false, otherhave=false, mergehave=false;
      if (mm)
        mergehave = mehave = m->second.is_living();
      if (oo)
        mergehave = otherhave = o->second.is_living();
      if (mm && oo)
        mergehave = m->second.merge(o->second).is_living();

      if (mehave && otherhave && mergehave)
        {
          if (mustright && mustleft)
            result.push_back(merge_section(left, right));
          else
            result.push_back(merge_section(clean));
          result.push_back(merge_section(line));
          left.clear();
          right.clear();
          clean.clear();
          mustleft = false;
          mustright = false;
        }
      else
        {
          if (mehave != otherhave)
            {
              if (mehave == mergehave)
                mustleft = true;
              else
                mustright = true;
            }
          if (mehave)
            left.push_back(line);
          if (otherhave)
            right.push_back(line);
          if (mergehave)
            clean.push_back(line);
        }
    }
  if (mustright && mustleft)
    result.push_back(merge_section(left, right));
  else
    result.push_back(merge_section(clean));
  return result;
}

// add a descendent of this version to the weave, and return it
file_state
file_state::resolve(vector<string> const & result_, string revision) const
{
  if (weave->empty())
    {
      file_state x(result_, revision, weave, itx);
      return x;
    }
  revid rev(itx->second.intern(revision));
  vector<line_contents> result;
  result.reserve(result_.size());
  for (vector<string>::const_iterator i = result_.begin();
       i != result_.end(); ++i)
    result.push_back(itx->first.intern(*i));
  vector<line_contents> lines;
  lines.reserve(weave->size());
  for (vector<weave_line>::iterator i = weave->begin();
       i != weave->end(); ++i)
    {
      map<line_id, living_status>::const_iterator j
        = states->find(i->id);
      if (j != states->end() && j->second.is_living())
        lines.push_back(i->line);
      else
        lines.push_back(-1);
    }
  vector<pair<int, int> > matches;
  recurse_matches(lines, result, 0, 0,
                  lines.size(), result.size(),
                  matches, 10);
  lines.clear();
  for (vector<weave_line>::iterator i = weave->begin();
       i != weave->end(); ++i)
    lines.push_back(i->line);
  vector<pair<int, int> > matches2;
  matches.push_back(make_pair(lines.size(), result.size()));
  for (vector<pair<int, int> >::iterator i = matches.begin();
        i != matches.end(); ++i)
    {
      int alo=0, blo=0;
      if (matches2.size())
        {
          alo = matches2.back().first;
          blo = matches2.back().second;
        }
      recurse_matches(lines, result, alo, blo,
                      i->first, i->second, matches2, 10);
      if ((unsigned int)(i->first) != lines.size())
        matches2.push_back(make_pair(i->first, i->second));
    }
  matches.pop_back();
  set<line_id> living;
  for (vector<pair<int, int> >::iterator i = matches2.begin();
        i != matches2.end(); ++i)
    {
      living.insert(idx(*weave, i->first).id);
    }
  vector<pair<int, weave_line> > toinsert;
  int lasta = -1, lastb = -1;
  matches2.push_back(make_pair(weave->size(), result.size()));
  for (vector<pair<int, int> >::iterator i = matches2.begin();
        i != matches2.end(); ++i)
    {
      for (int x = lastb + 1; x < i->second; ++x)
        {
          living.insert(line_id(rev, x));
          toinsert.push_back(make_pair(lasta + 1,
                                       weave_line(idx(result,x),
                                                  rev,
                                                  x)));
        }
      lasta = i->first;
      lastb = i->second;
    }
  reverse(toinsert.begin(), toinsert.end());
  // toinsert is now in last-first order

  if (toinsert.size())
    {
      weave->insert(weave->begin()+toinsert.front().first,
                    toinsert.size(),
                    weave_line());
      int tpos = 0;
      int wfrom = toinsert.front().first - 1;
      int wpos = wfrom + toinsert.size();
      while ((unsigned int)(tpos) < toinsert.size())
        {
          if (idx(toinsert, tpos).first == wfrom+1)
            idx(*weave, wpos--) = idx(toinsert, tpos++).second;
          else
            idx(*weave, wpos--) = idx(*weave, wfrom--);
        }
    }

  file_state out(weave, itx);
  for (vector<weave_line>::iterator i = weave->begin();
       i != weave->end(); ++i)
    {
      bool live = living.find(i->id) != living.end();
      living_status orig(i->versions);
      map<line_id, living_status>::const_iterator j = states->find(i->id);
      if (j != states->end())
        orig = j->second;
      else if (!live)
        continue;
      living_status x(orig.set_living(rev, live));
      out.states->insert(make_pair(i->id, x));
    }
  return out;
}

//////////////////////////////////////////////
//////////////// misc ////////////////////////
//////////////////////////////////////////////

void
show_conflict(vector<merge_section> const & result)
{
  for (vector<merge_section>::const_iterator i = result.begin();
       i != result.end(); ++i)
    {
      if (i->split)
        {
          if (i->left.size())
            {
              std::cout<<"<<<<<<<<<<"<<'\n';
              for (vector<string>::const_iterator j = i->left.begin();
                   j != i->left.end(); ++j)
                std::cout<<" "<<*j;
            }
          if (i->right.size())
            {
              std::cout<<">>>>>>>>>>"<<'\n';
              for (vector<string>::const_iterator j = i->right.begin();
                   j != i->right.end(); ++j)
                std::cout<<" "<<*j;
            }
        }
      else
        {
          if (i->left.size())
            {
              std::cout<<"=========="<<'\n';
              for (vector<string>::const_iterator j = i->left.begin();
                   j != i->left.end(); ++j)
                std::cout<<" "<<*j;
            }
        }
    }
}

vector<merge_section>
consolidate(vector<merge_section> const & in)
{
  vector<merge_section> out;
  if (!in.size())
    return out;
  vector<merge_section>::const_iterator i = in.begin();
  out.push_back(*i);
  ++i;
  while (i != in.end())
    {
      if (!out.back().split && !i->split)
        out.back().left.insert(out.back().left.end(),
                               i->left.begin(),
                               i->left.end());
      else
        out.push_back(*i);
      ++i;
    }
  return out;
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
////////////////// History-aware directory merge ////////////////////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

item_status::item_status():
             versions(new item_data()),
             leaves(),
             is_dir(false)
{
  versions->insert(make_pair(revid(-1),
                             make_pair(make_pair(item_id(-1),
                                                 make_null_component()),
                                       vector<revid>())));
  std::vector<revid> * l = new std::vector<revid>();
  l->push_back(revid(-1));
  leaves.reset(l);
}

item_status::item_status(boost::shared_ptr<item_data> ver):
             versions(ver),
             leaves(),
             is_dir(false)
{
  std::vector<revid> * l = new std::vector<revid>();
  l->push_back(revid(-1));
  leaves.reset(l);
  versions->insert(make_pair(revid(-1),
                             make_pair(make_pair(item_id(-1),
                                                 make_null_component()),
                                       vector<revid>())));
}

item_status::item_status(item_status const & x):
               versions(x.versions),
               leaves(x.leaves),
               is_dir(x.is_dir)
{}

item_status::~item_status()
{}

item_status
item_status::copy() const
{
  item_status out(*this);
  out.leaves.reset(new std::vector<revid>(*leaves));
  return out;
}

item_status const
item_status::new_version(vector<revid> const & _leaves) const
{
  I(leaves->size());
  item_status out(*this);
  out.leaves.reset(new vector<revid>(_leaves));
  return out;
}

item_status
item_status::merge(item_status const & other) const
{
  I(versions == other.versions);
  I(is_dir == other.is_dir);
  set<revid> leafset, done;
  std::deque<revid> todo;
  for (vector<revid>::const_iterator i = leaves->begin();
       i != leaves->end(); ++i)
    leafset.insert(*i);
  for (vector<revid>::const_iterator i = other.leaves->begin();
       i != other.leaves->end(); ++i)
    leafset.insert(*i);
  for (set<revid>::const_iterator i = leafset.begin();
       i != leafset.end(); ++i)
    todo.push_back(*i);
  while (todo.size())
    {
      item_data::const_iterator i = versions->find(todo.front());
      I(i != versions->end());
      for (vector<revid>::const_iterator j = i->second.second.begin();
           j != i->second.second.end(); ++j)
        {
          if (done.find(*j) != done.end())
            continue;
          set<revid>::iterator l = leafset.find(*j);
          if (l != leafset.end())
            {
              leafset.erase(l);
              continue;
            }
          done.insert(*j);
          todo.push_back(*j);
        }
      todo.pop_front();
    }
  I(leafset.size());

  vector<revid> newleaves;
  newleaves.reserve(leafset.size());
  for (set<revid>::const_iterator i = leafset.begin();
       i != leafset.end(); ++i)
    newleaves.push_back(*i);
  if (newleaves == *leaves)
    return *this;
  if (newleaves == *other.leaves)
    return other;
  return new_version(newleaves);
}

item_status
item_status::suture(item_status const & other) const
{
  I(versions != other.versions);
  I(is_dir == other.is_dir);
  for (item_data::iterator o = other.versions->begin();
       o != other.versions->end(); ++o)
    {
      item_data::iterator m = versions->find(o->first);
      if (m == versions->end())
        versions->insert(*o);
      else
        {
          I(m->second.first == o->second.first);
          std::set<revid> s;
          std::vector<revid> const & ov(o->second.second);
          std::vector<revid> & mv(m->second.second);
          for (std::vector<revid>::const_iterator j = mv.begin();
               j != mv.end(); ++j)
            s.insert(*j);
          for (std::vector<revid>::const_iterator j = ov.begin();
               j != ov.end(); ++j)
            {
              unsigned int p = s.size();
              s.insert(*j);
              if (p != s.size())
                mv.push_back(*j);
            }
        }
    }
  item_status myother(other);
  myother.versions = versions;
  return merge(myother);
}

std::set<item_status::item_state>
item_status::current_names() const
{
  I(leaves->size());
  std::set<item_state> out;
  for (vector<revid>::const_iterator i = leaves->begin();
       i != leaves->end(); ++i)
    {
      item_data::const_iterator j = versions->find(*i);
      I(j != versions->end());
      out.insert(j->second.first);
    }
  if (out.size() > 1
      && out.find(make_pair(-1, make_null_component())) != out.end())
    {
      out.clear();
      out.insert(make_pair(-1, make_null_component()));
    }
  return out;
}

item_status
item_status::rename(revid rev, item_id new_parent, path_component new_name) const
{
  item_state newstate(make_pair(new_parent, new_name));
//  {
    item_data::iterator i = versions->find(rev);
    if (i != versions->end())
      {
//        I(i->second.first == newstate);
// An error, but it's triggered by errors already in the monotone db.
// These are of the form cs_left = {}, cs_right = {drop, add}
// So, warn instead of failing.
        if (i->second.first == newstate)
          return *this;
        W(F("Renaming a file to multiple names within one revision."));
      }
//  }
  vector<revid> newleaves, badleaves;
  newleaves.push_back(rev);
  for (vector<revid>::const_iterator i = leaves->begin();
        i != leaves->end(); ++i)
    {
      item_data::const_iterator j = versions->find(*i);
      I(j != versions->end());
      if (j->second.first == newstate)
        newleaves.push_back(*i);
      else if (*i != rev)
        badleaves.push_back(*i);
    }
  if (i != versions->end())
    versions->erase(i);
  versions->insert(make_pair(rev, make_pair(newstate, badleaves)));
  if (badleaves.empty())
    {
      std::set<item_state> c = current_names();
      I(c.size() == 1);
      I(*c.begin() == newstate);
      return *this;
    }

  item_status out(new_version(newleaves));
  return out;
}


tree_state::tree_state():
            items(new vector<boost::shared_ptr<item_status::item_data> >()),
            states(new std::map<item_id, item_status>()),
            itx(new interner<revid>()),
            sutures(new std::map<item_id, item_id>())
{}

tree_state
tree_state::new_skel() const
{
  tree_state out;
  out.items = items;
  out.itx = itx;
  out.sutures = sutures;
  return out;
}

tree_state::~tree_state()
{}

void
tree_state::add_suture(item_id l, item_id r)
{
  std::map<item_id, item_id>::const_iterator i = sutures->find(r);
  if (i != sutures->end())
    add_suture(l, i->second);
}

void
tree_state::apply_sutures()
{
  for (std::map<item_id, item_id>::const_iterator i = sutures->begin();
       i != sutures->end(); ++i)
    {
      std::map<item_id, item_status>::iterator j = states->find(i->first);
      std::map<item_id, item_status>::iterator k = states->find(i->second);
      if (j != states->end())
        {
          if (k == states->end())
            {
              states->insert(make_pair(i->second, j->second));
              states->erase(j);
            }
          else
            {
              k->second = k->second.suture(j->second);
              states->erase(j);
            }
        }
    }
}

const int deleted_dir(1);
const int deleted_file(2);
const int renamed_dir(3);
const int renamed_file(4);
const int added_dir(5); // not used
const int added_file(6);
typedef std::multimap<boost::tuple<int, int, int>,
                    boost::tuple<file_path, file_path, bool> > orderer;

void
process_rearrangement(change_set::path_rearrangement const & changes,
                      orderer & todo, int num)
{
  for (std::set<file_path>::const_iterator i = changes.deleted_dirs.begin();
       i != changes.deleted_dirs.end(); ++i)
    {
      std::vector<path_component> splitted;
      split_path(*i, splitted);
      todo.insert(make_pair(boost::make_tuple(splitted.size(),
                                              deleted_dir, num),
                            boost::make_tuple(*i, file_path(), true)));
    }
  for (std::set<file_path>::const_iterator i = changes.deleted_files.begin();
       i != changes.deleted_files.end(); ++i)
    {
      std::vector<path_component> splitted;
      split_path(*i, splitted);
      todo.insert(make_pair(boost::make_tuple(splitted.size(),
                                              deleted_file, num),
                            boost::make_tuple(*i, file_path(), false)));
    }
  for (std::map<file_path, file_path>::const_iterator
         i = changes.renamed_dirs.begin();
       i != changes.renamed_dirs.end(); ++i)
    {
      std::vector<path_component> splitted;
      split_path(i->second, splitted);
      todo.insert(make_pair(boost::make_tuple(splitted.size(),
                                              renamed_dir, num),
                            boost::make_tuple(i->first, i->second, true)));
    }
  for (std::map<file_path, file_path>::const_iterator
         i = changes.renamed_files.begin();
       i != changes.renamed_files.end(); ++i)
    {
      std::vector<path_component> splitted;
      split_path(i->second, splitted);
      todo.insert(make_pair(boost::make_tuple(splitted.size(),
                                              renamed_file, num),
                            boost::make_tuple(i->first, i->second, false)));
    }
  for (std::set<file_path>::const_iterator i = changes.added_files.begin();
       i != changes.added_files.end(); ++i)
    {
      std::vector<path_component> splitted;
      split_path(*i, splitted);
      todo.insert(make_pair(boost::make_tuple(splitted.size(),
                                              added_file, num),
                            boost::make_tuple(file_path(), *i, false)));
    }

}


void
tree_state::ensure_dir_exists(std::vector<path_component> const & parts,
                             std::map<fpid, item_id> & outmap,
                             interner<fpid> & cit,
                             std::string const & revision)
{
  // parent directory implied, did not previously exist
  std::vector<path_component> p(parts);
  bool newfile;
  file_path pdir;
  fpid pd;
  do
    {
      p.pop_back();
      if (p.size())
        compose_path(p, pdir);
      else
        pdir = file_path();
      pd = cit.intern(pdir(), newfile);
    }
  while (newfile);
  // found a parent that already exists
  std::map<fpid, item_id>::const_iterator k = outmap.find(pd);
  I(k != outmap.end());
  item_id pi(k->second);
  while (p.size() != parts.size())
    {
      p.push_back(idx(parts, p.size()));
      compose_path(p, pdir);
      boost::shared_ptr<item_status::item_data> ver;
      ver.reset(new item_status::item_data());
      items->push_back(ver);
      revid r(itx->intern(revision));
      item_status newitem(item_status(ver).rename(r, pi, p.back()));
      newitem.is_dir = true;
      states->insert(make_pair(items->size()-1,
                               newitem));
      pi = items->size() - 1;
      pd = cit.intern(pdir());
      outmap.insert(make_pair(pd, pi));
    }
}


tree_state
tree_state::merge_with_rearrangement(std::vector<tree_state> const & trees,
                  std::vector<change_set::path_rearrangement> const & changes,
                  std::string revision)
{
  // shortest first, then in order of:
  // deleted dirs, deleted files, renamed dirs, renamed files, added files
  // sort key is (depth, class, rev#)
  orderer todo;
  std::map<fpid, item_id> outmap;// for tree poststate
  std::vector<std::map<fpid, item_id> > premaps;//for tree prestates
  std::vector<std::map<fpid, item_id> > postmaps;//for rearrangement poststates
  {
    int n;
    std::vector<change_set::path_rearrangement>::const_iterator i;
    for (i = changes.begin(), n = 0; i != changes.end(); ++i, ++n)
      {
        process_rearrangement(*i, todo, n);
      }
  }
  interner<fpid> cit;
  tree_state out(mash(trees));
  fpid rootid = cit.intern(file_path()());
  outmap.insert(make_pair(rootid, -1));

  // populate outmap with any unchanged entries
  {
    std::vector<change_set::path_rearrangement>::const_iterator x;
    std::vector<tree_state>::const_iterator i;
    for (i = trees.begin(), x = changes.begin();
         i != trees.end() && x != changes.end(); ++i, ++x)
      {
        premaps.push_back(std::map<fpid, item_id>());
        std::vector<std::pair<item_id, file_path> > curr = i->current();
        for (std::vector<std::pair<item_id, file_path> >::const_iterator
               j = curr.begin(); j != curr.end(); ++j)
          {
            fpid myid = cit.intern(j->second());
            premaps.back().insert(make_pair(myid, j->first));

            // does it stay put?
            bool deldir = (x->deleted_dirs.find(j->second)
                           != x->deleted_dirs.end());
            bool delfile = (x->deleted_files.find(j->second)
                            != x->deleted_files.end());
            bool mvdir = (x->renamed_dirs.find(j->second)
                          != x->renamed_dirs.end());
            bool mvfile = (x->renamed_files.find(j->second)
                           != x->renamed_files.end());
            if (!deldir && !delfile && !mvdir && !mvfile)
              {
                std::pair<std::map<fpid, item_id>::iterator, bool> r;
                r = outmap.insert(make_pair(myid, j->first));
                if (r.first->second != j->first)
                  {
                    W(F("Colliding over %1%") % j->second());
                    out.add_suture(r.first->second, j->first);
                  }
              }
          }
      }
  }

  for (orderer::const_iterator i = todo.begin(); i != todo.end(); ++i)
    {
      file_path const & from(i->second.get<0>());
      file_path const & to(i->second.get<1>());
      bool is_dir(i->second.get<2>());
      int type(i->first.get<1>());
      int which(i->first.get<2>());
      item_id current_id;
      std::map<item_id, item_status>::iterator j = out.states->end();
      bool addednew = false;

      // find where it comes from...
      if (type == added_file || type == added_dir)
        {
          std::map<fpid, item_id>::const_iterator
            k = outmap.find(cit.intern(to()));
          if (k == outmap.end())
            {
              boost::shared_ptr<item_status::item_data> ver;
              ver.reset(new item_status::item_data());
              out.items->push_back(ver);
              current_id = out.items->size() - 1;
              std::pair<std::map<item_id, item_status>::iterator, bool> p;
              p = out.states->insert(make_pair(current_id, item_status(ver)));
              I(p.second);
              j = p.first;
              addednew = true;
//              P(F("New item: %1%") % current_id);
            }
          else
            {
              current_id = k->second;
              j = out.states->find(current_id);
            }
        }
      else
        {
          bool newfile;
          fpid f = cit.intern(from(), newfile);
          I(!newfile);
          std::map<fpid, item_id>::const_iterator
            k = idx(premaps, which).find(f);
          I(k != idx(premaps, which).end());
          current_id = k->second;
          j = out.states->find(current_id);
        }
      I(j != out.states->end());
      item_status & current_item(j->second);
//      P(F("%1% (%2%) from %3% to %4%, by %5% (type %6%) in %7%") % current_id
//        % out.get_full_name(j->second) % from % to % which % type % revision);

      // ...find where it goes...
      if (type == deleted_file || type == deleted_dir)
        {
          current_item = current_item.rename(out.itx->intern(revision),
                                             item_id(-1),
                                             make_null_component());
          continue;
        }
      {
        int d = -1;
        std::set<item_status::item_state> s = current_item.current_names();
        file_path orig;
        if (s.size() == 1)
          orig = out.try_get_full_name(*s.begin(), d);
        if (!(addednew
              || !(orig == file_path() && d != -1)
              || to == file_path()))
          W(F("undeleting %1%") % to);
      }

      file_path pdir;
      std::vector<path_component> parts;
      path_component new_name;
      split_path(to, parts, new_name);
      if (parts.size())
        compose_path(parts, pdir);
      bool newfile;
      fpid pd = cit.intern(pdir(), newfile);
      if (newfile)
        out.ensure_dir_exists(parts, outmap, cit, revision);

      std::map<fpid, item_id>::const_iterator k = outmap.find(pd);
      I(k != outmap.end());

      // ...and get it moved in.
      current_item = current_item.rename(out.itx->intern(revision),
                                         k->second,
                                         new_name);
      current_item.is_dir = is_dir;
      file_path recon = out.get_full_name(current_item);
      I(recon == to);
      std::pair<std::map<fpid, item_id>::iterator, bool> r;
      r = outmap.insert(make_pair(cit.intern(to()), current_id));
      if (r.first->second != current_id)
        {
          W(F("Colliding over %1%") % to);
          out.add_suture(r.first->second, j->first);
        }
    }
  out.apply_sutures();
  return out;
}

tree_state
tree_state::mash(std::vector<tree_state> const & trees)
{
  I(!trees.empty());
  tree_state out(idx(trees, 0));
  for (std::vector<tree_state>::const_iterator i = ++trees.begin();
       i != trees.end(); ++i)
    out = out.mash(*i);
  if (trees.size() == 1)
    out.states.reset(new map<item_id, item_status>(*out.states));
  I(out.states != idx(trees, 0).states);
  return out;
}

tree_state
tree_state::mash(tree_state const & other) const
{
  I(items == other.items);
  tree_state newstate = new_skel();
  map<item_id, item_status>::const_iterator l, r;
  l = states->begin();
  r = other.states->begin();
  while (l != states->end() || r != other.states->end())
    {
      if (l == states->end())
        newstate.states->insert(make_pair(r->first,r->second.copy())), ++r;
      else if (r == other.states->end())
        newstate.states->insert(make_pair(l->first,l->second.copy())), ++l;
      else if (l->first > r->first)
        newstate.states->insert(make_pair(r->first,r->second.copy())), ++r;
      else if (r->first > l->first)
        newstate.states->insert(make_pair(l->first,l->second.copy())), ++l;
      else
        {
          newstate.states->insert(make_pair(l->first,
                                            l->second.merge(r->second)));
          ++l, ++r;
        }
    }
  return newstate;
}

std::vector<path_conflict>
tree_state::conflict(tree_state const & other) const
{
  tree_state merged(mash(other));
  merged.apply_sutures();
  std::vector<path_conflict> out;
  std::map<item_status::item_state, std::set<item_id> > m;

  // splits, merge(mv a b, mv a c)
  for (std::map<item_id, item_status>::const_iterator
         i = merged.states->begin();
       i != merged.states->end(); ++i)
    {
      std::set<item_status::item_state> s = i->second.current_names();
      if (s.size() != 1)
        {
          path_conflict c;
          c.type = path_conflict::split;
          c.items.push_back(i->first);
          std::map<item_id, item_status>::const_iterator
            j(states->find(i->first)),
            k(other.states->find(i->first));
          I(j != states->end());
          I(k != other.states->end());
          std::set<item_status::item_state>
            left(j->second.current_names()),
            right(k->second.current_names());
          I(left.size() == 1);
          I(right.size() == 1);
          c.lnames.push_back(get_full_name(*left.begin()));
          c.rnames.push_back(other.get_full_name(*right.begin()));
          out.push_back(c);
        }
      for (std::set<item_status::item_state>::const_iterator
             j = s.begin(); j != s.end(); ++j)
        {
          if (*j == make_pair(item_id(-1), make_null_component()))
            continue;
          std::map<item_status::item_state, std::set<item_id> >::iterator
            k = m.find(*j);
          if (k == m.end())
            {
              std::set<item_id> ns;
              ns.insert(i->first);
              m.insert(make_pair(*j, ns));
            }
          else
            k->second.insert(i->first);
        }
    }

  // collisions, merge(mv a c, mv b c)
  for (std::map<item_status::item_state, std::set<item_id> >::const_iterator
         i = m.begin(); i != m.end(); ++i)
    {
      if (i->second.size() == 1)
        continue;
      path_conflict c;
      c.type = path_conflict::collision;
      c.name = merged.get_ambiguous_full_name(i->first);
      for (std::set<item_id>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        {
          c.items.push_back(*j);
          std::map<item_id, item_status>::const_iterator
            l(states->find(*j)),
            r(other.states->find(*j));
          std::set<item_status::item_state> left, right;
          if (l != states->end())
            {
              left = l->second.current_names();
              I(left.size() == 1);
              c.lnames.push_back(get_full_name(*left.begin()));
            }
          else
            c.lnames.push_back(file_path());
          if (r != other.states->end())
            {
              right = r->second.current_names();
              I(right.size() == 1);
              c.rnames.push_back(other.get_full_name(*right.begin()));
            }
          else
            c.rnames.push_back(file_path());
        }
      out.push_back(c);
    }
  return out;
}

std::vector<std::pair<item_id, file_path> >
tree_state::current() const
{
  std::vector<std::pair<item_id, file_path> > out;
  for (std::map<item_id, item_status>::const_iterator i = states->begin();
       i != states->end(); ++i)
    {
      std::set<item_status::item_state> s = i->second.current_names();
      I(s.size() == 1);
      file_path fp = get_full_name(*s.begin());
      if (!(fp == file_path()))
      out.push_back(make_pair(i->first, fp));
    }
  return out;
}

void
tree_state::get_changes_for_merge(tree_state const & merged,
                                  change_set::path_rearrangement & changes)
                                  const
{
  changes.deleted_dirs.clear();
  changes.deleted_files.clear();
  changes.renamed_dirs.clear();
  changes.renamed_files.clear();
  //
  changes.added_files.clear();

  map<item_id, item_status>::const_iterator l, r;
  l = states->begin();
  r = merged.states->begin();
  while (l != states->end() || r != merged.states->end())
    {
      file_path from, to;
      bool from_is_dir(false), to_is_dir(false);

      if (l == states->end())
        {
          to = merged.get_full_name(r->second);
          to_is_dir = r->second.is_dir;
          ++r;
        }
      else if (r == merged.states->end())
        {
          from = get_full_name(l->second);
          from_is_dir = l->second.is_dir;
          ++l;
        }
      else if (l->first > r->first)
        {
          to = merged.get_full_name(r->second);
          to_is_dir = r->second.is_dir;
          ++r;
        }
      else if (r->first > l->first)
        {
          from = get_full_name(l->second);
          from_is_dir = l->second.is_dir;
          ++l;
        }
      else
        {
          from = get_full_name(l->second);
          from_is_dir = l->second.is_dir;
          to = merged.get_full_name(r->second);
          to_is_dir = r->second.is_dir;
          ++l, ++r;
        }

      if (to == from)
        continue;
      else if (to == file_path())
        {
          if (from_is_dir)
            changes.deleted_dirs.insert(from);
          else
            changes.deleted_files.insert(from);
        }
      else if (from == file_path())
        {
          if (to_is_dir)
            ;
          else
            changes.added_files.insert(to);
        }
      else
        {
          if (from_is_dir)
            changes.renamed_dirs.insert(make_pair(from, to));
          else
            changes.renamed_files.insert(make_pair(from, to));
        }
    }
}

tree_state
tree_state::merge_with_resolution(std::vector<tree_state> const & revs,
                               std::set<path_conflict::resolution> const & res,
                                  std::string const & revision)
{
  tree_state merged(mash(revs));
  merged.apply_sutures();

  std::set<item_id> resolved;
  typedef std::vector<path_component> splitpath;

  // we need the names of close-to-root items
  // before we can resolve their children
  std::multimap<int, std::pair<item_id, splitpath > > sorted;
  for (std::set<path_conflict::resolution>::const_iterator i = res.begin();
       i != res.end(); ++i)
    {
      file_path fp(i->second);
      splitpath sp;
      split_path(fp, sp);
      sorted.insert(make_pair(sp.size(), make_pair(i->first, sp)));
    }
  typedef int fpid;
  interner<fpid> cit;
  map<fpid, item_id> names;
  int lastlevel = 0;
  for (std::multimap<int, std::pair<item_id, splitpath > >::const_iterator
         i = sorted.begin(); i != sorted.end(); ++i)
    {
      if (i->first > lastlevel)
        {
          // names should contain everything closer to root
          // than the level we're at now
          for (std::map<item_id, item_status>::const_iterator
                 j = merged.states->begin(); j != merged.states->end(); ++j)
            {
              if (resolved.find(j->first) == resolved.end())
                {
                  std::set<item_status::item_state>
                    x(j->second.current_names());
                  if (x.size() != 1)
                    continue;// not resolved, so not closer
                  int d;
                  file_path fp(merged.try_get_full_name(*x.begin(), d));
                  if (d >= lastlevel || d == -1)
                    continue;// not reached, or not resolved
                  resolved.insert(j->first);
                  fpid f(cit.intern(fp()));
                  names.insert(make_pair(f, j->first));
                }
            }
          ++lastlevel;
        }
      item_id const & id(i->second.first);
      splitpath sp(i->second.second);
      unsigned int s = resolved.size();
      resolved.insert(id);
      std::map<item_id, item_status>::const_iterator
        j = merged.states->find(id);
      I(j != merged.states->end());
      item_status it = j->second;
      if (s == resolved.size())
        {
          // already resolved this item, this resolution had better match
          std::set<item_status::item_state> names = it.current_names();
          I(names.size() == 1);
          file_path prev = merged.get_full_name(*names.begin());
          file_path to;
          compose_path(sp, to);
          I(to == prev);
        }
       else
        {
          file_path fp;
          compose_path(sp, fp);
          path_component name(sp.back());
          sp.pop_back();
          file_path pdir;
          if (sp.size())
            compose_path(sp, pdir);
          bool newfile;
          fpid pd = cit.intern(pdir(), newfile);
          if (newfile)
            merged.ensure_dir_exists(sp, names, cit, revision);
          std::map<fpid, item_id>::const_iterator k = names.find(pd);
          I(k != names.end());

          std::map<item_id, item_status>::iterator
            j = merged.states->find(k->second);
          I(j != merged.states->end());
          j->second = item_status(j->second.rename(merged.itx->intern(revision),
                                                   k->second, name));
          cit.intern(fp());
        }
    }
  return merged;
}

file_path
tree_state::get_full_name(item_status x) const
{
  std::set<item_status::item_state> y;
  y = x.current_names();
  I(y.size() == 1);
  return get_full_name(*y.begin());
}

file_path
tree_state::get_full_name(item_status::item_state x) const
{
  int d;
  file_path out(try_get_full_name(x, d));
  I(d != -1);
  return out;
}

file_path
tree_state::try_get_full_name(item_status::item_state x, int & d) const
{
  d = 0;
  std::vector<path_component> names;
  names.push_back(x.second);
  while (x.first != -1)
    {
      std::map<item_id, item_status>::const_iterator
        i = states->find(x.first);
      I(i != states->end());
      std::set<item_status::item_state> y = i->second.current_names();
      if (y.size() != 1)
        {
          d = -1;
          return file_path();
        }
      x = *y.begin();
      names.push_back(x.second);
    }
  reverse(names.begin(), names.end());
  file_path out;
  compose_path(names, out);
  return out;
}

std::string
tree_state::get_ambiguous_full_name(item_status::item_state x) const
{
// fails if the item has multiple names (only call this on clean trees)
  std::vector<path_component> names;
  file_path fp;
  std::string out;
  names.push_back(x.second);
  while (x.first != -1)
    {
      std::map<item_id, item_status>::const_iterator
        i = states->find(x.first);
      if (i != states->end());
      else E(false, F("Missing: %1%") % x.first);
      std::set<item_status::item_state> y = i->second.current_names();
      if (y.size() != 1)
        {
          out = (F("<id:%1%>/") % x.first).str();
          break;
        }
      x = *y.begin();
      names.push_back(x.second);
    }
  reverse(names.begin(), names.end());
  compose_path(names, fp);
  out += fp();
  return out;
}





/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
/////////////////////////// Tests ///////////////////////////////////
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////


vector<string>
vectorize(string x)
{
  vector<string> out;
  for (string::iterator i = x.begin(); i != x.end(); ++i)
    out.push_back(string()+(*i)+"\n");
  return out;
}

struct vp: public vector<pair<int, int> >
{
  vp pb(int l, int r)
  {
    push_back(pair<int, int>(l, r));
    return *this;
  }
};

void
test_unique_lcs()
{
  // unique_lcs
  vector<line_contents> l, r;
  vector<pair<int, int> > res;
  unique_lcs(l, r, 0, 0, 0, 0, res);
  I(res == vp());

  l.push_back(0);
  r.push_back(0);
  unique_lcs(l, r, 0, 0, 1, 1, res);
  I(res == vp().pb(0, 0));

  l.clear();
  r.clear();
  l.push_back(0);
  r.push_back(1);
  unique_lcs(l, r, 0, 0, 1, 1, res);
  I(res == vp());

  l.clear();
  r.clear();
  l.push_back(0);
  l.push_back(1);
  r.push_back(0);
  r.push_back(1);
  unique_lcs(l, r, 0, 0, 2, 2, res);
  I(res == vp().pb(0, 0).pb(1, 1));

  l.clear();
  r.clear();
  l.push_back(0);
  l.push_back(1);
  l.push_back(2);
  l.push_back(3);
  l.push_back(4);
  r.push_back(2);
  r.push_back(3);
  r.push_back(4);
  r.push_back(0);
  r.push_back(1);
  unique_lcs(l, r, 0, 0, 5, 5, res);
  I(res == vp().pb(2, 0).pb(3, 1).pb(4, 2));

  unique_lcs(r, l, 0, 0, 5, 5, res);
  I(res == vp().pb(0, 2).pb(1, 3).pb(2, 4));

  l.clear();
  r.clear();
  l.push_back(0);
  l.push_back(1);
  l.push_back(10);
  l.push_back(3);
  l.push_back(4);
  r.push_back(0);
  r.push_back(1);
  r.push_back(11);
  r.push_back(3);
  r.push_back(4);
  unique_lcs(l, r, 0, 0, 5, 5, res);
  I(res == vp().pb(0, 0).pb(1, 1).pb(3, 3).pb(4, 4));

  l.clear();
  r.clear();
  l.push_back(0);
  l.push_back(2);
  l.push_back(1);
  l.push_back(0);
  l.push_back(2);
  r.push_back(0);
  r.push_back(1);
  r.push_back(2);
  unique_lcs(l, r, 0, 0, 5, 3, res);
  I(res == vp().pb(2, 1));
}

void
test_recurse_matches()
{
  vector<pair<int, int> > res;
  vector<line_contents> l, r;

  l.push_back(0);
  l.push_back(-1);
  l.push_back(1);
  l.push_back(-1);
  l.push_back(2);
  r.push_back(0);
  r.push_back(0);
  r.push_back(1);
  r.push_back(2);
  r.push_back(2);
  recurse_matches(l, r, 0, 0, 5, 5, res, 10);
  I(res == vp().pb(0, 1).pb(2, 2).pb(4, 3));

  res.clear();
  l.clear();
  r.clear();
  l.push_back(0);
  l.push_back(2);
  l.push_back(1);
  l.push_back(0);
  l.push_back(2);
  r.push_back(0);
  r.push_back(1);
  r.push_back(2);
  recurse_matches(l, r, 0, 0, 5, 3, res, 10);
  I(res == vp().pb(0, 0).pb(2, 1).pb(4, 2));
}

void
test_living_status()
{
  living_status ds;
  I(!ds.is_living());

  living_status ta(ds.set_living(1, true));
  I(ta.is_living());

  living_status tb(ds.set_living(2, true));
  living_status tc(ta.set_living(3, false));
  I(!tc.is_living());

  living_status td(ta.set_living(4, false));
  living_status te(tb.merge(tc));
  I(te.is_living());

  living_status tf(te.merge(td));
  I(tf.is_living());

  living_status tg(tb.set_living(7, false));
  living_status th(te.merge(tg));
  I(!th.is_living());
}

void
test_file_state()
{
  {
    file_state orig;
    file_state ta(orig.resolve(vectorize("abc"), "a"));
    file_state tb(ta.resolve(vectorize("bcd"), "b"));
    vector<string> res(tb.current());
    I(res == vectorize("bcd"));
  }

  {
    file_state orig;
    file_state ta(orig.resolve(vectorize("abc"), "a"));
    file_state tb(ta.resolve(vectorize("dabc"), "b"));
    file_state tc(ta.resolve(vectorize("abce"), "c"));
    file_state td(tb.mash(tc));
    I(td.current() == vectorize("dabce"));
  }

  {
    file_state orig;
    file_state ta(orig.resolve(vectorize("abc"), "a"));
    file_state tb(ta.resolve(vectorize("adc"), "b"));
    file_state tc(tb.resolve(vectorize("abc"), "c"));
    file_state td(ta.resolve(vectorize("aec"), "d"));

    vector<merge_section> res(consolidate(tc.conflict(td)));
    vector<merge_section> real;
    real.push_back(vectorize("aec"));
    I(res == real);
  }

  {
    file_state orig;
    file_state ta(orig.resolve(vectorize("abc"), "a"));
    file_state tb(ta.resolve(vectorize("adc"), "b"));
    file_state tc(ta.resolve(vectorize("aec"), "c"));

    vector<merge_section> res(consolidate(tb.conflict(tc)));
    vector<merge_section> real;
    real.push_back(vectorize("a"));
    real.push_back(merge_section(vectorize("d"), vectorize("e")));
    real.push_back(vectorize("c"));
    I(res == real);
  }
}

void
pcdv_test()
{
  test_unique_lcs();
  test_recurse_matches();
  test_living_status();
  test_file_state();
}


void
dirmerge_test()
{
}
