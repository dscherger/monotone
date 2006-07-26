#include <app_state.hh>
#include <mtn_pipe.hh>

struct mtncvs_state : app_state
{ bool full;
  utf8 since;
  utf8 db_name;
  std::vector<utf8> revisions;
  std::vector<utf8> mtn_options;
  class mtn_pipe mtn_pipe;
  
  mtncvs_state() : full() {}
  
  void open_pipe();
};
