// copyright (C) 2005-2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
#include "mtncvs_state.hh"
#include "keys.hh"
#include "transforms.hh"
#include <vector>
#include <boost/lexical_cast.hpp>
#include "botan/md5.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/greg_date.hpp>
#include <fstream>
#include <sys/stat.h>
#include "stringtok.hh"
#include "piece_table.hh"
#include "safe_map.hh"
#include <boost/format.hpp>

#ifdef WIN32
#define sleep(x) _sleep(x)
#endif

using namespace std;
using namespace cvs_sync;

std::string cvs_repository::gather_merge_information(revision_id const& id)
{ L(FL("gather_merge_information(%s)") % id);
  std::vector<revision_id> parents=app.get_revision_parents(id);
  std::string result;
  for (std::vector<revision_id>::const_iterator i=parents.begin();i!=parents.end();++i)
  { if (*i==revision_id()) continue;
#if 0    
    std::vector<mtn_automate::certificate>::const_iterator j=certs.begin();
    std::string to_match=create_cvs_cert_header();
    for (;j!=certs.end();++j)
    { if (j->inner().name()!=cvs_cert_name) continue;
      cert_value value;
      decode_base64(j->inner().value, value);
      if (value().size()<to_match.size()) continue;
      if (value().substr(0,to_match.size())!=to_match) continue;
      break;
    }
#endif
    // this revision is already in _this_ repository
// TODO: has sync info would be sufficient
    try
    { if (!app.get_sync_info(*i,app.opts.domain).empty()) continue;
    } catch (std::exception &e)
    { W(F("get sync info threw %s") % e.what());
    }
    
    std::vector<mtn_automate::certificate> certs=app.get_revision_certs(*i);
    std::string author,changelog;
    time_t date=0;
    for (std::vector<mtn_automate::certificate>::const_iterator j=certs.begin();j!=certs.end();++j)
    { if (!j->trusted || j->signature!=mtn_automate::certificate::ok)
        continue;
      if (j->name=="date")
      { date=cvs_repository::posix2time_t(j->value);
      }
      else if (j->name=="author")
      { author=j->value;
      }
      else if (j->name=="changelog")
      { changelog=j->value;
      }
    }
    result+="-------------------\n"
        +changelog+"\nmonotone "+author+" "
        +cvs_client::time_t2rfc822(date)+" "+i->inner()()+"\n";
    result+=gather_merge_information(*i);
  }
  return result;
}

void cvs_sync::push(const std::string &_repository, const std::string &_module,
            std::string const& _branch, mtncvs_state &app)
{ cvs_repository *repo=cvs_sync::prepare_sync(_repository,_module,_branch,app);
  
  if (repo->empty())
    W(F("no revision certs for this module, exporting all\n"));
  L(FL("push"));
//  std::cerr << repo->debug() << '\n';
  repo->commit();
  delete repo;
}

namespace {
struct is_branch
{ std::string comparison;
  is_branch(std::string const& br) : comparison(br) {}
  bool operator()(mtn_automate::certificate const& cert)
  { return cert.trusted 
        && cert.signature==mtn_automate::certificate::ok
        && cert.name=="branch"
        && cert.value==comparison;
  }
};
}

