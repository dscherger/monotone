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

template <class A,class B>
 B const_map_access(std::map<A,B> const& m, A const& a)
{ typename std::map<A,B>::const_iterator i=m.find(a);
  if (i!=m.end()) return i->second;
  else return B();
}

//  -> investigate under which conditions a string gets copied
//static std::string const cvs_cert_name="cvs-revisions";

bool file_state::operator<(const file_state &b) const
{ return since_when<b.since_when
    || (since_when==b.since_when 
        && cvs_revision_nr(cvs_version)<cvs_revision_nr(b.cvs_version));
}

/* supported by the woody version:
Root Valid-responses valid-requests Repository Directory Max-dotdot
Static-directory Sticky Checkin-prog Update-prog Entry Kopt Checkin-time
Modified Is-modified UseUnchanged Unchanged Notify Questionable Case
Argument Argumentx Global_option Gzip-stream wrapper-sendme-rcsOptions Set
expand-modules ci co update diff log rlog add remove update-patches
gzip-file-contents status rdiff tag rtag import admin export history release
watch-on watch-off watch-add watch-remove watchers editors annotate
rannotate noop version
*/

//--------------------- implementation -------------------------------

// cvs_repository ----------------------

// very short form to output in logs etc.
std::string cvs_repository::time_t2human(const time_t &t)
{ struct tm *tm;
  tm=gmtime(&t);
  return (boost::format("%02d%02d%02dT%02d%02d%02d") % (tm->tm_year%100) 
      % (tm->tm_mon+1) % tm->tm_mday % tm->tm_hour % tm->tm_min 
      % tm->tm_sec).str();
}

struct cvs_repository::get_all_files_log_cb : rlog_callbacks
{ cvs_repository &repo;
  get_all_files_log_cb(cvs_repository &r) : repo(r) {}
  virtual void file(const std::string &file,const std::string &head_rev) const
  { L(FL("get_all_files_log_cb %s") % file);
    repo.files[file]; 
  }
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const {}
  virtual void revision(const std::string &file,time_t t,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const {}
};

// get all available files and their newest revision
void cvs_repository::get_all_files()
{ if (edges.empty())
  { // rlist seems to be more efficient but it's hard to guess the directory the
    // server talks about
    I(CommandValid("rlog"));
    RLog(get_all_files_log_cb(*this),false,"-N","-h","--",module.c_str(),(void*)0);
  }
}

std::string debug_manifest(const cvs_manifest &mf)
{ std::string result;
  for (cvs_manifest::const_iterator i=mf.begin(); i!=mf.end(); ++i)
  { result+= i->first + " " + i->second->cvs_version;
    if (!i->second->keyword_substitution.empty()) 
      result+="/"+i->second->keyword_substitution;
    result+=" " + std::string(i->second->dead?"dead ":"") + encode_hexenc(i->second->sha1sum.inner()()) + "\n";
  }
  return result;
}

template <> void
static dump(cvs_sync::file_state const& fs, std::string& result)
{ 
    result="since "+cvs_repository::time_t2human(fs.since_when);
    result+=" V"+fs.cvs_version+" ";
    if (fs.dead) result+= "dead";
    else if (fs.size) result+= boost::lexical_cast<string>(fs.size);
    else if (fs.patchsize) result+= "p" + boost::lexical_cast<string>(fs.patchsize);
    else if (!fs.sha1sum.inner()().empty()) result+= encode_hexenc(fs.sha1sum.inner()()).substr(0,4) + fs.keyword_substitution;
    result+=" "+fs.log_msg.substr(0,20)+"\n";
}

std::string cvs_repository::debug_file(std::string const& name)
{ std::map<std::string,file_history>::const_iterator i=files.find(name);
  E(i!=files.end(),F("file '%s' not found\n") % name);
  std::string result;
  for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
        j!=i->second.known_states.end();++j)
  { 
    std::string part;
    dump(*j, part);
    result+=part;
    result+='\n';
  }
  return result;
}

