// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <iterator>
#include <vector>
#include <string>
#include <iostream>

#include "diff_patch.hh"
#include "interner.hh"
#include "lcs.hh"
#include "manifest.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"

using namespace std;

bool guess_binary(string const & s)
{
  // these do not occur in text files
  if (s.find_first_of("\x00\x01\x02\x03\x04\x05\x06\x0e\x0f"
		      "\x10\x11\x12\x13\x14\x15\x16\x17\x18"
		      "\x19\x1a\x1c\x1d\x1e\x1f") != string::npos)
    return true;
  return false;
}

//
// a 3-way merge works like this:
//
//            /---->   right
//    ancestor
//            \---->   left
//
// first you compute the edit list "EDITS(ancestor,left)".
// 
// then you make an offset table "leftpos" which describes positions in
// "ancestor" as they map to "left"; that is, for 0 < apos <
// ancestor.size(), we have
//
//  left[leftpos[apos]] == ancestor[apos]
//
// you do this by walking through the edit list and either jumping the
// current index ahead an extra position, on an insert, or remaining still,
// on a delete. on an insert *or* a delete, you push the current index back
// onto the leftpos array.
//
// next you compute the edit list "EDITS(ancestor,right)".
//
// you then go through this edit list applying the edits to left, rather
// than ancestor, and using the table leftpos to map the position of each
// edit to an appropriate spot in left. this means you walk a "curr_left" 
// index through the edits, and for each edit e:
//
// - if e is a delete (and e.pos is a position in ancestor)
//   - increment curr_left without copying anything to "merged"
//
// - if e is an insert (and e.pos is a position in right)
//   - copy right[e.pos] to "merged"
//   - leave curr_left alone
//
// - when advancing to apos (and apos is a position in ancestor)
//   - copy left[curr_left] to merged while curr_left < leftpos[apos]
//
//
// the practical upshot is that you apply the delta from ancestor->right
// to the adjusted contexts in left, producing something vaguely like
// the concatenation of delta(ancestor,left) :: delta(ancestor,right).
//
// NB: this is, as far as I can tell, what diff3 does. I don't think I'm
// infringing on anyone's fancy patents here.
//

struct hunk_consumer
{
  virtual void flush_hunk(size_t pos) = 0;
  virtual void advance_to(size_t newpos) = 0;
  virtual void insert_at(size_t b_pos) = 0;
  virtual void delete_at(size_t a_pos) = 0;
  virtual ~hunk_consumer() {}
};

void walk_hunk_consumer(vector<long> const & lcs,
			vector<long> const & lines1,
			vector<long> const & lines2,			
			hunk_consumer & cons)
{

  size_t a = 0, b = 0;
  if (lcs.begin() == lcs.end())
    {
      // degenerate case: files have nothing in common
      cons.advance_to(0);
      while (a < lines1.size())
	cons.delete_at(a++);
      while (b < lines2.size())
	cons.insert_at(b++);
      cons.flush_hunk(a);
    }
  else
    {
      // normal case: files have something in common
      for (vector<long>::const_iterator i = lcs.begin();
	   i != lcs.end(); ++i, ++a, ++b)
	{      	  
	  if (idx(lines1, a) == *i && idx(lines2, b) == *i)
	    continue;

	  cons.advance_to(a);
	  while (idx(lines1,a) != *i)
	      cons.delete_at(a++);
	  while (idx(lines2,b) != *i)
	    cons.insert_at(b++);
	}
      if (b < lines2.size())
	{
	  cons.advance_to(a);
	  while(b < lines2.size())
	    cons.insert_at(b++);
	}
      if (a < lines1.size())
	{
	  cons.advance_to(a);
	  while(a < lines1.size())
	    cons.delete_at(a++);
	}
      cons.flush_hunk(a);
    }
}


// helper class which calculates the offset table

struct hunk_offset_calculator : public hunk_consumer
{
  vector<size_t> & leftpos;
  set<size_t> & deletes;
  set<size_t> & inserts;
  size_t apos;
  size_t lpos;
  size_t final;
  hunk_offset_calculator(vector<size_t> & lp, size_t fin, 
			 set<size_t> & dels, set<size_t> & inss);
  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~hunk_offset_calculator();
};

