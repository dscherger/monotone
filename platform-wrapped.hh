#ifndef __PLATFORM_WRAPPED_HH__
#define __PLATFORM_WRAPPED_HH__

#include "paths.hh"
#include "platform.hh"
#include "transforms.hh"
#include "vocab.hh"

inline bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip)
{
  std::string out;
  bool ret = inodeprint_file(file.as_external(), out);
  inodeprint ip_raw(out);
  encode_hexenc(ip_raw, ip);
  return ret;
}

inline utf8 tilde_expand(utf8 const & path)
{
  return tilde_expand(path());
}

inline void change_current_working_dir(any_path const & to)
{
  change_current_working_dir(to.as_external());
}

inline path::status get_path_status(any_path const & path)
{
  return get_path_status(path.as_external());
}

inline void rename_clobberingly(any_path const & from, any_path const & to)
{
  rename_clobberingly(from.as_external(), to.as_external());
}

#endif
