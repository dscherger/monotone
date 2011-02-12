// Copyright (C) 2003 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

/* this is a pretty direct translation (with only vague understanding,
   unfortunately) of aubrey jaffer's most recent O(NP) edit-script
   calculation algorithm, which performs quite a bit better than myers,
   manber and miller's O(NP) simple edit *distance* algorithm. this one
   builds the entire *script* that fast.

   the following is jaffer's copyright and license statement. it probably
   still has some legal relevance here, as this is a highly derivative
   work. if you want to see more of the original file jaffer's work came
   from, see the SLIB repository on savannah.nongnu.org, his website at
   http://www.swiss.ai.mit.edu/~jaffer/, or look in the journal of
   computational biology. apparantly it's submitted for publication there
   too.

   ---

   "differ.scm" O(NP) Sequence Comparison Algorithm.
   Copyright (C) 2001, 2002, 2003 Aubrey Jaffer

   Permission to copy this software, to modify it, to redistribute it, to
   distribute modified versions, and to use it for any purpose is granted,
   subject to the following restrictions and understandings.

   1.  Any copy made of this software must include this copyright notice
   in full.

   2.  I have made no warrantee or representation that the operation of
   this software will be error-free, and I am under no obligation to
   provide any services, by way of maintenance, update, or otherwise.

   3.  In conjunction with products arising from the use of this material,
   there shall be no use of my name in any advertising, promotional, or
   sales literature without prior written consent in each case.

*/

/*
  This is now understood better. The comments below are all new,
  most of the variable names that aren't one letter and don't
  look like x86 registers are new, and there are some complexity
  fixes so the recursing doesn't make it accidentally O(n^2) in
  the input length.
 */

#include "base.hh"
#include <algorithm>
#include "vector.hh"

#include "lcs.hh"
#include "sanity.hh"

using std::back_insert_iterator;
using std::back_inserter;
using std::copy;
using std::iterator_traits;
using std::max;
using std::min;
using std::sort;
using std::vector;

/*
  http://read.pudn.com/downloads131/sourcecode/delphi_control/558602/O(NP).pdf
  An O(NP) Sequence Comparison Algorithm
  Sun Wu, Udi Manber, Gene Myers; University of Arizona
  Webb Miller; Pennsylvania State University
  August 1989

  The above paper shows how to find the edit distance between strings in time
  at worst O(num-deletions * longer-length), and on average
  O(longer-length + (edit-distance * num-deletions)).


  Name the two input strings "a" and "b", "a" being the shorter one. Consider
  and edit graph with a going down (x coordinate) and b going across (y coord).

   stringBislonger
  s\       \.
  t \       .
  r  \      .
  i   \   \ .
  n    \    . \    .
  g     X   .
  A      .

  You start in the top left corner, and want to end up in the lower right
  corner. There are 3 ways you can move: follow a diagonal for zero cost,
  or move directly down or directly right for a cost of one. The total cost
  of the cheapest path is the edit distance. A movement directly down
  corresponds to a deletion, and a movement directly right corresponds to
  an insertion.

  If you had a diagonal from the top all the way to the bottom, the cost
  would be the difference in the lengths of the input strings ("delta").
  For every movement directly down you need to add exactly one movement
  directly right, so the total cost D = delta + (2 * num-deletions).

  Give each diagonal in the edit graph a number. The diagonal through the
  origin is 0; diagonals above / right of it are numbered 1, 2, ...; diagonals
  below / left of it are numbered -1, -2, ... . The diagonal through the lower
  right corner will be number delta (difference of input lengths).

  An edit path with a particular number of deletions cannot go below
  diagonal -(num-deletions) or above diagonal delta + (num-deletions).
  So we have bounding diagonals for any edit path up to a given number of
  deletions and therefore up to a given length.


  compare() with a large p_lim and full_scan = false implements this algorithm.

  compare() with a given p_lim (maximum number of deletions) calculates
  the lowest cost of a path through each relevant point along the bottom of
  the edit graph.
 */



struct work_vec
{
  long lo;
  long hi;
  static vector<long, QA(long)> vec;
  work_vec(long lo, long hi) :
    lo(lo), hi(hi)
  {
    // I(hi >= lo);
    size_t len = (hi - lo) + 1;
    vec.resize(len);
    vec.assign(len, -1);
  }

