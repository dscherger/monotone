#include <app_state.hh>

struct mtncvs_state : app_state
{ bool full;
  utf8 since;
  utf8 db_name;
  std::vector<utf8> revisions;
  
  mtncvs_state() : full() {}
};
