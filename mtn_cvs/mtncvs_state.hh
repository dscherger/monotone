#include <app_state.hh>
#include <netxx_pipe.hh>

struct mtncvs_state : app_state
{ bool full;
  utf8 since;
  utf8 db_name;
  std::vector<utf8> revisions;
  std::vector<utf8> mtn_options;
  Netxx::PipeStream *mtn_pipe;
  
  mtncvs_state() : full(), mtn_pipe() {}
  ~mtncvs_state();
  
  void open_pipe();
};