hunk_offset_calculator::hunk_offset_calculator(vector<size_t> & off, size_t fin,
					       set<size_t> & dels, set<size_t> & inss)
  : leftpos(off), deletes(dels), inserts(inss), apos(0), lpos(0), final(fin)
{}

hunk_offset_calculator::~hunk_offset_calculator()
{
  while(leftpos.size() < final)
    leftpos.push_back(leftpos.back());
}

void hunk_offset_calculator::flush_hunk(size_t ap)
{
  this->advance_to(ap);
}

void hunk_offset_calculator::advance_to(size_t ap)
{
  while(apos < ap)
    {
      //       L(F("advance to %d: [%d,%d] -> [%d,%d] (sz=%d)\n") % 
      //         ap % apos % lpos % apos+1 % lpos+1 % leftpos.size());
      apos++;
      leftpos.push_back(lpos++);
    }
}

void hunk_offset_calculator::insert_at(size_t lp)
{
  //   L(F("insert at %d: [%d,%d] -> [%d,%d] (sz=%d)\n") % 
  //     lp % apos % lpos % apos % lpos+1 % leftpos.size());
  inserts.insert(apos);
  I(lpos == lp);
  lpos++;
}

void hunk_offset_calculator::delete_at(size_t ap)
{
  //   L(F("delete at %d: [%d,%d] -> [%d,%d] (sz=%d)\n") % 
  //      ap % apos % lpos % apos+1 % lpos % leftpos.size());
  deletes.insert(apos);
  I(apos == ap);
  apos++;
  leftpos.push_back(lpos);
}

void calculate_hunk_offsets(vector<string> const & ancestor,
			    vector<string> const & left,
			    vector<size_t> & leftpos,
			    set<size_t> & deletes, 
			    set<size_t> & inserts)
{

  vector<long> anc_interned;  
  vector<long> left_interned;  
  vector<long> lcs;  

  interner<long> in;

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  lcs.reserve(std::min(left.size(),ancestor.size()));
  longest_common_subsequence(anc_interned.begin(), anc_interned.end(),
			     left_interned.begin(), left_interned.end(),
			     std::min(ancestor.size(), left.size()),
			     back_inserter(lcs));

  leftpos.clear();
  hunk_offset_calculator calc(leftpos, ancestor.size(), deletes, inserts);
  walk_hunk_consumer(lcs, anc_interned, left_interned, calc);
}


typedef enum { preserved = 0, deleted = 1, changed = 2 } edit_t;
static char *etab[3] = 
  {
    "preserved",
    "deleted",
    "changed"
  };

struct extent
{
  extent(size_t p, size_t l, edit_t t) 
    : pos(p), len(l), type(t)
  {}
  size_t pos;
  size_t len;
  edit_t type;
};

void calculate_extents(vector<long> const & a_b_edits,
		       vector<long> const & b,
		       vector<long> & prefix,
		       vector<extent> & extents,
		       vector<long> & suffix,
		       size_t const a_len,
		       interner<long> & intern)
{
  extents.reserve(a_len * 2);

  size_t a_pos = 0, b_pos = 0;

  for (vector<long>::const_iterator i = a_b_edits.begin(); 
       i != a_b_edits.end(); ++i)
    {
      // L(F("edit: %d") % *i);
      if (*i < 0)
	{
	  // negative elements code the negation of the one-based index into A
	  // of the element to be deleted
	  size_t a_deleted = (-1 - *i);

	  // fill positions out to the deletion point
	  while (a_pos < a_deleted)
	    {
	      a_pos++;
	      extents.push_back(extent(b_pos++, 1, preserved));
	    }

	  // L(F(" -- delete at A-pos %d (B-pos = %d)\n") % a_deleted % b_pos);

	  // skip the deleted line
	  a_pos++;
	  extents.push_back(extent(b_pos, 0, deleted));
	}
      else
	{
	  // positive elements code the one-based index into B of the element to
	  // be inserted
	  size_t b_inserted = (*i - 1);

	  // fill positions out to the insertion point
	  while (b_pos < b_inserted)
	    {
	      a_pos++;
	      extents.push_back(extent(b_pos++, 1, preserved));
	    }

	  // L(F(" -- insert at B-pos %d (A-pos = %d) : '%s'\n") 
	  //   % b_inserted % a_pos % intern.lookup(b.at(b_inserted)));
	  
	  // record that there was an insertion, but a_pos did not move.
	  if ((b_pos == 0 && extents.empty())
	      || (b_pos == prefix.size()))
	    {
	      prefix.push_back(b.at(b_pos));
	    }
	  else if (a_len == a_pos)
	    {
	      suffix.push_back(b.at(b_pos));
	    }
	  else
	    {
	      // make the insertion
	      extents.back().type = changed;
	      extents.back().len++;
	    }
	  b_pos++;
	}
    }

  while (extents.size() < a_len)
    extents.push_back(extent(b_pos++, 1, preserved));
}

