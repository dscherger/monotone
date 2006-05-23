#include <string>

#include "botan/botan.h"

#include "sanity.hh"
#include "hmac.hh"
#include "vocab.hh"
#include "constants.hh"

chained_hmac::chained_hmac(netsync_session_key const & session_key, bool active) :
  hmac_length(constants::sha1_digest_length), 
  active(active),
  key(reinterpret_cast<Botan::byte const *>(session_key().data()), session_key().size())
{
  chain_val.assign(hmac_length, 0x00);
}

void
chained_hmac::set_key(netsync_session_key const & session_key)
{
  if (active)
    {
      key = Botan::SymmetricKey(reinterpret_cast<Botan::byte const *>(session_key().data()),
				session_key().size());
    }
}

std::string
chained_hmac::process(std::string const & str, size_t pos, size_t n)
{
  I(active);

  I(pos < str.size());
  if (n == std::string::npos)
    n = str.size() - pos;

  I(pos + n <= str.size());

  Botan::Pipe p(new Botan::MAC_Filter("HMAC(SHA-160)", key,
                                      constants::sha1_digest_length));
  p.start_msg();
  p.write(chain_val);
  p.write(reinterpret_cast<Botan::byte const *>(str.data() + pos), n);
  p.end_msg();

  chain_val = p.read_all_as_string();
  I(chain_val.size() == constants::sha1_digest_length);

  return chain_val;
}

std::string
chained_hmac::process(string_queue const & str, size_t pos, size_t n)
{
  I(active);

  I(pos < str.size());
  if (n == std::string::npos)
    n = str.size() - pos;

  I(pos + n <= str.size());

  Botan::Pipe p(new Botan::MAC_Filter("HMAC(SHA-160)", key,
                                      constants::sha1_digest_length));
  p.start_msg();
  p.write(chain_val);
  p.write(reinterpret_cast<Botan::byte const *>(str.front_pointer(n) + pos), n);
	  
  p.end_msg();

  chain_val = p.read_all_as_string();
  I(chain_val.size() == constants::sha1_digest_length);

  return chain_val;
}
