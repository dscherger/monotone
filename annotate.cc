// copyright (C) 2005 emile snyder <emile@alumni.reed.edu>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <sstream>
#include <deque>
#include <set>

#include <boost/shared_ptr.hpp>

#include "platform.hh"
#include "vocab.hh"
#include "sanity.hh"
#include "revision.hh"
#include "change_set.hh"
#include "app_state.hh"
#include "manifest.hh"
#include "transforms.hh"
#include "lcs.hh"
#include "annotate.hh"
#include "format.hh"


/* 
   file of interest, 'foo', is made up of 6 lines, while foo's 
   parent (foo') is 5 lines:

   foo     foo'
   A       A
   B       z
   C       B
   D       C
   E       y
   F

   The longest common subsequence between foo and foo' is
   [A,B,C] and we know that foo' lines map to foo lines like
   so:
   
   foo'
   A    0 -> 0
   z    1 -> (none)
   B    2 -> 1
   C    3 -> 2
   y    4 -> (none)
   
   How do we know?  Because we walk the file along with the LCS,
   having initialized the copy_count at 0, and do:

   i = j = copy_count = 0;
   while (i < foo'.size()) {
     map[i] = -1;
     if (foo'[i] == LCS[j]) { 
       map[i] = lcs_src_lines[j];
       i++; j++; copy_count++;
       continue;
     }
     i++;
   }

   If we're trying to annotate foo, we want to assign each line
   of foo that we can't find in the LCS to the foo revision (since 
   it can't have come from further back in time.)  So at each edge
   we do the following:

   1. build the LCS
   2. walk over the child (foo) and the LCS simultaneously, using the
      lineage map of the child and the LCS to assign
      blame as we go for lines that aren't in the LCS.  Also generate
      a vector, lcs_src_lines, with the same length as LCS whose 
      elements are the line in foo which that LCS entry represents. 
      So for foo, it would be [0, 1, 2] because [A,B,C] is the first
      3 elements.
   3. walk over the parent (foo'), using our exising lineage map and the 
      LCS, to build the parent's lineage map (which will be used
      at the next phase.)

*/   

class annotate_lineage_mapping;
class annotate_formatter;

class annotate_context {
public:
  annotate_context(file_id fid, app_state &app);

  boost::shared_ptr<annotate_lineage_mapping> initial_lineage() const;

  /// credit any remaining unassigned lines to rev
  void complete(revision_id rev);

  /// credit any uncopied lines (as recorded in copied_lines) to
  /// rev, and reset copied_lines.
  void evaluate(revision_id rev);

  void set_copied(int index);
  void set_touched(int index);

  /// return an immutable reference to our vector of string data for external use
  const std::vector<std::string>& get_file_lines() const;

  /// return true if we have no more unassigned lines
  bool is_complete() const;

  std::ostream& write_annotations(boost::shared_ptr<annotate_formatter> frmt, std::ostream &os) const;

  std::set<revision_id>::const_iterator begin_revisions() const { return annotate_revisions.begin(); }
  std::set<revision_id>::const_iterator end_revisions() const { return annotate_revisions.end(); }

private:
  std::vector<std::string> file_lines;
  std::vector<revision_id> annotations;

  /// keep a count so we can tell quickly whether we can terminate
  size_t annotated_lines_completed;

  // elements of the set are indexes into the array of lines in the UDOI
  // lineages add entries here when they notice that they copied a line from the UDOI
  std::set<size_t> copied_lines;

  // similarly, lineages add entries here for all the lines from the UDOI they know about that they didn't copy
  std::set<size_t> touched_lines;

  revision_id root_revision;

  std::set<revision_id> annotate_revisions; // set of all revisions that appear in the annotations
};



/*
  An annotate_lineage_mapping tells you, for each line of a file, where in the 
  ultimate descendent of interest (UDOI) the line came from (a line not
  present in the UDOI is represented as -1).
*/
class annotate_lineage_mapping {
public:
  annotate_lineage_mapping(const file_data &data);
  annotate_lineage_mapping(const std::vector<std::string> &lines);