#if 0
std::string debug_files(const std::map<std::string,file_history> &files)
{ std::string result;
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { result += i->first;
    result += " (";
    for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
          j!=i->second.known_states.end();)
    { string toadd;
      dump(*j,toadd);
      result+=toadd;
      ++j;
      if (j!=i->second.known_states.end()) result += ",";
    }
    result += ")\n";
  }
  return result;
}
#endif

// returns the length of the first line (header) and fills in fields
void cvs_repository::parse_cvs_cert_header(
      mtn_automate::sync_map_t const& value, std::string const& domain,
      std::string &repository, std::string& module, std::string& branch)
{ 
//  MM(value);
  file_path sp=file_path_internal("");
  repository=const_map_access(value,std::make_pair(sp,attr_key(domain+":root")))();
  module=const_map_access(value,std::make_pair(sp,attr_key(domain+":module")))();
  branch=const_map_access(value,std::make_pair(sp,attr_key(domain+":branch")))();
}

mtn_automate::sync_map_t cvs_repository::create_cvs_cert_header() const
{ 
  mtn_automate::sync_map_t result;
  file_path sp=file_path_internal("");
  result[std::make_pair(sp,attr_key(app.opts.domain+":root"))]= attr_value(host+":"+root);
  result[std::make_pair(sp,attr_key(app.opts.domain+":module"))]= attr_value(module);
  if (!branch.empty())
    result[std::make_pair(sp,attr_key(app.opts.domain+":branch"))]= attr_value(branch);
  return result;
}

template <> void
static dump(cvs_sync::cvs_edge const& e, std::string& result)
{ result= "[" + cvs_repository::time_t2human(e.time);
    if (e.time!=e.time2) result+= "+" + boost::lexical_cast<string>(e.time2-e.time);
    if (!e.revision.inner()().empty()) result+= "," + encode_hexenc(e.revision.inner()()).substr(0,4);
    if (!e.xfiles.empty()) 
      result+= "," + boost::lexical_cast<string>(e.xfiles.size()) 
         + (e.delta_base.inner()().empty()?"files":"deltas");
    result+= "," + e.author + ",";
    std::string::size_type nlpos=e.changelog.find_first_of("\n\r");
    if (nlpos>50) nlpos=50;
    result+= e.changelog.substr(0,nlpos) + "]";
}

std::string cvs_repository::debug() const
{ std::string result;

  // edges set<cvs_edge>
  result+= "Edges :\n";
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  {
    std::string edge_part;
    dump(*i, edge_part);
    result+=edge_part;
    result+='\n';
  }
  result+= "Files :\n";
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { result+= i->first;
    result+= " (";
    for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
          j!=i->second.known_states.end();)
    { std::string toadd;
      dump(*j,toadd);
      result+=toadd;
      ++j;
      if (j!=i->second.known_states.end()) result+= ",";
    }
    result+= ")\n";
  }
  result+= "Tags :\n";
  for (std::map<std::string,std::map<std::string,cvs_revision_nr> >::const_iterator i=tags.begin();
      i!=tags.end();++i)
  { result+= i->first + "(" + boost::lexical_cast<string>(i->second.size()) + " files)\n";
  }
  return result;
}