  inline long & operator[](long t)
  {
    // I(t >= lo && t <= hi);
    return vec[t-lo];
  }
};

vector<long, QA(long)> work_vec::vec;

template <typename A,
          typename B,
          typename LCS>
struct jaffer_edit_calculator
{

  typedef vector<long, QA(long)> cost_vec;
  typedef vector<long, QA(long)> edit_vec;

  template <typename T>
  struct subarray
  {
    typedef typename iterator_traits<T>::value_type vt;

    T base;     // underlying representation
    long start; // current extent
    long end;   // current extent

    subarray(T b, long s, long e) :
      base(b), start(s), end(e) {}

    inline long size() const
    {
      if (end < start)
        return start - end;
      else
        return end - start;
    }

    inline subarray subset(long s, long e) const
    {
      return subarray(base + min(start, end), s, e);
    }

    inline vt const & operator[](size_t idx) const
    {
      if (end < start)
          return *(base + (start - (idx + 1)));
      else
          return *(base + (start + idx));
    }
  };

  static long run(work_vec & fp, long k,
                  subarray<A> const & a, long a_len,
                  subarray<B> const & b, long b_len,
                  cost_vec & CC, long p)
  {
    long cost = k + 2*p;

    // do the run
    long y = max(fp[k-1]+1, fp[k+1]);
    long x = y - k;

    I(y >= 0);
    I(x >= 0);

    while (true)
      {
        // record costs along the way
        long xcst = a_len - x;
        if (y < static_cast<long>(CC.size()) && xcst >= 0)
          {
            CC[y] = min(xcst + cost, CC[y]);
          }
        if (x < a_len && y < b_len && a[x] == b[y])
          {
            ++x; ++y;
          }
        else
          break;
      }

    fp[k] = y;
    return y;
  }

  // 'compare' here is the core myers, manber and miller algorithm.
  static long compare(cost_vec & costs,
                      subarray<A> const & a, long len_a,
                      subarray<B> const & b, long len_b,
                      long p_lim,
                      bool full_scan = true)
  {
    long const delta = len_b - len_a;
    long lo = -(len_a+1), hi = (1+len_b);
    if (full_scan)
      {
        lo = -(p_lim + 1);
        hi = p_lim + 1 + delta;
      }
    work_vec fp(lo, hi);

    long p = 0;

    for (; p <= p_lim; ++p)
      {

        // lower sweep
        for (long k = -p; k < delta; ++k)
          run(fp, k, a, len_a, b, len_b, costs, p);

        // upper sweep
        for (long k = delta + p; k > delta; --k)
          run(fp, k, a, len_a, b, len_b, costs, p);

        // middle
        long fpval = run(fp, delta, a, len_a, b, len_b, costs, p);

        // we can bail early if not doing a full scan
        if (!full_scan && len_b <= fpval)
          break;
      }

    return delta + 2*p;
  }

