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

static void read_file(std::string const& name, file_data &result)
{ std::string dest;
  ifstream is(name.c_str());
  while (is.good())
  { char buf[10240];
    is.read(buf,sizeof buf);
    if (is.gcount()) dest+=std::string(buf,buf+is.gcount());
  }
  result=file_data(dest,origin::system);
}

void cvs_repository::takeover_dir(const std::string &path)
{ // remember the server path for this subdirectory
  MM(path);
  { std::string repository;
    std::ifstream cvs_repository((path+"CVS/Repository").c_str());
    E(cvs_repository.good(), origin::workspace, F("can't open %sCVS/Repository\n") % path);
    std::getline(cvs_repository,repository);
    I(!repository.empty());
    if (repository[0]!='/') repository=root+"/"+repository;
    validate_path(path,repository+"/");
  }
  std::ifstream cvs_Entries((path+"CVS/Entries").c_str());
  E(cvs_Entries.good(),origin::workspace,
      F("can't open %s\n") % (path+"CVS/Entries"));
  L(FL("takeover_dir %s\n") % path);
  static hexenc<id> empty_file;
  while (true)
  { std::string line;
    std::getline(cvs_Entries,line);
    if (!cvs_Entries.good()) break;
    if (!line.empty())
    { std::vector<std::string> parts;
      MM(line);
      stringtok(parts,line,"/");
      // empty last part will not get created
      if (parts.size()==5) parts.push_back(std::string());
      if (parts.size()!=6) 
      { W(F("entry line with %d components '%s'\n") % parts.size() %line);
        continue;
      }
      if (parts[0]=="D")
      { takeover_dir(path+parts[1]+"/");
      }
      else // file
      {  // remember permissions, store file contents
        I(parts[0].empty());
        std::string filename=path+parts[1];
        I(!access(filename.c_str(),R_OK));
        // parts[2]=version
        // parts[3]=date
        // parts[4]=keyword subst
        // parts[5]='T'tag
        time_t modtime=-1;
        try
        { modtime=cvs_client::Entries2time_t(parts[3]);
        } 
        catch (recoverable_failure &e) {}
        catch (std::exception &e) {}
        
        I(files.find(filename)==files.end());
        std::map<std::string,file_history>::iterator f
            =files.insert(std::make_pair(filename,file_history())).first;
        file_state fs(modtime,parts[2]);
        fs.author="unknown";
        fs.keyword_substitution=parts[4];
        { struct stat sbuf;
          I(!stat(filename.c_str(), &sbuf));
          if (sbuf.st_mtime!=modtime)
          { L(FL("modified %s %u %u\n") % filename % modtime % sbuf.st_mtime);
            fs.log_msg="partially overwritten content from last update";
            store_contents(file_data(), fs.sha1sum);
            f->second.known_states.insert(fs);
            
            fs.since_when=time(NULL);
            fs.cvs_version=std::string();
          }
          fs.mode= sbuf.st_mode;
        }
        // import the file and check whether it is (un-)changed
        fs.log_msg="initial cvs content";
        file_data new_data;
        read_file(filename, new_data);
        store_contents(new_data, fs.sha1sum);
        f->second.known_states.insert(fs);
      }
    }
  }
}

void cvs_repository::takeover()
{ app.open();
  takeover_dir("");
  
  bool need_second=false;
  cvs_edge e1,e2;
  e1.time=0;
  e1.changelog="last cvs update (modified)";
  e1.changelog_valid=true;
  e1.author="unknown";
  e2.time=time(NULL);
  e2.changelog="cvs takeover";
  e2.changelog_valid=true;
  e2.author="unknown";
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { cvs_file_state first,second;
    first=i->second.known_states.begin();
    I(first!=i->second.known_states.end());
    second=first;
    ++second;
    if (second==i->second.known_states.end()) second=first;
    else if (!need_second) need_second=true;
    if (e1.time<first->since_when) e1.time=first->since_when;
    e1.xfiles[i->first]=first;
    e2.xfiles[i->first]=second;
    // at most two states known !
    I((++second)==i->second.known_states.end());
  }
  if (!need_second) e1.changelog=e2.changelog;
  edges.insert(e1);
  if (need_second)
  { edges.insert(e2);
  }
  // commit them all
  commit_cvs2mtn(edges.begin());
  
  app.close();
  // now we initialize the workspace
  // mtn setup .
  { std::vector<std::string> args;
    args.push_back(app.opts.mtn_binary);
    if (args[0].empty()) args[0]="mtn";
    for (std::vector<std::string>::const_iterator i=app.opts.mtn_options.begin();i!=app.opts.mtn_options.end();++i)
      args.push_back(*i);
    args.push_back("--branch");
    args.push_back(app.opts.branchname());
    args.push_back("setup");
    args.push_back(".");
    I(args.size()<30);
    const char *argv[30];
    unsigned i=0;
    for (;i<30 && i<args.size();++i) argv[i]=args[i].c_str();
    argv[i]=0;
    process_spawn(argv);
  }
  { ofstream of("_MTN/revision");
    if (!of.good())
    {
      W(F("_MTN/revision still busy?"));
      sleep(1);
      of.open("_MTN/revision",std::ios_base::out|std::ios_base::trunc);
    }
    I(of.good());
    I(!edges.empty());
    I(!(--edges.end())->revision.inner()().empty());
    of << "format_version \"1\"\n\n"
      "new_manifest [0000000000000000000000000000000000000001]\n\n"
      "old_revision [" << encode_hexenc((--edges.end())->revision.inner()(),origin::internal) << "]\n";
  }
// like in commit ?
//  update_any_attrs(app);
//  put_revision_id((--edges.end())->revision);
//  maybe_update_inodeprints(app);
//  store_modules();
}

// read in directory put into db
void cvs_sync::takeover(mtncvs_state &app, const std::string &_module)
{ std::string root,module=_module,branch;

  E(access("_MTN",F_OK),origin::workspace,F("Found a _MTN file or directory, already under monotone's control?"));
  { fstream cvs_root("CVS/Root");
    E(cvs_root.good(),origin::workspace,
      F("can't open ./CVS/Root, please change into the working directory\n"));
    std::getline(cvs_root,root);
  }
  { fstream cvs_branch("CVS/Tag");
    if (cvs_branch.good())
    { std::getline(cvs_branch,branch);
      MM(branch);
      I(!branch.empty());
      I(branch[0]=='T');
      branch.erase(0,1);
    }
  }
  if (module.empty())
  { fstream cvs_repository("CVS/Repository");
    E(cvs_repository.good(),origin::workspace,
      F("can't open ./CVS/Repository\n"));
    std::getline(cvs_repository,module);
    W(F("Guessing '%s' as the module name\n") % module);
  }
//  test_key_availability(app);
  cvs_sync::cvs_repository repo(app,root,module,branch,false);
  // FIXME? check that directory layout matches the module structure
  repo.takeover();
}
//#endif