void cvs_repository::commit()
{ retrieve_modules();
  if (edges.empty())
  { // search for a matching start of history
    // take first head 
    std::vector<revision_id> heads=app.heads(app.opts.branchname());
    N(!heads.empty(), F("branch %s has no heads") % app.opts.branchname());
    
    revision_id actual=*heads.begin();
    is_branch branch_comparer(app.opts.branchname());
    do
    { std::vector<revision_id> parents=app.get_revision_parents(actual);
      for (std::vector<revision_id>::const_iterator i=parents.begin();
              i!=parents.end();++i)
      { if (*i==revision_id()) break; // root revision
        std::vector<mtn_automate::certificate> certs
          = app.get_revision_certs(*i);
        for (std::vector<mtn_automate::certificate>::const_iterator j=certs.begin();
              j!=certs.end();++j)
          if (branch_comparer(*j))
          { actual=*i;
            goto continue_outer;
          }
      }
     continue_outer: ;
    } while (true);
    // start with actual
    I(!null_id(actual));
    bool fail=false;
    // set up an empty CVS revision?
    commit_mtn2cvs(edges.end(), actual, fail);
//    automate::manifest_map root_manifest=app.get_manifest_of(actual);
    
    I(!fail);
  }
  std::set<cvs_edge>::iterator now_iter=last_known_revision();
  while (now_iter!=edges.end())
  { const cvs_edge &now=*now_iter;
    I(!now.revision.inner()().empty());
    
    L(FL("looking for children of revision %s\n") % now.revision);
    std::vector<revision_id> children=app.get_revision_children(now.revision);
    
    if (!app.opts.branchname().empty())
    { // app.opts.branch_name
      // ignore revisions not belonging to the specified branch
      for (std::vector<revision_id>::iterator i=children.begin();
                    i!=children.end();)
      { std::vector<mtn_automate::certificate> certs
          = app.get_revision_certs(*i);
        if (std::remove_if(certs.begin(),certs.end(),is_branch(app.opts.branchname()))==certs.begin())
          i=children.erase(i);
        else ++i;
      }
    }
    if (children.empty()) return;
    revision_id next;
    if (children.size()>1 && !app.opts.first)
    { for (std::vector<revision_id>::const_iterator i=app.opts.revisions.begin();
          i!=app.opts.revisions.end();++i)
      { for (std::vector<revision_id>::const_iterator j=children.begin();
          j!=children.end();++j)
        { if (*i==*j)
          { next=*j;
            break;
          }
        }
      }
      if (next.inner()().empty())
      { W(F("several children found for %s:\n") % now.revision);
        for (std::vector<revision_id>::const_iterator i=children.begin();
                    i!=children.end();++i)
        { W(F("%s\n") % *i);
        }
        W(F("please specify direction using --revision\n"));
        return;
      }
    }
    else next=*children.begin();
    bool fail=bool();
    now_iter=commit_mtn2cvs(now_iter,next,fail);
    
    if (!fail)
      P(F("checked %s into cvs repository") % now.revision);
    // we'd better seperate the commits so that ordering them is possible
    if (now_iter!=edges.end()) sleep(2);
  }
//  store_modules();
}