  // This splits the edit graph into a top half and a bottom half, calculates
  // the (cost of the) cheapest possible path through each point along the
  // middle, and then splits the graph into left/right portions based on that
  // point. It then recurses on the top left and bottom right quadrants (the
  // shortest edit path cannot possibly go through the other two quadrants).
  //
  // When getting costs through the top and bottom halves, it can discard the
  // rightmost part of the top and the leftmost part of the bottom, beyond where
  // the edit band (diagonls -(num-deletes) and delta + num-deletes) crosses
  // the split. Even with doing this the edit band is overstated in the
  // calls to compare(), because while max-possible-deletes (p_lim) is correct
  // the delta value is still larger by max-possible-deletes.
  static long divide_and_conquer(subarray<A> const & a, long start_a, long end_a,
                                 subarray<B> const & b, long start_b, long end_b,
                                 edit_vec & edits,
                                 unsigned long edx,
                                 long polarity,
                                 long p_lim)
  {
    long len_b = end_b - start_b;
    long len_a = end_a - start_a;
    long const delta = len_b - len_a;
    // total edit distance
    long tcst = (2 * p_lim) + (len_b - len_a);
    // top/bottom split point
    long mid_a = (start_a + end_a) / 2;

    I(start_a >= 0);
    I(start_a <= a.size());
    I(start_b >= 0);
    I(start_b <= b.size());
    I(end_a >= 0);
    I(end_a <= a.size());
    I(end_b >= 0);
    I(end_b <= b.size());

    cost_vec cc(len_b + 1, len_a + len_b);
    cost_vec rr(len_b + 1, len_a + len_b);

    // get costs from the top left through each point on the top/bottom split
    long const top_len_a = mid_a - start_a;
    // trim off the rightmost part of b, past where the edit band crosses the split
    long const top_end_b = min(end_b, start_b + (top_len_a + delta + p_lim + 1));
    compare (cc,
             a.subset(start_a, mid_a), top_len_a,
             b.subset(start_b, top_end_b), top_end_b - start_b,
             min(p_lim, len_a));

    // get costs from the lower right through each point on the top/bottom split
    long const bottom_len_a = end_a - mid_a;
    // here we trim the leftmost part of b (before reversing it)
    long const bottom_start_b = max(start_b, end_b - (bottom_len_a + delta + p_lim + 1));
    compare (rr,
             a.subset(end_a, mid_a), bottom_len_a,
             b.subset(end_b, bottom_start_b), end_b - bottom_start_b,
             min(p_lim, len_a));

    // find the first (closest-to-center) point on the split line, which
    // has the correct total (top + bottom) cost and is therefore on the
    // shortest edit path
    long b_split = mid_split(len_a, len_b, rr, cc, tcst);

    // known costs of each half of the path
    long est_c = cc[b_split];
    long est_r = rr[len_b - b_split];

    // recurse on the two halves

    long cost_c = diff_to_et (a, start_a, mid_a,
                              b, start_b, start_b + b_split,
                              edits, edx, polarity,
                              (est_c - (b_split - (mid_a - start_a))) / 2);

    I(cost_c == est_c);

    long cost_r = diff_to_et (a, mid_a, end_a,
                              b, start_b + b_split, end_b,
                              edits, est_c + edx, polarity,
                              (est_r - ((len_b - b_split) - (end_a - mid_a))) / 2);

    I(cost_r == est_r);

    return est_r + est_c;
  }

  static long mid_split(long m, long n,
                        cost_vec const & rr,
                        cost_vec const & cc,
                        long cost)
  {
    long cdx = 1 + n/2;
    long rdx = n/2;
    while (true)
      {
        I (rdx >= 0);

        if (cost == (cc[rdx] + rr[n-rdx]))
            return rdx;
        if (cost == (cc[cdx] + rr[n-cdx]))
            return cdx;
        --rdx;
        ++cdx;
      }
  }


  static void order_edits(edit_vec const & edits,
                          long sign,
                          edit_vec & nedits)
  {
    nedits.clear();
    nedits.resize(edits.size());
    long cost = edits.size();

    if (cost == 0)
      {
        nedits = edits;
        return;
      }

    edit_vec sedits = edits;
    sort(sedits.begin(), sedits.end());

    long idx0;
    for (idx0 = 0; idx0 < cost && sedits[idx0] < 0; ++idx0) ;
    long len_a = max(0L, -sedits[0]);
    long len_b = sedits[cost-1];

    long ddx = idx0 - 1;
    long idx = idx0;
    long ndx = 0;
    long adx = 0;
    long bdx = 0;

    while (bdx < len_b || adx < len_a)
      {

        long del = (ddx < 0) ? 0 : sedits[ddx];
        long ins = (idx >= cost) ? 0 : sedits[idx];

        if (del < 0 && adx >= (-1 - del) &&
            ins > 0 && bdx >= (-1 + ins))
          {
            nedits[ndx] = del;
            nedits[ndx+1] = ins;
            --ddx; ++idx; ndx += 2; ++adx; ++bdx;
          }
        else if (del < 0 && adx >= (-1 - del))
          {
            nedits[ndx] = del;
            --ddx; ++ndx; ++adx;
          }
        else if (ins > 0 && bdx >= (-1 + ins))
          {
            nedits[ndx] = ins;
            ++idx; ++ndx; ++bdx;
          }
        else
          {
            ++adx; ++bdx;
          }
      }
  }

