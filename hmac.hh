#ifndef __HMAC_HH__
#define __HMAC_HH__

#include <string>

#include "botan/botan.h"
#include "vocab.hh"
#include "constants.hh"
#include "string_queue.hh"

struct chained_hmac
{
public:
  chained_hmac(netsync_session_key const & session_key, bool active);
  void set_key(netsync_session_key const & session_key);
  std::string process(std::string const & str, size_t pos = 0,
		      size_t n = std::string::npos);
  std::string process(string_queue const & str, size_t pos = 0,
		      size_t n = std::string::npos);

  size_t const hmac_length;
  bool is_active() { return active; }

private:
  bool active;
  Botan::SymmetricKey key;
  std::string chain_val;
};




#endif // __HMAC_HH__

