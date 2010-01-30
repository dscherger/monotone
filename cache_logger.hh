#ifndef __CACHE_LOGGER_HH__
#define __CACHE_LOGGER_HH__

#include <boost/shared_ptr.hpp>

class cache_logger_impl;

class cache_logger
{
  boost::shared_ptr<cache_logger_impl> _impl;
  int max_size;
public:
  // if given the empty filename, do nothing
  explicit cache_logger(std::string const & filename, int max_size);

  bool logging() const { return _impl; }

  void log_exists(bool exists, int position, int item_count, int est_size) const;
  void log_touch(bool exists, int position, int item_count, int est_size) const;
  void log_fetch(bool exists, int position, int item_count, int est_size) const;
  void log_insert(int items_removed, int item_count, int est_size) const;
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