  // trims and calls diff_to_ez
  static long diff_to_et(subarray<A> const & a, long start_a, long end_a,
                         subarray<B> const & b, long start_b, long end_b,
                         vector<long, QA(long)> & edits,
                         long edx,
                         long polarity,
                         long p_lim)
  {

    I(start_a >= 0);
    I(start_a <= a.size());
    I(start_b >= 0);
    I(start_b <= b.size());
    I(end_a >= 0);
    I(end_a <= a.size());
    I(end_b >= 0);
    I(end_b <= b.size());

    I(end_a - start_a >= p_lim);

    // last, not end
    long new_last_a = end_a - 1;
    long new_last_b = end_b - 1;
    while ((start_b <= new_last_b) &&
           (start_a <= new_last_a) &&
           (a[new_last_a] == b[new_last_b]))
      {
        --new_last_a;
        --new_last_b;
      }

    long new_start_a = start_a;
    long new_start_b = start_b;
    while((new_start_b < new_last_b) &&
          (new_start_a < new_last_a) &&
          (a[new_start_a] == b[new_start_b]))
      {
        ++new_start_a;
        ++new_start_b;
      }

    // we've trimmed; now call diff_to_ez.

    // difference between length of (new) a and length of (new) b
    long delta = (new_last_b - new_start_b) - (new_last_a - new_start_a);

    if (delta < 0)
      return diff_to_ez (b, new_start_b, new_last_b+1,
                         a, new_start_a, new_last_a+1,
                         edits, edx, -polarity, delta + p_lim);
    else
      return diff_to_ez (a, new_start_a, new_last_a+1,
                         b, new_start_b, new_last_b+1,
                         edits, edx, polarity, p_lim);
  }

  static long diff_to_ez(subarray<A> const & a, long start_a, long end_a,
                         subarray<B> const & b, long start_b, long end_b,
                         vector<long, QA(long)> & edits,
                         long edx1,
                         long polarity,
                         long p_lim)
  {

    I(start_a >= 0);
    I(start_a <= a.size());
    I(start_b >= 0);
    I(start_b <= b.size());
    I(end_a >= 0);
    I(end_a <= a.size());
    I(end_b >= 0);
    I(end_b <= b.size());

    long len_a = end_a - start_a;
    long len_b = end_b - start_b;

    I(len_a <= len_b);

    // easy case #1: B inserts only
    if (p_lim == 0)
      {
        // A == B, no edits
        if (len_a == len_b)
          return 0;

        long adx = start_a;
        long bdx = start_b;
        long edx0 = edx1;

        while (true)
          {
            if (bdx >= end_b)
              return len_b - len_a;

            if (adx >= end_a)
              {
                for (long idx = bdx, edx = edx0;
                     idx < end_b;
                     ++idx, ++edx)
                  edits[edx] = polarity * (idx+1);

                return len_b - len_a;
              }

            if (a[adx] == b[bdx])
              {
                ++adx; ++bdx;
              }
            else
              {
                edits[edx0] = polarity * (bdx+1);
                ++bdx; ++edx0;
              }
          }
      }

    // easy case #2: delete all A, insert all B
    else if (len_a <= p_lim)
      {
        I(len_a == p_lim);

        long edx0 = edx1;
        for (long idx = start_a; idx < end_a; ++idx, ++edx0)
          edits[edx0] = polarity * (-1 - idx);

        for (long jdx = start_b; jdx < end_b; ++jdx, ++edx0)
          edits[edx0] = polarity * (jdx + 1);

        return len_a + len_b;
      }

    // hard case: recurse on subproblems
    else
      {
        return divide_and_conquer (a, start_a, end_a,
                                   b, start_b, end_b,
                                   edits, edx1, polarity, p_lim);
      }
  }

  static void diff_to_edits(subarray<A> const & a, long len_a,
                            subarray<B> const & b, long len_b,
                            vector<long, QA(long)> & edits)
  {
    I(len_a <= len_b);
    cost_vec costs(len_a + len_b); // scratch array, ignored
    long edit_distance = compare(costs, a, len_a, b, len_b, min(len_a, len_b), false);

    edits.clear();
    edits.resize(edit_distance, 0);
    long cost = diff_to_et(a, 0, len_a,
                           b, 0, len_b,
                           edits, 0, 1, (edit_distance - (len_b - len_a)) / 2);
    I(cost == edit_distance);
  }

