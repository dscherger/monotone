// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <set>
#include <stdarg.h>
#include <zlib.h>

struct rlog_callbacks
{ // virtual void file(const std::string &file,)=0;
  virtual void file(const std::string &file,
        const std::string &head_rev) const=0;
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const=0;
  virtual void revision(const std::string &file,time_t checkin_date,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const=0;
};

struct rlist_callbacks
{ virtual void file(const std::string &name, time_t last_change,
        const std::string &last_rev, bool dead) const=0;
};

struct checkout
{ time_t mod_time;
  std::string contents;
  std::string mode;
  bool dead;
  
  checkout() : mod_time(-1), dead() {}
};

class cvs_client
{public:
  struct update
  { std::string contents;
    std::string checksum;
    std::string patch;
    bool removed;
    update() : removed() {}
  };
  typedef struct checkout checkout;
  typedef struct rlist_callbacks rlist_callbacks;
  typedef struct rlog_callbacks rlog_callbacks;

private:
  int readfd,writefd;
  size_t bytes_read,bytes_written;
  typedef std::set<std::string> stringset_t;
  stringset_t Valid_requests;
  int gzip_level;
  z_stream compress,decompress;
  std::string inputbuffer;

  void InitZipStream(int level);
  void underflow(); // fetch new characters from stream
  bool begins_with(const std::string &s, const std::string &sub);
protected:
  std::string root;
  std::string module;
  std::string rcs_root; // the real directory of the root (ask cvs.gnome.org)
  
public:  
  cvs_client(const std::string &host, const std::string &_root,
             const std::string &user=std::string(), 
             const std::string &module=std::string(), bool pserver=false);
  ~cvs_client();
             
  void writestr(const std::string &s, bool flush=false);
  std::string readline();
  std::string read_n(unsigned size);
  
  size_t get_bytes_read() const { return bytes_read; }
  size_t get_bytes_written() const { return bytes_written; }
  void ticker(bool newline=true) const;
  void SendCommand(const char *cmd,...);
  void SendCommand(const char *cmd, va_list ap);
  // false if none available
  bool fetch_result(std::string &result);
  // semi internal helper to get one result line from a list
  static std::string combine_result(const std::vector<std::pair<std::string,std::string> > &result);
  static bool begins_with(const std::string &s, const std::string &sub, unsigned &len);
  
  // MT style
  bool fetch_result(std::vector<std::pair<std::string,std::string> > &result);
  void GzipStream(int level);
  
  void RLog(const rlog_callbacks &cb,bool dummy,...);
  void RList(const rlist_callbacks &cb,bool dummy,...);
  struct checkout CheckOut(const std::string &file, const std::string &revision);
  struct update Update(const std::string &file, 
            const std::string &old_revision, const std::string &new_revision);
  
  bool CommandValid(const std::string &cmd) const
  { return Valid_requests.find(cmd)!=Valid_requests.end(); }
  static std::string pserver_password(const std::string &root);
};

