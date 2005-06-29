#include <set>
#include <map>

#include "sanity.hh"
#include "pcdv.hh"

#include <iostream>

unsigned int biggest_living_status=0;
unsigned int sum_living_status=0;
unsigned int num_living_status=0;

living_status::~living_status()
{
  if (overrides.unique())
    {
      ++num_living_status;
      sum_living_status+=overrides->size();
      if (overrides->size() > biggest_living_status)
        biggest_living_status = overrides->size();
    }
}

file_state::~file_state()
{
  if (false && weave.unique())
    {
      states.reset();
      std::cout<<"Destroyed file_state of "<<weave->size()
               <<" lines."<<std::endl;
      std::cout<<"Average living_status size: "
               <<float(sum_living_status)/float(num_living_status)<<std::endl;
      std::cout<<"Max living_status size: "<<biggest_living_status<<std::endl;
      std::cout<<"Destroyed "<<num_living_status
               <<" living_status so far."<<std::endl;
    }
}

// find lines that occur exactly once in each of a and b
void
unique_lcs(vector<string> const & a,
           vector<string> const & b,
           vector<pair<int, int> > & res)
{
  // index[line in a] = position of line
  // if line is a duplicate, index[line] = -1
  map<string, int> index;
  for (int i = 0; (unsigned int)(i) < a.size(); ++i)
    {
      map<string, int>::iterator j = index.find(idx(a,i));
      if (j != index.end())
        j->second=-1;
      else
        index.insert(make_pair(idx(a,i), i));
    }
  // btoa[i] = a.find(b[i]), if b[i] is unique in both
  // otherwise, btoa[i] = -1
  map<string, int> index2;
  vector<int> btoa(b.size(), -1);
  for (int i = 0; (unsigned int)(i) < b.size(); ++i)
    {
      map<string, int>::iterator j = index.find(idx(b,i));
      if (j != index.end())
        {
          map<string, int>::iterator k = index2.find(idx(b,i));
          if (k != index2.end())
            {
              btoa[k->second] = -1;
              index.erase(j);
            }
          else
            {
              index2.insert(make_pair(idx(b,i), i));
              btoa[i] = j->second;
            }
        }
    }
  // Patience sorting
  // http://en.wikipedia.org/wiki/Patience_sorting
  vector<int> backpointers(b.size(), -1);
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
  res.clear();
  if (lasts.empty())
    return;
  k = lasts.back();
  while (k != -1)
    {
      res.push_back(make_pair(idx(btoa, k), k));
      k = idx(backpointers, k);
    }
  reverse(res.begin(), res.end());
  return;
}

void
recurse_matches(vector<string> const & a,
                vector<string> const & b,
                int ahi,
                int bhi,
                vector<pair<int, int> > & answer,
                int maxrecursion)
{
  if (maxrecursion < 0)
    return;
  unsigned int oldlength = answer.size();
  int alo = 0, blo = 0;
  if (oldlength != 0)
    {
      alo = answer.back().first + 1;
      blo = answer.back().second + 1;
    }
  // extend line matches into section matches
  vector<pair<int, int> > linematches;
  vector<string> a2, b2;
  for(int i = alo; i < ahi; ++i)
    a2.push_back(idx(a, i));
  for(int i = blo; i < bhi; ++i)
    b2.push_back(idx(b, i));

  unique_lcs(a2, b2, linematches);
  for (vector<pair<int, int> >::iterator i = linematches.begin();
       i != linematches.end(); ++i)
    {
      int apos = i->first + alo;
      int bpos = i->second + blo;
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
          while (newapos > lasta && idx(a, newapos).empty())
            --newapos;
          if (newapos == lasta || idx(a, newapos) != idx(b, bpos-1))
            break;
          apos = newapos;
          --bpos;
        }
      recurse_matches(a, b, apos, bpos, answer, maxrecursion-1);
      answer.push_back(make_pair(apos, bpos));
      // extend as far forward as possible
      while (apos < ahi - 1 && bpos < bhi - 1)
        {
          int newapos = apos + 1;
          while (newapos < ahi - 1 && idx(a, newapos).empty())
            ++newapos;
          if (newapos == ahi || idx(a, newapos) != idx(b, bpos + 1))
            break;
          apos = newapos;
          ++bpos;
          answer.push_back(make_pair(apos, bpos));
        }
    }
  if (answer.size() > oldlength)
    // find matches between the laswt match and the end
    recurse_matches(a, b, ahi, bhi, answer, maxrecursion - 1);
}