  static void edits_to_lcs (vector<long, QA(long)> const & edits,
                            subarray<A> const a, long len_a, long len_b,
                            LCS output)
  {
    long edx = 0, sdx = 0, adx = 0;
    typedef typename iterator_traits<A>::value_type vt;
    vector<vt, QA(vt)> lcs(((len_a + len_b) - edits.size()) / 2);
    while (adx < len_a)
      {
        long edit = (edx < static_cast<long>(edits.size())) ? edits[edx] : 0;

        if (edit > 0)
          { ++edx; }
        else if (edit == 0)
          { lcs[sdx++] = a[adx++]; }
        else if (adx >= (-1 - edit))
          { ++edx; ++adx; }
        else
          { lcs[sdx++] = a[adx++]; }
      }

    copy(lcs.begin(), lcs.end(), output);
  }
};


template <typename A,
          typename B,
          typename LCS>
void _edit_script(A begin_a, A end_a,
                  B begin_b, B end_b,
                  vector<long, QA(long)> & edits_out,
                  LCS ignored_out)
{
  typedef jaffer_edit_calculator<A,B,LCS> calc_t;
  long len_a = end_a - begin_a;
  long len_b = end_b - begin_b;
  typename calc_t::edit_vec edits, ordered;

  typename calc_t::template subarray<A> a(begin_a, 0, len_a);
  typename calc_t::template subarray<B> b(begin_b, 0, len_b);

  if (len_b < len_a)
    {
      calc_t::diff_to_edits (b, len_b, a, len_a, edits);
      calc_t::order_edits (edits, -1, ordered);
      for (size_t i = 0; i < ordered.size(); ++i)
        ordered[i] *= -1;
    }
  else
    {
      calc_t::diff_to_edits (a, len_a, b, len_b, edits);
      calc_t::order_edits (edits, 1, ordered);
    }

  edits_out.clear();
  edits_out.reserve(ordered.size());
  copy(ordered.begin(), ordered.end(), back_inserter(edits_out));
}


template <typename A,
          typename B,
          typename LCS>
void _longest_common_subsequence(A begin_a, A end_a,
                                 B begin_b, B end_b,
                                 LCS out)
{
  typedef jaffer_edit_calculator<A,B,LCS> calc_t;
  long len_a = end_a - begin_a;
  long len_b = end_b - begin_b;
  typename calc_t::edit_vec edits, ordered;

  typename calc_t::template subarray<A> a(begin_a, 0, len_a);
  typename calc_t::template subarray<B> b(begin_b, 0, len_b);

  if (len_b < len_a)
    {
      calc_t::diff_to_edits(b, len_b, a, len_a, edits);
      calc_t::order_edits(edits, -1, ordered);
      calc_t::edits_to_lcs(ordered, b, len_b, len_a, out);
    }
  else
    {
      calc_t::diff_to_edits(a, len_a, b, len_b, edits);
      calc_t::order_edits(edits, 1, ordered);
      calc_t::edits_to_lcs(ordered, a, len_a, len_b, out);
    }
}


void
longest_common_subsequence(vector<long, QA(long)>::const_iterator begin_a,
                           vector<long, QA(long)>::const_iterator end_a,
                           vector<long, QA(long)>::const_iterator begin_b,
                           vector<long, QA(long)>::const_iterator end_b,
                           back_insert_iterator< vector<long, QA(long)> > lcs)
{
  _longest_common_subsequence(begin_a, end_a,
                              begin_b, end_b,
                              lcs);
}

void
edit_script(vector<long, QA(long)>::const_iterator begin_a,
            vector<long, QA(long)>::const_iterator end_a,
            vector<long, QA(long)>::const_iterator begin_b,
            vector<long, QA(long)>::const_iterator end_b,
            vector<long, QA(long)> & edits_out)
{
  vector<long, QA(long)> lcs;
  _edit_script(begin_a, end_a,
               begin_b, end_b,
               edits_out,
               back_inserter(lcs));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
