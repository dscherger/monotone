#include <set>
#include <map>
#include <deque>

#include "sanity.hh"
#include "pcdv.hh"

#include <iostream>

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
      ref.insert(todo.front());
      line_data::const_iterator i = overrides->find(todo.front());
      I(i != overrides->end());
      for (vector<revid>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        todo.push_back(*j);
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
        newstate.states->insert(*(r++));
      else if (r == other.states->end())
        newstate.states->insert(*(l++));
      else if (l->first < r->first)
        newstate.states->insert(*(r++));
      else if (r->first < l->first)
        newstate.states->insert(*(l++));
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


/////////////////////////////////////////////////////////////////
///////////////////////// Tests /////////////////////////////////
/////////////////////////////////////////////////////////////////


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
