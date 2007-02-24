#include "crescendo-sanity.hh"

extern sanity & global_sanity;
crescendo_sanity real_sanity;
sanity & global_sanity = real_sanity;

crescendo_sanity::crescendo_sanity() : relaxed(false)
{}

crescendo_sanity::~crescendo_sanity()
{}

void
crescendo_sanity::initialize(int argc, char ** argv, char const * lc_all)
{
  std::string full_version_string="1.0";
  //  get_full_version(full_version_string);
  PERM_MM(full_version_string);
  
  this->sanity::initialize(argc, argv, lc_all);
}

void
crescendo_sanity::set_relaxed(bool rel)
{
  relaxed = rel;
}

void
crescendo_sanity::inform_log(std::string const &msg)
{
  // ui.inform(msg);
}

void
crescendo_sanity::inform_message(std::string const &msg)
{
  // ui.inform(msg);
}

void
crescendo_sanity::inform_warning(std::string const &msg)
{
  // ui.warn(msg);
}

void
crescendo_sanity::inform_error(std::string const &msg)
{
  // ui.inform(msg);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