void cvs_repository::fill_manifests(std::set<cvs_edge>::iterator e)
{ cvs_manifest current_manifest;
  if (e!=edges.begin())
  { std::set<cvs_edge>::const_iterator before=e;
    --before;
    current_manifest=get_files(*before);
  }
  for (;e!=edges.end();++e)
  { MM(*e);
    std::set<cvs_edge>::iterator next_edge=e;
    ++next_edge;
    for (std::map<std::string,file_history>::const_iterator f=files.begin();f!=files.end();++f)
    { I(!branch.empty() || !f->second.known_states.empty());
      // this file does not belong to this branch
      if (f->second.known_states.empty()) continue; 
      if (!(*(f->second.known_states.begin()) <= (*e)))
      // the file does not exist yet (first is not below/equal current edge)
      { L(FL("%s before beginning %s/%s+%d\n") % f->first 
            % time_t2human(f->second.known_states.begin()->since_when)
            % time_t2human(e->time) % (e->time2-e->time));
        continue; 
      }
      cvs_manifest::iterator mi=current_manifest.find(f->first);
      if (mi==current_manifest.end()) // the file is currently dead
      { cvs_file_state s=f->second.known_states.end();
        // find last revision that fits but does not yet belong to next edge
        // use until end, or above range, or belongs to next edge
        for (cvs_file_state s2=f->second.known_states.begin();
             s2!=f->second.known_states.end() 
             && (*s2)<=(*e)
             && ( next_edge==edges.end() || ((*s2)<(*next_edge)) );
             ++s2)
        { L(FL("%s matches %s/%s+%d\n") % f->first 
            % time_t2human(s2->since_when)
            % time_t2human(e->time) % (e->time2-e->time));
          s=s2;
        }
          
        if (s!=f->second.known_states.end() && !s->dead)
        // a matching revision was found
        { current_manifest[f->first]=s;
          I(!s->sha1sum.inner()().empty());
          check_split(s,f->second.known_states.end(),e);
        }
      }
      else // file was present in last manifest, check whether next revision already fits
      {
        MM(mi->first);
        cvs_file_state s=mi->second;
        MM(*s);
        ++s;
        MM(*s);
        if (s!=f->second.known_states.end() 
            && (*s)<=(*e)
            && ( next_edge==edges.end() || ((*s)<(*next_edge)) ) )
        { if (s->dead) current_manifest.erase(mi);
          else 
          { 
            mi->second=s;
            I(!s->sha1sum.inner()().empty());
          }
          check_split(s,f->second.known_states.end(),e);
        }
      }
    }
    e->xfiles=current_manifest;
  }
}

mtn_automate::sync_map_t cvs_repository::create_sync_state(cvs_edge const& e)
{ mtn_automate::sync_map_t state=create_cvs_cert_header();
  const std::map<std::string,std::string> &sd=GetServerDir();
  for (std::map<std::string,std::string>::const_iterator i=sd.begin();
        i!=sd.end();++i)
  { std::string dirname=i->first;
    if (!dirname.empty())
    { I(dirname[dirname.size()-1]=='/');
      dirname.erase(--dirname.end());
    }
    file_path sp=file_path_internal(dirname);
    if (!dirname.empty() || i->second!=root+"/"+module+"/")
      state[std::make_pair(sp,attr_key(app.opts.domain+":path"))]=attr_value(i->second);
  }
  
  for (cvs_manifest::const_iterator i=e.xfiles.begin(); i!=e.xfiles.end(); ++i)
  { 
#if 1
    if (i->second->cvs_version.empty())
    { if (i->second->sha1sum.inner()().empty())
      { W(F("internal error: directory '%s' skipped\n") % i->first); 
        continue;
      }
      W(F("blocking attempt to certify an empty CVS revision of '%s'\n"
        "(this is normal for a cvs_takeover of a locally modified tree)\n"
        "%s")
        % i->first % debug_manifest(e.xfiles));
      return mtn_automate::sync_map_t();
    }
#else
    I(!i->second->cvs_version.empty());
#endif
    file_path sp=file_path_internal(i->first);
    state[std::make_pair(sp,attr_key(app.opts.domain+":revision"))]
        =attr_value(i->second->cvs_version);
    if (!i->second->keyword_substitution.empty())
      state[std::make_pair(sp,attr_key(app.opts.domain+":keyword"))]
          =attr_value(i->second->keyword_substitution);
// FIXME: How to flag locally modified files? add the synched sha1sum?
    if (!i->second->sha1sum.inner()().empty())
      state[std::make_pair(sp,attr_key(app.opts.domain+":sha1"))]
            =attr_value(encode_hexenc(i->second->sha1sum.inner()()).substr(0,6));
  }
  return state;
}

cvs_repository::cvs_repository(mtncvs_state &_app, const std::string &repository, 
            const std::string &module, const std::string &branch, bool connect)
      : cvs_client(repository,module,branch,connect), app(_app), 
        file_id_ticker(), revision_ticker(), cvs_edges_ticker(), 
        remove_state(), sync_since(-1)
{
  file_id_ticker.reset(new ticker("file ids", "F", 10));
  remove_state=remove_set.insert(file_state(0,"-",true)).first;
  if (!app.opts.since.empty())
  { sync_since=posix2time_t(app.opts.since);
    N(sync_since<=time(0), F("Since lies in the future. Remember to specify time in UTC\n"));
  }
}