  /// need a better name.  does the work of setting copied bits in the context object.
  boost::shared_ptr<annotate_lineage_mapping> 
  build_parent_lineage(boost::shared_ptr<annotate_context> acp, revision_id parent_rev, const file_data &parent_data) const;

  void credit_mapped_lines (boost::shared_ptr<annotate_context> acp) const;

  void set_copied_all_mapped (boost::shared_ptr<annotate_context> acp) const;

private:
  void init_with_lines(const std::vector<std::string> &lines);

  static interner<long> in; // FIX, testing hack 

  std::vector<long, QA(long)> file_interned;

  // same length as file_lines. if file_lines[i] came from line 4 in the UDOI, mapping[i] = 4
  std::vector<int> mapping;
};


interner<long> annotate_lineage_mapping::in;


/*
  annotate_node_work encapsulates the input data needed to process 
  the annotations for a given childrev, considering all the
  childrev -> parentrevN edges.
*/
struct annotate_node_work {
  annotate_node_work (boost::shared_ptr<annotate_context> annotations_,
                      boost::shared_ptr<annotate_lineage_mapping> lineage_,
                      revision_id node_revision_, file_id node_fid_, file_path node_fpath_)
    : annotations(annotations_), 
      lineage(lineage_),
      node_revision(node_revision_), 
      node_fid(node_fid_), 
      node_fpath(node_fpath_)
  {}

  boost::shared_ptr<annotate_context> annotations;
  boost::shared_ptr<annotate_lineage_mapping> lineage;
  revision_id node_revision;
  file_id     node_fid;
  file_path   node_fpath;
};


class annotate_formatter {
public:
  annotate_formatter(app_state &app,
                     std::set<revision_id>::const_iterator startrev,
                     std::set<revision_id>::const_iterator endrev);

  std::string format (const revision_id &rev, const std::string &line) const
  {
    std::map<revision_id, std::string>::const_iterator i = desc.find(rev);
    //I(i != desc.end()); // FIX this is the same bug as the not-all-lines-annotated
    if (i == desc.end())
      return "FIXME!!! : " + line;

    return i->second + line;
  }

protected:
  std::map<revision_id, std::string> desc;
};


annotate_formatter::annotate_formatter(app_state &app,
                                       std::set<revision_id>::const_iterator startrev,
                                       std::set<revision_id>::const_iterator endrev)
{
  std::ostringstream res_stream;
  utf8 fs = (app.default_format) ? utf8("%i: ") : app.format_string;
  PrintFormatter pf(res_stream, app, fs);

  size_t max_annotate_len = 0;
  std::set<revision_id>::const_iterator i;
  i = startrev;
  while (i != endrev) {
    pf.apply(*i);

    size_t rs = res_stream.str().size();
    max_annotate_len =  (rs > max_annotate_len) ? rs : max_annotate_len;

    desc.insert(std::make_pair(*i, res_stream.str()));
    res_stream.str("");
    i++;
  }

  // justify the annotate strings
  std::map<revision_id, std::string>::iterator j = desc.begin();
  while (j != desc.end()) {
    size_t extra_spaces = max_annotate_len - j->second.size();
    j->second = std::string(extra_spaces, ' ') + j->second;
    j++;
  }
}


annotate_context::annotate_context(file_id fid, app_state &app)
  : annotated_lines_completed(0)
{
  // initialize file_lines
  file_data fpacked;
  app.db.get_file_version(fid, fpacked);
  std::string encoding = default_encoding; // FIXME
  split_into_lines(fpacked.inner()(), encoding, file_lines);
  L(F("annotate_context::annotate_context initialized with %d file lines\n") % file_lines.size());

  // initialize annotations
  revision_id nullid;
  annotations.clear();
  annotations.reserve(file_lines.size());
  annotations.insert(annotations.begin(), file_lines.size(), nullid);
  L(F("annotate_context::annotate_context initialized with %d entries in annotations\n") % annotations.size());

  // initialize copied_lines and touched_lines
  copied_lines.clear();
  touched_lines.clear();
}


