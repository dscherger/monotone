#include <app_state.hh>
#include <mtn_pipe.hh>

struct mtncvs_state : app_state
{ bool full;
  utf8 since;
  std::vector<utf8> revisions;
  utf8 mtn_binary;
  std::vector<utf8> mtn_options;
  class mtn_pipe mtn_pipe;
  
  mtncvs_state() : full(), mtn_binary("mtn") {}
  
  void open_pipe();
};