std::set<cvs_edge>::iterator cvs_repository::last_known_revision()
{ I(!edges.empty());
  std::set<cvs_edge>::iterator now_iter=edges.end();
  --now_iter;
  return now_iter;
}

time_t cvs_repository::posix2time_t(std::string posix_format)
{ std::string::size_type next_illegal=0;
  MM(posix_format);
  while ((next_illegal=posix_format.find_first_of("-:"))!=std::string::npos)
        posix_format.erase(next_illegal,1);
  boost::posix_time::ptime tmp= boost::posix_time::from_iso_string(posix_format);
  boost::posix_time::time_duration dur= tmp
          -boost::posix_time::ptime(boost::gregorian::date(1970,1,1),
                          boost::posix_time::time_duration(0,0,0,0));
  return dur.total_seconds();
}

// look for last sync cert in the given monotone branch and assign
// its value to repository, module, branch

static void guess_repository(std::string &repository, std::string &module,
        std::string & branch,mtn_automate::sync_map_t &last_state, revision_id &lastid, mtncvs_state &app)
{ I(!app.opts.branchname().empty());
  try
  { lastid=app.find_newest_sync(app.opts.domain,app.opts.branchname());
    if (null_id(lastid)) 
    { L(FL("no sync information found on branch %s\n")%app.opts.branchname());
      return;
    }
    last_state=app.get_sync_info(lastid,app.opts.domain);
    cvs_repository::parse_cvs_cert_header(last_state,app.opts.domain,repository,module,branch);
    if (branch.empty())
      L(FL("using module '%s' in repository '%s'\n") % module % repository);
    else
      L(FL("using branch '%s' of module '%s' in repository '%s'\n") 
                % branch % module % repository);
  }
  catch (std::runtime_error)
  { N(false, F("can not guess repository (in domain %s), "
        "please specify on first pull") % app.opts.domain);
  }
}

#if 1
cvs_sync::cvs_repository *cvs_sync::prepare_sync(const std::string &_repository, const std::string &_module,
            std::string const& _branch, mtncvs_state &app)
{ app.open();
  std::string repository=_repository, module=_module, branch=_branch;
  
  mtn_automate::sync_map_t last_sync_info;
  revision_id lastid;
  if (app.opts.branchname().empty())
  {
    app.opts.branchname=branch_name(app.get_option("branch"));
    if (!app.opts.branchname().empty() && app.opts.branchname()[app.opts.branchname().size()-1]=='\n')
      app.opts.branchname=branch_name(app.opts.branchname().substr(0,app.opts.branchname().size()-1));
  }
  N(!app.opts.branchname().empty(), F("no destination branch specified\n"));
  { std::string rep,mod,br;
    // search for module and last revision
    guess_repository(rep, mod, br, last_sync_info, lastid, app);
    L(FL("prepare_sync: last id %s\n") % lastid);
    if (repository.empty() || module.empty())
    { repository=rep;
      module=mod;
      branch=br;
    }
    else
    { MM(repository); 
      MM(rep); 
      MM(module); 
      MM(mod); 
      MM(branch); 
      MM(br);
      if (!last_sync_info.empty() && repository!=rep)
        W(F("Repositories do not match: '%s' != '%s'\n") % repository % rep);
      if (!last_sync_info.empty() && module!=mod)
        W(F("Modules do not match: '%s' != '%s'\n") % module % mod);
      if (!last_sync_info.empty() && branch!=br)
        W(F("Branches do not match: '%s' != '%s'\n") % branch % br);
    }
  }
  N(!repository.empty(), F("you must name a repository, I can't guess"));
  N(!module.empty(), F("you must name a module, I can't guess"));
   
  cvs_repository *repo = new cvs_repository(app,repository,module,branch);
// turn compression on when not DEBUGGING
  if (!getenv("CVS_CLIENT_LOG"))
    repo->GzipStream(3);

  if (!last_sync_info.empty())
  { repo->parse_module_paths(last_sync_info);
    repo->process_sync_info(last_sync_info, lastid);
  }
  return repo;
}