void normalize_extents(vector<extent> & a_b_map,
		       vector<long> const & a,
		       vector<long> const & b)
{
  for (size_t i = 0; i < a_b_map.size(); ++i)
    {
      if (i > 0)
      {	
	size_t j = i;
	while (j > 0
	       && (a_b_map.at(j-1).type == preserved)
	       && (a_b_map.at(j).type == changed)
	       && (b.at(a_b_map.at(j-1).pos) == 
		   b.at(a_b_map.at(j).pos + a_b_map.at(j).len - 1)))
	  {
	    // the idea here is that if preserved extent j-1 has the same
	    // contents as the last line in changed extent j of length N,
	    // then it's exactly the same to consider j-1 as changed, of
	    // length N, (starting 1 line earlier) and j as preserved as
	    // length 1.

	    L(F("exchanging preserved extent [%d+%d] with changed extent [%d+%d]\n")
	      % a_b_map.at(j-1).pos
	      % a_b_map.at(j-1).len
	      % a_b_map.at(j).pos
	      % a_b_map.at(j).len);

	    swap(a_b_map.at(j-1).len, a_b_map.at(j).len);
	    swap(a_b_map.at(j-1).type, a_b_map.at(j).type);
	    --j;
	  }
      }
    }


  for (size_t i = 0; i < a_b_map.size(); ++i)
    {
      if (i > 0)
      {	
	size_t j = i;
	while (j > 0
	       && a_b_map.at(j).type == changed
	       && a_b_map.at(j-1).type == changed
	       && a_b_map.at(j).len > 1
	       && a_b_map.at(j-1).pos + a_b_map.at(j-1).len == a_b_map.at(j).pos)
	  {
	    // step 1: move a chunk from this insert extent to its
	    // predecessor
	    size_t piece = a_b_map.at(j).len - 1;
	    // 	    L(F("moving change piece of len %d from pos %d to pos %d\n")
	    // 	      % piece
	    // 	      % a_b_map.at(j).pos
	    // 	      % a_b_map.at(j-1).pos);
	    a_b_map.at(j).len = 1;
	    a_b_map.at(j).pos += piece;
	    a_b_map.at(j-1).len += piece;
	    
	    // step 2: if this extent (now of length 1) has become a "changed" 
	    // extent identical to its previous state, switch it to a "preserved"
	    // extent.
	    if (b.at(a_b_map.at(j).pos) == a.at(j))
	      {
		// 		L(F("changing normalized 'changed' extent at %d to 'preserved'\n")
		// 		  % a_b_map.at(j).pos);
		a_b_map.at(j).type = preserved;
	      }
	    --j;
	  }
      }
    }
}


