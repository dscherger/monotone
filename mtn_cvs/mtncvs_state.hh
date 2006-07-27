#include <app_state.hh>
#include <mtn_pipe.hh>

struct mtncvs_state : private app_state
{ bool full;
  utf8 since;
  std::vector<utf8> revisions;
  utf8 mtn_binary;
  std::vector<utf8> mtn_options;
  class mtn_pipe mtn_pipe;
  utf8 branch;
  
  mtncvs_state() : full(), mtn_binary("mtn") {}
 
// to access the private base class (only to pass it around)
  app_state& downcast() { return *this; }
  static mtncvs_state& upcast(app_state &app) 
  { return static_cast<mtncvs_state&>(app); }
  
  void open_pipe();
  void dump();
};