revision_id cvs_sync::last_sync(mtncvs_state &app)
{
	app.open();
	  mtn_automate::sync_map_t last_sync_info;
	  revision_id lastid;
	  app.opts.branchname=branch_name(app.get_option("branch"));
	  if (!app.opts.branchname().empty() && app.opts.branchname()[app.opts.branchname().size()-1]=='\n')
	      app.opts.branchname=branch_name(app.opts.branchname().substr(0,app.opts.branchname().size()-1));
	  std::string rep,mod,br;
	    // search for module and last revision
	  guess_repository(rep, mod, br, last_sync_info, lastid, app);
	  return lastid;	
}

#endif

#if 1 // used by process_sync_info
cvs_file_state cvs_repository::remember(std::set<file_state> &s,const file_state &fs, std::string const& filename)
{ for (std::set<file_state>::iterator i=s.begin();i!=s.end();++i)
  { if (i->cvs_version==fs.cvs_version)
    { if (i->since_when>fs.since_when) // i->since_when has to be the minimum
        const_cast<time_t&>(i->since_when)=fs.since_when;
      static file_id emptysha1sum;
      if (emptysha1sum.inner()().empty())
        calculate_ident(file_data(),emptysha1sum);
      if (i->log_msg=="last cvs update (modified)" 
            && i->sha1sum==emptysha1sum
            && i->author==("unknown@"+host))
      { W(F("replacing fake contents for %s V%s\n")
            % filename % i->cvs_version);
//        const_cast<hexenc<id>&>(i->sha1sum)=fs.sha1sum;
        const_cast<std::string&>(i->log_msg)=fs.log_msg;
      }
      return i;
    }
  }
  std::pair<cvs_file_state,bool> iter=s.insert(fs);
  I(iter.second);
  return iter.first;
}
#endif

void cvs_repository::process_sync_info(mtn_automate::sync_map_t const& sync_info, revision_id const& rid)
{ mtn_automate::manifest_map manifest=app.get_manifest_of(rid);
  // populate data structure using this sync info
      cvs_edge e(rid,app);

      for (mtn_automate::manifest_map::const_iterator i=manifest.begin();
              i!=manifest.end();++i)
      {
        // populate the file info
        file_path sp= i->first;
        
        file_state fs;
        fs.since_when=e.time;
        fs.cvs_version=const_map_access(sync_info,std::make_pair(sp,attr_key(app.opts.domain+":revision")))();
        fs.cvssha1sum=const_map_access(sync_info,std::make_pair(sp,attr_key(app.opts.domain+":sha1")))();
        fs.keyword_substitution=const_map_access(sync_info,std::make_pair(sp,attr_key(app.opts.domain+":keywords")))();
        
        fs.sha1sum=i->second.first;
        if (fs.sha1sum.inner()().empty()) continue; // directory node
        fs.log_msg=e.changelog;
        fs.author=e.author;
        std::string path=file_path(i->first).as_internal();
        cvs_file_state cfs=remember(files[path].known_states,fs,path);
        e.xfiles.insert(std::make_pair(path,cfs));
      }
      revision_lookup[e.revision]=edges.insert(e).first;
#warning do I need this code?
#if 0
  // because some manifests might have been absolute (not delta encoded)
  // we possibly did not notice removes. check for them
  std::set<cvs_edge>::const_iterator last=edges.end();
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { if (last!=edges.end() && i->delta_base.inner()().empty())
      try
      { cvs_manifest old=get_files(*last),new_m=get_files(*i);
        for (cvs_manifest::iterator j=old.begin();j!=old.end();++j)
        { cvs_manifest::iterator new_iter=new_m.find(j->first);
          if (new_iter==new_m.end()) // this file get's removed here
          { file_state fs;
            fs.since_when=i->time;
            cvs_revision_nr rev=j->second->cvs_version;
            ++rev;
            fs.cvs_version=rev.get_string();
            fs.log_msg=i->changelog;
            fs.author=i->author;
            fs.dead=true;
            L(FL("file %s gets removed at %s\n") % j->first % i->revision.inner()());
            remember(files[j->first].known_states,fs,j->first);
          }
        }
      }
      catch (informative_failure &e)
      { L(FL("failed to reconstruct CVS revisions: %s: %s->%s\n")
            % e.what % last->revision.inner()() % i->revision.inner()());
      }
    last=i;
  }
#endif
  if (global_sanity.debug_p()) L(FL("%s") % debug());
}