void merge_extents(vector<extent> const & a_b_map,
		   vector<extent> const & a_c_map,
		   vector<long> const & b,
		   vector<long> const & c,
		   interner<long> const & in,
		   vector<long> & merged)
{
  I(a_b_map.size() == a_c_map.size());

  vector<extent>::const_iterator i = a_b_map.begin();
  vector<extent>::const_iterator j = a_c_map.begin();
  merged.reserve(a_b_map.size() * 2);

  //   for (; i != a_b_map.end(); ++i, ++j)
  //     {
  
  //       L(F("trying to merge: [%s %d %d] vs. [%s %d %d] \n")
  //        	% etab[i->type] % i->pos % i->len 
  //        	% etab[j->type] % j->pos % j->len);
  //     }
  
  //   i = a_b_map.begin();
  //   j = a_c_map.begin();

  for (; i != a_b_map.end(); ++i, ++j)
    {

      //       L(F("trying to merge: [%s %d %d] vs. [%s %d %d] \n")
      //        	% etab[i->type] % i->pos % i->len 
      //        	% etab[j->type] % j->pos % j->len);
      
      // mutual, identical preserves / inserts / changes
      if (((i->type == changed && j->type == changed)
	   || (i->type == preserved && j->type == preserved))
	  && i->len == j->len)
	{
	  for (size_t k = 0; k < i->len; ++k)
	    {
	      if (b.at(i->pos + k) != c.at(j->pos + k))
		{
		  L(F("conflicting edits: %s %d[%d] '%s' vs. %s %d[%d] '%s'\n")
		    % etab[i->type] % i->pos % k % in.lookup(b.at(i->pos + k)) 
		    % etab[j->type] % j->pos % k % in.lookup(c.at(j->pos + k)));
		  throw conflict();
		}
	      merged.push_back(b.at(i->pos + k));
	    }
	}

      // mutual or single-edge deletes
      else if ((i->type == deleted && j->len == deleted)
	       || (i->type == deleted && j->type == preserved)
	       || (i->type == preserved && j->type == deleted))
	{ 
	  // do nothing
	}

      // single-edge insert / changes 
      else if (i->type == changed && j->type == preserved)
	for (size_t k = 0; k < i->len; ++k)
	  merged.push_back(b.at(i->pos + k));
      
      else if (i->type == preserved && j->type == changed)
	for (size_t k = 0; k < j->len; ++k)
	  merged.push_back(c.at(j->pos + k));
      
      else
	{
	  L(F("conflicting edits: [%s %d %d] vs. [%s %d %d]\n")
	    % etab[i->type] % i->pos % i->len 
	    % etab[j->type] % j->pos % j->len);
	  throw conflict();	  
	}      
      
      //       if (merged.empty())
      // 	L(F(" --> EMPTY\n"));
      //       else
      //        	L(F(" --> [%d]: %s\n") % (merged.size() - 1) % in.lookup(merged.back()));
    }
}


void merge_via_edit_scripts(vector<string> const & ancestor,
			    vector<string> const & left,			    
			    vector<string> const & right,
			    vector<string> & merged)
{
  vector<long> anc_interned;  
  vector<long> left_interned, right_interned;  
  vector<long> left_edits, right_edits;  
  vector<long> left_prefix, right_prefix;  
  vector<long> left_suffix, right_suffix;  
  vector<extent> left_extents, right_extents;
  vector<long> merged_interned;
  interner<long> in;

  //   for (int i = 0; i < std::min(std::min(left.size(), right.size()), ancestor.size()); ++i)
  //     {
  //       std::cerr << "[" << i << "] " << left[i] << " " << ancestor[i] <<  " " << right[i] << endl;
  //     }

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(right.size());
  for (vector<string>::const_iterator i = right.begin();
       i != right.end(); ++i)
    right_interned.push_back(in.intern(*i));

  L(F("calculating left edit script on %d -> %d lines\n")
    % anc_interned.size() % left_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
	      left_interned.begin(), left_interned.end(),
	      std::min(ancestor.size(), left.size()),
	      left_edits);
  
  L(F("calculating right edit script on %d -> %d lines\n")
    % anc_interned.size() % right_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
	      right_interned.begin(), right_interned.end(),
	      std::min(ancestor.size(), right.size()),
	      right_edits);

  L(F("calculating left extents on %d edits\n") % left_edits.size());
  calculate_extents(left_edits, left_interned, 
		    left_prefix, left_extents, left_suffix, 
		    anc_interned.size(), in);

  L(F("calculating right extents on %d edits\n") % right_edits.size());
  calculate_extents(right_edits, right_interned, 
		    right_prefix, right_extents, right_suffix, 
		    anc_interned.size(), in);

  L(F("normalizing %d right extents\n") % right_extents.size());
  normalize_extents(right_extents, anc_interned, right_interned);

  L(F("normalizing %d left extents\n") % left_extents.size());
  normalize_extents(left_extents, anc_interned, left_interned);


  if ((!right_prefix.empty()) && (!left_prefix.empty()))
    {
      L(F("conflicting prefixes\n"));
      throw conflict();
    }

  if ((!right_suffix.empty()) && (!left_suffix.empty()))
    {
      L(F("conflicting suffixes\n"));
      throw conflict();
    }

  L(F("merging %d left, %d right extents\n") 
    % left_extents.size() % right_extents.size());

  copy(left_prefix.begin(), left_prefix.end(), back_inserter(merged_interned));
  copy(right_prefix.begin(), right_prefix.end(), back_inserter(merged_interned));

  merge_extents(left_extents, right_extents,
		left_interned, right_interned, 
		in, merged_interned);

  copy(left_suffix.begin(), left_suffix.end(), back_inserter(merged_interned));
  copy(right_suffix.begin(), right_suffix.end(), back_inserter(merged_interned));

  merged.reserve(merged_interned.size());
  for (vector<long>::const_iterator i = merged_interned.begin();
       i != merged_interned.end(); ++i)
    merged.push_back(in.lookup(*i));
}