boost::shared_ptr<annotate_lineage_mapping> 
annotate_context::initial_lineage() const
{
  boost::shared_ptr<annotate_lineage_mapping> res(new annotate_lineage_mapping(file_lines));
  return res;
}


void 
annotate_context::complete(revision_id rev)
{
  revision_id nullid;
  std::vector<revision_id>::iterator i;
  for (i=annotations.begin(); i != annotations.end(); i++) {
    if (*i == nullid) {
      *i = rev;
      annotated_lines_completed++;
    }
  }
}

void 
annotate_context::evaluate(revision_id rev)
{
  revision_id nullid;
  I(copied_lines.size() <= annotations.size());
  I(touched_lines.size() <= annotations.size());

  // Find the lines that we touched but that no other parent copied.
  std::set<size_t> credit_lines;
  std::set_difference(touched_lines.begin(), touched_lines.end(),
                      copied_lines.begin(), copied_lines.end(),
                      inserter(credit_lines, credit_lines.begin()));

  size_t old_lines_completed = annotated_lines_completed;
  std::set<size_t>::const_iterator i;
  for (i = credit_lines.begin(); i != credit_lines.end(); i++) {
    I(*i >= 0 && *i < annotations.size());
    if (annotations[*i] == nullid) {
      //L(F("evaluate setting annotations[%d] -> %s, since touched_lines contained %d, copied_lines didn't and annotations[%d] was nullid\n") 
      //  % *i % rev % *i % *i);
      annotations[*i] = rev;
      annotated_lines_completed++;
    } else {
      //L(F("evaluate LEAVING annotations[%d] -> %s\n") % *i % annotations[*i]);
    }
  }

  if (old_lines_completed != annotated_lines_completed)
      annotate_revisions.insert(rev);

  copied_lines.clear();
  touched_lines.clear();
}

void 
annotate_context::set_copied(int index)
{
  //L(F("annotate_context::set_copied %d\n") % index);
  if (index == -1)
    return;

  I(index >= 0 && index < (int)file_lines.size());
  copied_lines.insert(index);
}

void 
annotate_context::set_touched(int index)
{
  //L(F("annotate_context::set_touched %d\n") % index);
  if (index == -1)
    return;

  I(index >= 0 && index <= (int)file_lines.size());
  touched_lines.insert(index);
}


const std::vector<std::string>& 
annotate_context::get_file_lines() const
{
  return file_lines;
}


bool
annotate_context::is_complete() const
{
  if (annotated_lines_completed == annotations.size())
    return true;

  I(annotated_lines_completed < annotations.size());
  return false;
}


std::ostream& 
annotate_context::write_annotations(boost::shared_ptr<annotate_formatter> frmt, std::ostream &os) const
{
  for (size_t i=0; i<file_lines.size(); i++) {
    os << frmt->format(annotations[i], file_lines[i]) << std::endl;
  }

  return os;
}


annotate_lineage_mapping::annotate_lineage_mapping(const file_data &data)
{
  // split into lines
  std::vector<std::string> lines;
  std::string encoding = default_encoding; // FIXME
  split_into_lines (data.inner()().data(), encoding, lines);

  init_with_lines(lines);
}

annotate_lineage_mapping::annotate_lineage_mapping(const std::vector<std::string> &lines)
{
  init_with_lines(lines);
}

void
annotate_lineage_mapping::init_with_lines(const std::vector<std::string> &lines)
{
  file_interned.clear();
  file_interned.reserve(lines.size());
  mapping.clear();
  mapping.reserve(lines.size());

  int count;
  std::vector<std::string>::const_iterator i;
  for (count=0, i = lines.begin(); i != lines.end(); i++, count++) {
    file_interned.push_back(in.intern(*i));
    mapping.push_back(count);
  }
  L(F("annotate_lineage_mapping::init_with_lines  ending with %d entries in mapping\n") % mapping.size());
}