living_status
living_status::merge(living_status const & other) const
{
  boost::shared_ptr<map<string, vector<string> > > newdict;
  newdict.reset(new map<string, vector<string> >());
  bool notleft=false, notright=false;
  for (map<string, vector<string> >::const_iterator i = overrides->begin();
        i != overrides->end(); ++i)
    {
      newdict->insert(*i);
      map<string, vector<string> >::const_iterator j =
          other.overrides->find(i->first);
      if (j == other.overrides->end())
        notright = true;
      else
        I(j->second == i->second);
    }
  for (map<string, vector<string> >::const_iterator i
        = other.overrides->begin(); i != other.overrides->end(); ++i)
    {
      newdict->insert(*i);
      map<string, vector<string> >::const_iterator j
          = overrides->find(i->first);
      if (j == overrides->end())
        notleft = true;
      else
        I(j->second == i->second);
    }
  if (!notleft)
    return *this;
  else if (!notright)
    return other;
  else
    return living_status(newdict);
}

bool
living_status::is_living() const
{
  if (precomp->first)
    return precomp->second;
  set<string> oldworking, newworking, ref;
  for (map<string, vector<string> >::const_iterator i = overrides->begin();
        i != overrides->end(); ++i)
    ref.insert(i->first);
  newworking = ref;
  while (oldworking != newworking)
    {
      oldworking = newworking;
      newworking = ref;
      for (set<string>::const_iterator k = oldworking.begin();
            k != oldworking.end(); ++k)
        {
          map<string, vector<string> >::const_iterator x
              = overrides->find(*k);
          for (vector<string>::const_iterator j = x->second.begin();
                j != x->second.end(); ++j)
            newworking.erase(*j);
        }
    }
  precomp->first = true;
  return precomp->second = (newworking.find("root") == newworking.end());
}

bool
living_status::_makes_living(string key) const
{
  bool result = false;
  while (key != "root")
    {
      result = !result;
      map<string, vector<string> >::const_iterator i;
      i = overrides->find(key);
      if (i == overrides->end() || i->second.empty())
        break;
      key = idx(i->second, 0);
    }
  return result;
}

living_status
living_status::set_living(string rev, bool new_status) const
{
  if (new_status == is_living())
    return *this;
  set<string> alive;
  for (map<string, vector<string> >::const_iterator i = overrides->begin();
        i != overrides->end(); ++i)
    alive.insert(i->first);
  for (map<string, vector<string> >::const_iterator i = overrides->begin();
        i != overrides->end(); ++i)
    {
      for (vector<string>::const_iterator j = i->second.begin();
            j != i->second.end(); ++j)
        alive.erase(*j);
    }
  boost::shared_ptr<map<string, vector<string> > > newdict;
  newdict.reset(new map<string, vector<string> >(*overrides));
  map<string, vector<string> >::iterator res =
      newdict->insert(make_pair(rev, vector<string>())).first;
  for (set<string>::iterator i = alive.begin();
        i != alive.end(); ++i)
    {
      if (_makes_living(*i) != new_status)
        res->second.push_back(*i);
    }
  return living_status(newdict, new_status);
}



file_state::file_state(vector<string> const & initial, string const & rev):
 states(new map<pair<string, int>, living_status>())
{
  weave.reset(new vector<pair<string, pair<string, int> > >);
  for (int i = 0; (unsigned int)(i) < initial.size(); ++i)
    weave->push_back(make_pair(idx(initial, i), make_pair(rev, i)));
  for (int i = 0; (unsigned int)(i) < initial.size(); ++i)
    {
      (*states)[make_pair(rev, i)] = living_status().set_living(rev, true);
    }
}