bool merge3(vector<string> const & ancestor,
	    vector<string> const & left,
	    vector<string> const & right,
	    vector<string> & merged)
{
  try 
   { 
      merge_via_edit_scripts(ancestor, left, right, merged);
    }
  catch(conflict & c)
    {
      L(F("conflict detected. no merge.\n"));
      return false;
    }
  return true;
}


merge_provider::merge_provider(app_state & app) 
  : app(app) {}

void merge_provider::record_merge(file_id const & left_ident, 
					 file_id const & right_ident, 
					 file_id const & merged_ident,
					 file_data const & left_data, 
					 file_data const & merged_data)
{  
  L(F("recording successful merge of %s <-> %s into %s\n")
    % left_ident % right_ident % merged_ident);

  base64< gzip<delta> > merge_delta;
  transaction_guard guard(app.db);

  diff(left_data.inner(), merged_data.inner(), merge_delta);  
  packet_db_writer dbw(app);
  dbw.consume_file_delta (left_ident, merged_ident, file_delta(merge_delta));
  guard.commit();
}

void merge_provider::get_version(file_path const & path,
				 file_id const & ident,
				 file_data & dat)
{
  app.db.get_file_version(ident, dat);
}

bool merge_provider::try_to_merge_files(file_path const & path,
					file_id const & ancestor_id,					
					file_id const & left_id,
					file_id const & right_id,
					file_id & merged_id)
{
  
  L(F("trying to merge %s <-> %s (ancestor: %s)\n")
    % left_id % right_id % ancestor_id);

  if (left_id == right_id)
    {
      L(F("files are identical\n"));
      merged_id = left_id;
      return true;      
    }  

  file_data left_data, right_data, ancestor_data;
  data left_unpacked, ancestor_unpacked, right_unpacked, merged_unpacked;
  vector<string> left_lines, ancestor_lines, right_lines, merged_lines;

  this->get_version(path, left_id, left_data);
  this->get_version(path, ancestor_id, ancestor_data);
  this->get_version(path, right_id, right_data);
    
  unpack(left_data.inner(), left_unpacked);
  unpack(ancestor_data.inner(), ancestor_unpacked);
  unpack(right_data.inner(), right_unpacked);

  split_into_lines(left_unpacked(), left_lines);
  split_into_lines(ancestor_unpacked(), ancestor_lines);
  split_into_lines(right_unpacked(), right_lines);
    
  if (merge3(ancestor_lines, 
	     left_lines, 
	     right_lines, 
	     merged_lines))
    {
      hexenc<id> tmp_id;
      base64< gzip<data> > packed_merge;
      string tmp;
      
      L(F("internal 3-way merged ok\n"));
      join_lines(merged_lines, tmp);
      calculate_ident(data(tmp), tmp_id);
      file_id merged_fid(tmp_id);
      pack(data(tmp), packed_merge);

      merged_id = merged_fid;
      record_merge(left_id, right_id, merged_fid, 
		   left_data, packed_merge);

      return true;
    }
  else if (app.lua.hook_merge3(ancestor_unpacked, left_unpacked, 
			       right_unpacked, merged_unpacked))
    {
      hexenc<id> tmp_id;
      base64< gzip<data> > packed_merge;

      L(F("lua merge3 hook merged ok\n"));
      calculate_ident(merged_unpacked, tmp_id);
      file_id merged_fid(tmp_id);
      pack(merged_unpacked, packed_merge);

      merged_id = merged_fid;
      record_merge(left_id, right_id, merged_fid, 
		   left_data, packed_merge);
      return true;
    }

  return false;
}