std::set<cvs_edge>::iterator cvs_repository::commit_mtn2cvs(
      std::set<cvs_edge>::iterator parent, const revision_id &rid, bool &fail)
{ // check that it's the last one
  L(FL("commit %s -> %s\n") % parent->revision % rid);
  if (parent!=edges.end()) // accept full push
  { std::set<cvs_edge>::iterator test=parent;
    ++test;
    I(test==edges.end());
  }
  // a bit like process_certs
  cvs_edge e(rid,app);

  mtn_automate::revision_t rs=app.get_revision(rid);
  std::vector<commit_arg> commits;
  
  for (mtn_automate::edge_map::const_iterator j = rs.edges.begin(); 
       j != rs.edges.end();
       ++j)
  { if ((parent==edges.end() && !null_id(j->first)))
    { L(FL("%s != \"\"\n") % j->first);
      continue;
    }
    else if ((parent!=edges.end() && !(j->first == parent->revision)))
    { L(FL("%s != %s\n") % j->first % (parent->revision));
      continue;
    }
    boost::shared_ptr<mtn_automate::cset> cs=j->second;
    cvs_manifest parent_manifest;
    if (parent!=edges.end()) parent_manifest=get_files(*parent);
    std::map<file_path, file_id> renamed_ids;

    for (mtn_automate::path_set::const_iterator i=cs->nodes_deleted.begin();
            i!=cs->nodes_deleted.end(); ++i)
    { commit_arg a;
      a.file=file_path(*i).as_internal();
      cvs_manifest::const_iterator old=parent_manifest.find(a.file);
//      if (a.file==".mtn-sync-"+app.opts.domain) continue;
      I(old!=parent_manifest.end());
      a.removed=true;
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      commits.push_back(a);
      L(FL("delete %s -%s %s\n") % a.file % a.old_revision % a.keyword_substitution);
    }

    for (std::map<file_path,file_path>::const_iterator i
                            =cs->nodes_renamed.begin();
            i!=cs->nodes_renamed.end(); ++i)
    { commit_arg a; // remove
      a.file=file_path(i->first).as_internal();
      cvs_manifest::const_iterator old=parent_manifest.find(a.file);
      I(old!=parent_manifest.end());
      a.removed=true;
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      commits.push_back(a);
      L(FL("rename from %s -%s %s\n") % a.file % a.old_revision % a.keyword_substitution);
      
      a=commit_arg(); // add
      a.file=file_path(i->second).as_internal();
      I(!old->second->sha1sum.inner()().empty());
      std::map<file_path, std::pair<file_id, file_id> >::const_iterator change_ent =
          cs->deltas_applied.find(i->second);
      if (change_ent != cs->deltas_applied.end())
        // the file content is going to change - handle that little detail now...
        renamed_ids[i->second] = change_ent->second.second;
      else
        renamed_ids[i->second] = old->second->sha1sum;
      a.new_content=app.get_file(renamed_ids[i->second]).inner()();
      commits.push_back(a);
      L(FL("rename to %s %d\n") % a.file % a.new_content.size());
    }
    
    for (mtn_automate::path_set::const_iterator i=cs->dirs_added.begin();
            i!=cs->dirs_added.end(); ++i)
    { std::string name=file_path(*i).as_internal();
      L(FL("dir add %s\n") % name);
      
      std::string parent,dir=name;
      std::string::size_type last_slash=name.rfind('/');
      if (last_slash!=std::string::npos)
      { parent=name.substr(0,last_slash);
        dir=name.substr(last_slash+1);
      }
      AddDirectory(dir,parent);
    }

    for (std::map<file_path, file_id>::const_iterator 
            i=cs->files_added.begin();
            i!=cs->files_added.end(); ++i)
    { 
      commit_arg a;
      a.file=file_path(i->first).as_internal();
//      if (a.file==".mtn-sync-"+app.opts.domain) continue;
      a.new_content=app.get_file(i->second).inner()();
      commits.push_back(a);
      L(FL("add %s %d\n") % a.file % a.new_content.size());
    }

    for (std::map<file_path, std::pair<file_id, file_id> >::const_iterator 
            i=cs->deltas_applied.begin();
            i!=cs->deltas_applied.end(); ++i)
    { 
      if (renamed_ids.find(i->first) != renamed_ids.end())
        continue; // a renamed file that's already been added with the correct contents
      commit_arg a;
      a.file=file_path(i->first).as_internal();
//      if (a.file==".mtn-sync-"+app.opts.domain) continue;
      cvs_manifest::const_iterator old=parent_manifest.find(a.file);
      I(old!=parent_manifest.end());
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      a.new_content=app.get_file(i->second.second).inner()();
      commits.push_back(a);
      L(FL("delta %s %s %s %d\n") % a.file % a.old_revision % a.keyword_substitution
          % a.new_content.size());
    }

    if (commits.empty())
    { W(F("revision %s: nothing to commit") % e.revision.inner()());
      if (parent!=edges.end()) e.delta_base=parent->revision;
      cert_cvs(e);
      revision_lookup[e.revision]=edges.insert(e).first;
      fail=false;
      return --(edges.end());
    }
    std::string changelog;
    changelog=e.changelog+"\nmonotone "+e.author+" "
        +cvs_client::time_t2rfc822(e.time)+" "+e.revision.inner()().substr(0,6)+"\n";
    // gather information CVS does not know about into the changelog
    changelog+=gather_merge_information(e.revision);
    std::map<std::string,std::pair<std::string,std::string> > result
      =Commit(changelog,e.time,commits);
    if (result.empty()) { fail=true; return edges.end(); }

    if (parent!=edges.end())    
      e.delta_base=parent->revision;
    
    // the result of the commit: create history entry (file state)
    //    FIXME: is this really necessary?
    for (std::map<std::string,std::pair<std::string,std::string> >::const_iterator
            i=result.begin(); i!=result.end(); ++i)
    { if (i->second.first.empty())
      { e.xfiles[i->first]=remove_state;
      }
      else
      { MM(i->first);
        file_state fs(e.time,i->second.first);
        fs.log_msg=e.changelog;
        fs.author=e.author;
        fs.keyword_substitution=i->second.second;
        file_path sp= file_path_internal(i->first);
        std::map<file_path, std::pair<file_id, file_id> >::const_iterator mydelta=cs->deltas_applied.find(sp);
        if (mydelta!=cs->deltas_applied.end())
        { fs.sha1sum=mydelta->second.second;
        }
        else // newly added?
        { std::map<file_path, file_id>::const_iterator myadd=cs->files_added.find(sp);
          if (myadd!=cs->files_added.end())
          { fs.sha1sum=myadd->second;
          }
          else  // renamed?
          { std::map<file_path, file_id>::const_iterator myrename=renamed_ids.find(sp);
            I(myrename!=renamed_ids.end());
            fs.sha1sum=myrename->second;
          }
        }
        std::pair<std::set<file_state>::iterator,bool> newelem=
            files[i->first].known_states.insert(fs);
        I(newelem.second);
        e.xfiles[i->first]=newelem.first;
      }
    }
    cert_cvs(e);
    revision_lookup[e.revision]=edges.insert(e).first;
    if (global_sanity.debug_p()) L(FL("%s") % debug());
    fail=false;
    return --(edges.end());
  }
  W(F("no matching parent found\n"));
  fail=true;
  return edges.end();
}