boost::shared_ptr<annotate_lineage_mapping>
annotate_lineage_mapping::build_parent_lineage (boost::shared_ptr<annotate_context> acp, 
                                                revision_id parent_rev, 
                                                const file_data &parent_data) const
{
  boost::shared_ptr<annotate_lineage_mapping> parent_lineage(new annotate_lineage_mapping(parent_data));

  std::vector<long, QA(long)> lcs;
  std::back_insert_iterator< std::vector<long, QA(long)> > bii(lcs);
  longest_common_subsequence(file_interned.begin(), 
                             file_interned.end(),
                             parent_lineage->file_interned.begin(), 
                             parent_lineage->file_interned.end(),
                             std::min(file_interned.size(), parent_lineage->file_interned.size()),
                             std::back_inserter(lcs));

  //L(F("build_parent_lineage: file_lines.size() == %d, parent.file_lines.size() == %d, lcs.size() == %d\n")
  //  % file_interned.size() % parent_lineage->file_interned.size() % lcs.size());

  // do the copied lines thing for our annotate_context
  std::vector<long> lcs_src_lines;
  lcs_src_lines.reserve(lcs.size());
  size_t i, j;
  i = j = 0;
  while (i < file_interned.size() && j < lcs.size()) {
    //L(F("file_interned[%d]: %ld    lcs[%d]: %ld\n") % i % file_interned[i] % j % lcs[j]);

    if (file_interned[i] == lcs[j]) {
      acp->set_copied(mapping[i]);
      lcs_src_lines[j] = mapping[i];
      j++;
    } else {
      acp->set_touched(mapping[i]);
    }

    i++;
  }
  //L(F("loop ended with i: %d, j: %d, lcs.size(): %d\n") % i % j % lcs.size());
  I(j == lcs.size());

  // set touched for the rest of the lines in the file
  while (i < file_interned.size()) {
    acp->set_touched(mapping[i]);
    i++;
  }

  // determine the mapping for parent lineage
  //L(F("build_parent_lineage: building mapping now\n"));
  i = j = 0;
  while (i < parent_lineage->file_interned.size() && j < lcs.size()) {
    if (parent_lineage->file_interned[i] == lcs[j]) {
      parent_lineage->mapping[i] = lcs_src_lines[j];
      j++;
    } else {
      parent_lineage->mapping[i] = -1;
    }
    //L(F("mapping[%d] -> %d\n") % i % parent_lineage->mapping[i]);
    
    i++;
  }
  I(j == lcs.size());

  return parent_lineage;
}


void
annotate_lineage_mapping::credit_mapped_lines (boost::shared_ptr<annotate_context> acp) const
{
  std::vector<int>::const_iterator i;
  for (i=mapping.begin(); i != mapping.end(); i++) {
    acp->set_touched(*i);
  }
}


void 
annotate_lineage_mapping::set_copied_all_mapped (boost::shared_ptr<annotate_context> acp) const
{
  std::vector<int>::const_iterator i;
  for (i=mapping.begin(); i != mapping.end(); i++) {
    acp->set_copied(*i);
  }
}