bool merge_provider::try_to_merge_files(file_path const & path,
					file_id const & left_id,
					file_id const & right_id,
					file_id & merged_id)
{
  file_data left_data, right_data;
  data left_unpacked, right_unpacked, merged_unpacked;

  L(F("trying to merge %s <-> %s\n")
    % left_id % right_id);

  if (left_id == right_id)
    {
      L(F("files are identical\n"));
      merged_id = left_id;
      return true;      
    }  

  this->get_version(path, left_id, left_data);
  this->get_version(path, right_id, right_data);
    
  unpack(left_data.inner(), left_unpacked);
  unpack(right_data.inner(), right_unpacked);

  if (app.lua.hook_merge2(left_unpacked, right_unpacked, merged_unpacked))
    {
      hexenc<id> tmp_id;
      base64< gzip<data> > packed_merge;
      
      L(F("lua merge2 hook merged ok\n"));
      calculate_ident(merged_unpacked, tmp_id);
      file_id merged_fid(tmp_id);
      pack(merged_unpacked, packed_merge);
      
      merged_id = merged_fid;
      record_merge(left_id, right_id, merged_fid, 
		   left_data, packed_merge);
      return true;
    }
  
  return false;
}


// during the "update" command, the only real differences from merging
// are that we take our right versions from the filesystem, not the db,
// and we only record the merges in a transient, in-memory table.

update_merge_provider::update_merge_provider(app_state & app) 
  : merge_provider(app) {}

void update_merge_provider::record_merge(file_id const & left_id, 
					 file_id const & right_id,
					 file_id const & merged_id,
					 file_data const & left_data, 
					 file_data const & merged_data)
{  
  L(F("temporarily recording merge of %s <-> %s into %s\n")
    % left_id % right_id % merged_id);
  I(temporary_store.find(merged_id) == temporary_store.end());
  temporary_store.insert(make_pair(merged_id, merged_data));
}

void update_merge_provider::get_version(file_path const & path,
					file_id const & ident, 
					file_data & dat)
{
  if (app.db.file_version_exists(ident))
    app.db.get_file_version(ident, dat);
  else
    {
      base64< gzip<data> > tmp;
      file_id fid;
      N(file_exists (path),
	F("file %s does not exist in working copy") % path);
      read_localized_data(path, tmp, app.lua);
      calculate_ident(tmp, fid);
      N(fid == ident,
	F("file %s in working copy has id %s, wanted %s")
	% path % fid % ident);
      dat = tmp;
    }
}



// the remaining part of this file just handles printing out unidiffs for
// the case where someone wants to *read* a diff rather than apply it.

struct unidiff_hunk_writer : public hunk_consumer
{
  vector<string> const & a;
  vector<string> const & b;
  size_t ctx;
  ostream & ost;
  size_t a_begin, b_begin, a_len, b_len;
  vector<string> hunk;
  unidiff_hunk_writer(vector<string> const & a,
		      vector<string> const & b,
		      size_t ctx,
		      ostream & ost);
  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~unidiff_hunk_writer() {}
};

unidiff_hunk_writer::unidiff_hunk_writer(vector<string> const & a,
					 vector<string> const & b,
					 size_t ctx,
					 ostream & ost)
: a(a), b(b), ctx(ctx), ost(ost),
  a_begin(0), b_begin(0), 
  a_len(0), b_len(0)
{}

void unidiff_hunk_writer::insert_at(size_t b_pos)
{
  b_len++;
  hunk.push_back(string("+") + b[b_pos]);
}

void unidiff_hunk_writer::delete_at(size_t a_pos)
{
  a_len++;
  hunk.push_back(string("-") + a[a_pos]);  
}