// combine line states between two versions of a file
file_state
file_state::mash(file_state const & other) const
{
  I(weave == other.weave);
  file_state newstate(weave);
  for (map<pair<string, int>, living_status>::const_iterator i
        = states->begin(); i != states->end(); ++i)
    {
      map<pair<string, int>, living_status>::const_iterator j
          = other.states->find(i->first);
      if (j != other.states->end())
        (*newstate.states)[i->first] = i->second.merge(j->second);
      else
        (*newstate.states)[i->first] = i->second.merge(living_status());
    }
  for (map<pair<string, int>, living_status>::const_iterator i
        = other.states->begin(); i != other.states->end(); ++i)
    {
      map<pair<string, int>, living_status>::const_iterator j
          = states->find(i->first);
      if (j != states->end())
        (*newstate.states)[i->first] = i->second.merge(j->second);
      else
        (*newstate.states)[i->first] = i->second.merge(living_status());
    }
  return newstate;
}

// get the list of live lines in this version of the file
vector<string>
file_state::current() const
{
  vector<string> res;
  for (vector<pair<string, pair<string, int> > >::iterator i
        = weave->begin(); i != weave->end(); ++i)
    {
      map<pair<string, int>, living_status>::const_iterator j
        = states->find(i->second);
      if (j != states->end() && j->second.is_living())
        res.push_back(i->first);
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
  for (vector<pair<string, pair<string, int> > >::const_iterator i
        = weave->begin(); i != weave->end(); ++i)
    {
      map<pair<string, int>, living_status>::const_iterator m
          = states->find(i->second);
      map<pair<string, int>, living_status>::const_iterator o
          = other.states->find(i->second);
      bool mm(m != states->end());
      bool oo(o != other.states->end());
      living_status const & meline(mm?m->second:living_status());
      living_status const & otherline(oo?o->second:living_status());
      bool mehave = meline.is_living();
      bool otherhave = otherline.is_living();
      bool mergehave = meline.merge(otherline).is_living();
      if (mehave && otherhave && mergehave)
        {
          if (mustright && mustleft)
            result.push_back(merge_section(left, right));
          else
            result.push_back(merge_section(clean));
          result.push_back(merge_section(i->first));
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
            left.push_back(i->first);
          if (otherhave)
            right.push_back(i->first);
          if (mergehave)
            clean.push_back(i->first);
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
file_state::resolve(vector<string> const & result, string revision) const
{
  if (weave->empty())
    {
      file_state x(result, revision);
      *weave = *x.weave;
      x.weave = weave;
      return x;
    }
  vector<string> lines;
  lines.reserve(weave->size());
  for (vector<pair<string, pair<string, int> > >::iterator i
        = weave->begin(); i != weave->end(); ++i)
    {
      map<pair<string, int>, living_status>::const_iterator j
        = states->find(i->second);
      if (j != states->end() && j->second.is_living())
        lines.push_back(i->first);
      else
        lines.push_back(string());
    }
  vector<pair<int, int> > matches;
  recurse_matches(lines, result,
                  lines.size(), result.size(),
                  matches, 10);
  lines.clear();
  for (vector<pair<string, pair<string, int> > >::iterator i
        = weave->begin(); i != weave->end(); ++i)
    lines.push_back(i->first);
  vector<pair<int, int> > matches2;
  matches.push_back(make_pair(lines.size(), result.size()));
  for (vector<pair<int, int> >::iterator i = matches.begin();
        i != matches.end(); ++i)
    {
      recurse_matches(lines, result, i->first, i->second, matches2, 10);
      if ((unsigned int)(i->first) != lines.size())
        matches2.push_back(make_pair(i->first, i->second));
    }
  matches.pop_back();
  set<pair<string, int> > living;
  for (vector<pair<int, int> >::iterator i = matches2.begin();
        i != matches2.end(); ++i)
    {
      living.insert(idx(*weave, i->first).second);
    }
  vector<pair<int, pair<string, pair<string, int> > > > toinsert;
  int lasta = -1, lastb = -1;
  matches2.push_back(make_pair(weave->size(), result.size()));
  for (vector<pair<int, int> >::iterator i = matches2.begin();
        i != matches2.end(); ++i)
    {
      for (int x = lastb + 1; x < i->second; ++x)
        {
          pair<string, int> newline(revision, x);
          living.insert(newline);
          toinsert.push_back(make_pair(lasta + 1,
                                        make_pair(idx(result,x),
                                                  newline)));
        }
      lasta = i->first;
      lastb = i->second;
    }
  reverse(toinsert.begin(), toinsert.end());
  for (vector<pair<int, pair<string, pair<string, int> > > >::iterator i
        = toinsert.begin(); i != toinsert.end(); ++i)
    weave->insert(weave->begin() + i->first, i->second);
  file_state out(weave);
  for (vector<pair<string, pair<string, int> > >::iterator i
        = weave->begin(); i != weave->end(); ++i)
    {
      map<pair<string, int>, living_status>::const_iterator j
        = states->find(i->second);
      if (j != states->end())
        (*out.states)[i->second] = j->second.set_living(revision,
                                      living.find(i->second) != living.end());
      else
        (*out.states)[i->second] = living_status().set_living(revision,
                                      living.find(i->second) != living.end());
    }
  return out;
}



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
  vector<string> l, r;
  vector<pair<int, int> > res;
  unique_lcs(l,r, res);
  I(res == vp());

  l = vectorize("a");
  r = vectorize("a");
  unique_lcs(l,r, res);
  I(res == vp().pb(0, 0));

  l = vectorize("a");
  r = vectorize("b");
  unique_lcs(l,r, res);
  I(res == vp());

  l = vectorize("ab");
  r = vectorize("ab");
  unique_lcs(l,r, res);
  I(res == vp().pb(0, 0).pb(1, 1));

  l = vectorize("abcde");
  r = vectorize("cdeab");
  unique_lcs(l,r, res);
  I(res == vp().pb(2, 0).pb(3, 1).pb(4, 2));

  l = vectorize("cdeab");
  r = vectorize("abcde");
  unique_lcs(l,r, res);
  I(res == vp().pb(0, 2).pb(1, 3).pb(2, 4));

  l = vectorize("abXde");
  r = vectorize("abYde");
  unique_lcs(l,r, res);
  I(res == vp().pb(0, 0).pb(1, 1).pb(3, 3).pb(4, 4));

  l = vectorize("acbac");
  r = vectorize("abc");
  unique_lcs(l,r, res);
  I(res == vp().pb(2, 1));
}

void
test_recurse_matches()
{
  vector<pair<int, int> > res;
  vector<string> l, r;

  l.push_back("a\n");
  l.push_back("");
  l.push_back("b\n");
  l.push_back("");
  l.push_back("c\n");

  r = vectorize("aabcc");
  recurse_matches(l, r, 5, 5, res, 10);
  I(res == vp().pb(0, 1).pb(2, 2).pb(4, 3));

  res.clear();
  l = vectorize("acbac");
  r = vectorize("abc");
  recurse_matches(l, r, 5, 3, res, 10);
  I(res == vp().pb(0, 0).pb(2, 1).pb(4, 2));
}

void
test_living_status()
{
  living_status ds;
  I(!ds.is_living());
  living_status ta(ds.set_living("a", true));
  I(ta.is_living());
  living_status tb(ds.set_living("b", true));
  living_status tc(ta.set_living("c", false));
  I(!tc.is_living());
  living_status td(ta.set_living("d", false));
  living_status te(tb.merge(tc));
  I(te.is_living());
  living_status tf(te.merge(td));
  I(tf.is_living());
  living_status tg(tb.set_living("g", false));
  living_status th(te.merge(tg));
  I(!th.is_living());
}

void
test_file_state()
{
  {
    file_state ta(vectorize("abc"), "x");
    I(ta.resolve(vectorize("bcd"), "y").current() == vectorize("bcd"));
  }

  {
    file_state ta(vectorize("abc"), "x");
    file_state tb(ta.resolve(vectorize("dabc"), "y"));
    file_state tc(ta.resolve(vectorize("abce"), "z"));
    file_state td(tb.mash(tc));
    I(td.current() == vectorize("dabce"));
  }

  {
    file_state ta(vectorize("abc"), "x");
    file_state tb(ta.resolve(vectorize("adc"), "y"));
    file_state tc(tb.resolve(vectorize("abc"), "z"));
    file_state td(ta.resolve(vectorize("aec"), "w"));

    vector<merge_section> res(consolidate(tc.conflict(td)));
    vector<merge_section> real;
    real.push_back(vectorize("aec"));
    I(res == real);
  }

  {
    file_state ta(vectorize("abc"), "x");
    file_state tb(ta.resolve(vectorize("adc"), "y"));
    file_state tc(ta.resolve(vectorize("aec"), "z"));

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