static void
do_annotate_node (const annotate_node_work &work_unit, 
                  app_state &app,
                  std::deque<annotate_node_work> &nodes_to_process,
                  std::set<revision_id> &nodes_seen)
{
  //L(F("do_annotate_node for node %s\n") % work_unit.node_revision);
  nodes_seen.insert(work_unit.node_revision);
  revision_id null_revision; // initialized to 0 by default?

  int added_in_parent_count = 0;

  revision_set rev;
  app.db.get_revision(work_unit.node_revision, rev);

  if (rev.edges.size() == 0) {
    // work_unit.node_revision is a root node
    L(F("do_annotate_node credit_mapped_lines to revision %s\n") % work_unit.node_revision);
    work_unit.lineage->credit_mapped_lines(work_unit.annotations);
    work_unit.annotations->evaluate(work_unit.node_revision);
    return;
  }

  // edges are from parent -> child where child is our work_unit node
  for (edge_map::const_iterator i = rev.edges.begin(); i != rev.edges.end(); i++) {
    L(F("do_annotate_node processing edge from parent %s to child %s\n") 
      % edge_old_revision(i) % work_unit.node_revision);

    change_set cs = edge_changes(i);
    if (cs.rearrangement.added_files.find(work_unit.node_fpath) != cs.rearrangement.added_files.end()) {
      L(F("file %s added in %s, continuing\n") 
        % work_unit.node_fpath % work_unit.node_revision);
      added_in_parent_count++;
      continue;
    }

    file_path parent_fpath = apply_change_set_inverse(cs, work_unit.node_fpath);
    L(F("file %s in parent revision %s is %s\n") % work_unit.node_fpath % edge_old_revision(i) % parent_fpath);
    I(!(parent_fpath == std::string("")));
    //file_id parent_fid = find_file_id_in_revision(app, parent_fpath, edge_old_manifest(i));
    change_set::delta_map::const_iterator fdelta_iter = cs.deltas.find(parent_fpath);
    file_id parent_fid = work_unit.node_fid;

    boost::shared_ptr<annotate_lineage_mapping> parent_lineage;

    if (fdelta_iter != cs.deltas.end()) { // then the file changed
      I(delta_entry_dst(fdelta_iter) == work_unit.node_fid);
      parent_fid = delta_entry_src(fdelta_iter);
      file_data data;
      app.db.get_file_version(parent_fid, data);

      parent_lineage = work_unit.lineage->build_parent_lineage(work_unit.annotations,
                                                               work_unit.node_revision,
                                                               data);
    } else {
      parent_lineage = work_unit.lineage;
      parent_lineage->set_copied_all_mapped(work_unit.annotations);
    }

    // if this parent has not yet been queued for processing, create the work unit for it.
    if (nodes_seen.find(edge_old_revision(i)) == nodes_seen.end()) {
      nodes_seen.insert(edge_old_revision(i));
      annotate_node_work newunit(work_unit.annotations, parent_lineage, edge_old_revision(i), parent_fid, parent_fpath);
      nodes_to_process.push_back(newunit);
    }
  }

  I(added_in_parent_count >= 0);
  I((size_t)added_in_parent_count <= rev.edges.size());
  if ((size_t)added_in_parent_count == rev.edges.size()) {
    //L(F("added_in_parent_count == rev.edges.size(), credit_mapped_lines to %s\n") 
    //  % work_unit.node_revision);
    work_unit.lineage->credit_mapped_lines(work_unit.annotations);
  }

  work_unit.annotations->evaluate(work_unit.node_revision);
}


void 
do_annotate (app_state &app, file_path fpath, file_id fid, revision_id rid)
{
  L(F("annotating file %s with id %s in revision %s\n") % fpath % fid % rid);

  boost::shared_ptr<annotate_context> acp(new annotate_context(fid, app));
  boost::shared_ptr<annotate_lineage_mapping> lineage = acp->initial_lineage();

  // build node work unit
  std::deque<annotate_node_work> nodes_to_process;
  std::set<revision_id> nodes_seen;
  annotate_node_work workunit(acp, lineage, rid, fid, fpath);
  nodes_to_process.push_back(workunit);

  while (nodes_to_process.size() && !acp->is_complete()) {
    annotate_node_work work = nodes_to_process.front();
    nodes_to_process.pop_front();
    do_annotate_node(work, app, nodes_to_process, nodes_seen);
  }
  //I(acp->is_complete());
  if (!acp->is_complete()) {
      W(F("annotate was unable to assign blame to some lines.  This is a bug.\n"));
  }

  boost::shared_ptr<annotate_formatter> frmt(new annotate_formatter(app, acp->begin_revisions(), acp->end_revisions()));
  acp->write_annotations(frmt, std::cout);
}