void unidiff_hunk_writer::flush_hunk(size_t pos)
{
  if (hunk.size() > ctx)
    {
      // insert trailing context
      size_t a_pos = a_begin + a_len;
      for (size_t i = 0; (i < ctx) && (a_pos + i < a.size()); ++i)
	{	  
	  hunk.push_back(string(" ") + a[a_pos + i]);
	  a_len++;
	  b_len++;
	}
    }

  if (hunk.size() > 0)
    {
      
      // write hunk to stream
      ost << "@@ -" << a_begin+1;
      if (a_len > 1)
	ost << "," << a_len;
      
      ost << " +" << b_begin+1;
      if (b_len > 1)
    ost << "," << b_len;
      ost << " @@" << endl;
      
      copy(hunk.begin(), hunk.end(), ostream_iterator<string>(ost, "\n"));
    }
  
  // reset hunk
  hunk.clear();
  int skew = b_len - a_len;
  a_begin = pos;
  b_begin = pos + skew;
  a_len = 0;
  b_len = 0;
}

void unidiff_hunk_writer::advance_to(size_t newpos)
{
  if (a_begin + a_len + (2 * ctx) < newpos)
    {
      flush_hunk(newpos);

      // insert new leading context
      if (newpos - ctx < a.size())
	{
	  for (int i = ctx; i > 0; --i)
	    {
	      if (newpos - i < 0)
		continue;
	      hunk.push_back(string(" ") + a[newpos - i]);
	      a_begin--; a_len++;
	      b_begin--; b_len++;
	    }
	}
    }
  else
    {
      // pad intermediate context
      while(a_begin + a_len < newpos)
	{
	  hunk.push_back(string(" ") + a[a_begin + a_len]);
	  a_len++;
	  b_len++;	  
	}
    }
}


