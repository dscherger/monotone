#ifndef __CRESCENDO_SANITY_HH__
#define __CRESCENDO_SANITY_HH__

#include "sanity.hh"

struct crescendo_sanity : public sanity
{
  bool relaxed;

  crescendo_sanity();
  ~crescendo_sanity();
  void initialize(int, char **, char const *);

  void set_relaxed(bool rel);

private:
  void inform_log(std::string const &msg);
  void inform_message(std::string const &msg);
  void inform_warning(std::string const &msg);
  void inform_error(std::string const &msg);
};

extern crescendo_sanity real_sanity;

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