static void apply_manifest_delta(cvs_manifest &base,const cvs_manifest &delta)
{ L(FL("apply_manifest_delta: base %d delta %d\n") % base.size() % delta.size());
  for (cvs_manifest::const_iterator i=delta.begin(); i!=delta.end(); ++i)
  { if (i->second->dead)
    { cvs_manifest::iterator to_remove=base.find(i->first);
      I(to_remove!=base.end());
      base.erase(to_remove);
    }
    else
      base[i->first]=i->second;
  }
  L(FL("apply_manifest_delta: result %d\n") % base.size());
}

#if 0
void cvs_sync::debug(const std::string &command, const std::string &arg, 
            mtncvs_state &app)
{ 
  // we default to the first repository found (which might not be what you wanted)
  if (command=="manifest" && arg.size()==constants::idlen)
  { revision_id rid(arg);
    // easy but not very efficient way, since we parse all revisions to decode
    // the delta encoding
    // (perhaps?) it would be better to retrieve needed revisions recursively
    std::vector< revision<cert> > certs;
    app.db.get_revision_certs(rid,cvs_cert_name,certs);
    // erase_bogus_certs ?
    N(!certs.empty(),F("revision has no 'cvs-revisions' certificates\n"));
    
    std::string repository,module,branch;
    cvs_repository::parse_cvs_cert_header(certs.front(), repository, module, branch);
    cvs_sync::cvs_repository repo(app,repository,module,branch,false);
    app.db.get_revision_certs(cvs_cert_name, certs);
    repo.process_certs(certs);
    std::cout << debug_manifest(repo.get_files(rid));
  }
  else if (command=="history") // filename or empty
  { 
    std::string repository, module, branch;
    std::vector< revision<cert> > certs;
    guess_repository(repository, module, branch, certs, app);
    cvs_sync::cvs_repository repo(app,repository,module,branch,false);
    repo.process_certs(certs);
    if (arg.empty()) std::cout << repo.debug();
    else std::cout << repo.debug_file(arg);
  }
}
#endif

void cvs_client::validate_path(const std::string &local, const std::string &server)
{ for (std::map<std::string,std::string>::const_iterator i=server_dir.begin();
      i!=server_dir.end();++i)
  { if (local.substr(0,i->first.size())==i->first 
        && server.substr(0,i->second.size())==i->second
        && local.substr(i->first.size())==server.substr(i->second.size()))
      return;
  }
  server_dir[local]=server;
}

void cvs_sync::test(mtncvs_state &app)
{
  I(!app.opts.revisions.empty());
  app.open();
  revision_id rid=*app.opts.revisions.begin();
  app.get_revision_certs(rid);
}

void cvs_repository::parse_module_paths(mtn_automate::sync_map_t const& mp)
{ 
  std::map<std::string,std::string> sd;
  bool active=false;
  for (mtn_automate::sync_map_t::const_iterator i=mp.begin(); i!=mp.end(); ++i)
  {
    if (i->first.second()==app.opts.domain+":path")
    { L(FL("found module %s:%s") % i->first.first % i->second());
      std::string path=file_path(i->first.first).as_internal();
      if (!path.empty()) path+='/';
      sd[path]=i->second();
    }
  }
  // how can we know that this is all?
  if (sd.find("")==sd.end())
  { sd[""]=root+"/"+module+"/";
  }
  SetServerDir(sd);
}

// is this a no-op?
void cvs_repository::retrieve_modules()
{ if (!GetServerDir().empty()) return;
}

// we could pass delta_base and forget about it later
void cvs_repository::cert_cvs(cvs_edge const& e)
{ mtn_automate::sync_map_t content=create_sync_state(e);
  app.put_sync_info(e.revision,app.opts.domain,content);
}