void unidiff(string const & filename1,
	     string const & filename2,
	     vector<string> const & lines1,
	     vector<string> const & lines2,
	     ostream & ost)
{
  ost << "--- " << filename1 << endl;
  ost << "+++ " << filename2 << endl;  

  vector<long> left_interned;  
  vector<long> right_interned;  
  vector<long> lcs;  

  interner<long> in;

  left_interned.reserve(lines1.size());
  for (vector<string>::const_iterator i = lines1.begin();
       i != lines1.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(lines2.size());
  for (vector<string>::const_iterator i = lines2.begin();
       i != lines2.end(); ++i)
    right_interned.push_back(in.intern(*i));

  lcs.reserve(std::min(lines1.size(),lines2.size()));
  longest_common_subsequence(left_interned.begin(), left_interned.end(),
			     right_interned.begin(), right_interned.end(),
			     std::min(lines1.size(), lines2.size()),
			     back_inserter(lcs));

  unidiff_hunk_writer hunks(lines1, lines2, 3, ost);
  walk_hunk_consumer(lcs, left_interned, right_interned, hunks);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"
#include <boost/lexical_cast.hpp>
#include "randomfile.hh"

using boost::lexical_cast;

static void dump_incorrect_merge(vector<string> const & expected,
				 vector<string> const & got,
				 string const & prefix)
{
  size_t mx = expected.size();
  if (mx < got.size())
    mx = got.size();
  for (size_t i = 0; i < mx; ++i)
    {
      cerr << "bad merge: " << i << " [" << prefix << "]\t";
      
      if (i < expected.size())
	cerr << "[" << expected[i] << "]\t";
      else
	cerr << "[--nil--]\t";
      
      if (i < got.size())
	cerr << "[" << got[i] << "]\t";
      else
	cerr << "[--nil--]\t";
      
      cerr << endl;
    }
}

// regression blockers go here
static void unidiff_append_test()
{
  string src(string("#include \"hello.h\"\n")
	     + "\n"
	     + "void say_hello()\n"
	     + "{\n"
	     + "        printf(\"hello, world\\n\");\n"
	     + "}\n"
	     + "\n"
	     + "int main()\n"
	     + "{\n"
	     + "        say_hello();\n"
	     + "}\n");
  
  string dst(string("#include \"hello.h\"\n")
	     + "\n"
	     + "void say_hello()\n"
	     + "{\n"
	     + "        printf(\"hello, world\\n\");\n"
	     + "}\n"
	     + "\n"
	     + "int main()\n"
	     + "{\n"
	     + "        say_hello();\n"
	     + "}\n"
	     + "\n"
	     + "void say_goodbye()\n"
	     + "{\n"
	     + "        printf(\"goodbye\\n\");\n"
	     + "}\n"
	     + "\n");
  
  string ud(string("--- hello.c\n")
	    + "+++ hello.c\n"
	    + "@@ -9,3 +9,9 @@\n"
	    + " {\n"
	    + "         say_hello();\n"
	    + " }\n"
	    + "+\n"
	    + "+void say_goodbye()\n"
	    + "+{\n"
	    + "+        printf(\"goodbye\\n\");\n"
	    + "+}\n"
	    + "+\n");

  vector<string> src_lines, dst_lines;
  split_into_lines(src, src_lines);
  split_into_lines(dst, dst_lines);
  stringstream sst;
  unidiff("hello.c", "hello.c", src_lines, dst_lines, sst);
  BOOST_CHECK(sst.str() == ud);  
}


// high tech randomizing test

static void randomizing_merge_test()
{
  for (int i = 0; i < 30; ++i)
    {
      vector<string> anc, d1, d2, m1, m2, gm;

      file_randomizer::build_random_fork(anc, d1, d2, gm,
					 i * 1023, (10 + 2 * i));      

      BOOST_CHECK(merge3(anc, d1, d2, m1));
      if (gm != m1)
	dump_incorrect_merge (gm, m1, "random_merge 1");
      BOOST_CHECK(gm == m1);

      BOOST_CHECK(merge3(anc, d2, d1, m2));
      if (gm != m2)
	dump_incorrect_merge (gm, m2, "random_merge 2");
      BOOST_CHECK(gm == m2);
    }
}


// old boring tests

static void merge_prepend_test()
{
  BOOST_CHECKPOINT("prepend test");
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  for (int i = 0; i < 10; ++i)
    {
      anc.push_back(lexical_cast<string>(i));
      d1.push_back(lexical_cast<string>(i));
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_prepend 1");
  BOOST_CHECK(gm == m1);


  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_prepend 2");
  BOOST_CHECK(gm == m2);
}


static void merge_append_test()
{
  BOOST_CHECKPOINT("append test");
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 0; i < 10; ++i)
      anc.push_back(lexical_cast<string>(i));

  d1 = anc;
  d2 = anc;
  gm = anc;

  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_append 1");
  BOOST_CHECK(gm == m1);

  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_append 2");
  BOOST_CHECK(gm == m2);


}

static void merge_additions_test()
{
  BOOST_CHECKPOINT("additions test");
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc1("I like oatmeal\nI don't like spam\nI like orange juice\nI like toast");
  string confl("I like oatmeal\nI don't like tuna\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like orange juice\nI don't like tuna\nI like toast");
  string good_merge("I like oatmeal\nI don't like spam\nI like orange juice\nI don't like tuna\nI like toast");
  vector<string> anc, d1, cf, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc1, d1);
  split_into_lines(confl, cf);
  split_into_lines(desc2, d2);
  split_into_lines(good_merge, gm);
  
  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_addition 1");
  BOOST_CHECK(gm == m1);

  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_addition 2");
  BOOST_CHECK(gm == m2);

  BOOST_CHECK(!merge3(anc, d1, cf, m1));
}

static void merge_deletions_test()
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like toast");

  vector<string> anc, d1, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc2, d2);
  d1 = anc;
  gm = d2;

  BOOST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_deletion 1");
  BOOST_CHECK(gm == m1);

  BOOST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_deletion 2");
  BOOST_CHECK(gm == m2);
}


void add_diff_patch_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&unidiff_append_test));
  suite->add(BOOST_TEST_CASE(&merge_prepend_test));
  suite->add(BOOST_TEST_CASE(&merge_append_test));
  suite->add(BOOST_TEST_CASE(&merge_additions_test));
  suite->add(BOOST_TEST_CASE(&merge_deletions_test));
  suite->add(BOOST_TEST_CASE(&randomizing_merge_test));
}


#endif // BUILD_UNIT_TESTS
