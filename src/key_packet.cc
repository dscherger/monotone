// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.
//
// This duplicates part of packet.cc; the intent is to deprecate that but
// keep this.

#include "base.hh"
#include <sstream>
#include <botan/botan.h>
#include <botan/rsa.h>

#include "cset.hh"
#include "constants.hh"
#include "key_packet.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "cert.hh"
#include "key_store.hh" // for keypair
#include "char_classifiers.hh"
#include "lazy_rng.hh"

using std::istream;
using std::istringstream;
using std::make_pair;
using std::map;
using std::ostream;
using std::pair;
using std::string;

using boost::shared_ptr;

// --- key_packet writer ---

key_packet_writer::key_packet_writer(ostream & o) : ost(o) {}

void
key_packet_writer::consume_public_key(key_name const & ident,
                                      rsa_pub_key const & k)
{
  ost << "[pubkey " << ident() << "]\n"
      << trim(encode_base64(k)()) << '\n'
      << "[end]\n";
}

void
key_packet_writer::consume_key_pair(key_name const & ident,
                                    keypair const & kp)
{
  ost << "[keypair " << ident() << "]\n"
      << trim(encode_base64(kp.pub)()) << "#\n"
      << trim(encode_base64(kp.priv)()) << '\n'
      << "[end]\n";
}

void
key_packet_writer::consume_old_private_key(key_name const & ident,
                                           old_arc4_rsa_priv_key const & k)
{
  ost << "[privkey " << ident() << "]\n"
      << trim(encode_base64(k)()) << '\n'
      << "[end]\n";
}


// --- reading key_packets from streams ---
namespace
{
  struct
  feed_key_packet_consumer : public origin_aware
  {
    size_t & count;
    key_packet_consumer & cons;
    feed_key_packet_consumer(size_t & count, key_packet_consumer & c,
                             origin::type whence)
      : origin_aware(whence), count(count), cons(c)
    {}
    void validate_base64(string const & s) const
    {
      E(!s.empty()
        && s.find_first_not_of(constants::legal_base64_bytes) == string::npos,
        made_from,
        F("malformed key_packet: invalid base64 block"));
    }
    void validate_arg_base64(string const & s) const
    {
      E(s.find_first_not_of(constants::legal_base64_bytes) == string::npos,
        made_from,
        F("malformed key_packet: invalid base64 block"));
    }
    void validate_key(string const & k) const
    {
      E(!k.empty()
        && k.find_first_not_of(constants::legal_key_name_bytes) == string::npos,
        made_from,
        F("malformed key_packet: invalid key name"));
    }
    void validate_public_key_data(string const & name, string const & keydata) const
    {
      string decoded = decode_base64_as<string>(keydata, origin::user);
      Botan::SecureVector<Botan::byte> key_block
        (reinterpret_cast<Botan::byte const *>(decoded.c_str()), decoded.size());
      try
        {
          Botan::X509::load_key(key_block);
        }
      catch (Botan::Decoding_Error const & e)
        {
          E(false, origin::user,
            F("malformed key_packet: invalid public key data for '%s': %s")
              % name % e.what());
        }
    }
    void validate_private_key_data(string const & name, string const & keydata) const
    {
      string decoded = decode_base64_as<string>(keydata, origin::user);
      Botan::DataSource_Memory ds(decoded);
      try
        {
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
          Botan::PKCS8::load_key(ds, lazy_rng::get(), string());
#else
          Botan::PKCS8::load_key(ds, string());
#endif
        }
      catch (Botan::Decoding_Error const & e)
        {
          E(false, origin::user,
            F("malformed key_packet: invalid private key data for '%s': %s")
              % name % e.what());
        }
      // since we do not want to prompt for a password to decode it finally,
      // we ignore all other exceptions
      catch (Botan::Invalid_Argument) {}
    }
    void validate_no_more_args(istringstream & iss) const
    {
      string next;
      iss >> next;
      E(next.empty(), made_from,
        F("malformed key_packet: too many arguments in header"));
    }

    static void read_rest(istream& in, string& dest)
    {

      while (true)
        {
          string t;
          in >> t;
          if (t.empty()) break;
          dest += t;
        }
    }
    void pubkey_packet(string const & args, string const & body) const
    {
      L(FL("read pubkey key_packet"));
      validate_key(args);
      validate_base64(body);
      validate_public_key_data(args, body);

      cons.consume_public_key(key_name(args, made_from),
                              decode_base64_as<rsa_pub_key>(body, made_from));
    }

    void keypair_packet(string const & args, string const & body) const
    {
      L(FL("read keypair key_packet"));
      string::size_type hashpos = body.find('#');
      string pub(body, 0, hashpos);
      string priv(body, hashpos+1);

      validate_key(args);
      validate_base64(pub);
      validate_public_key_data(args, pub);
      validate_base64(priv);
      validate_private_key_data(args, priv);

      cons.consume_key_pair(key_name(args, made_from),
                            keypair(decode_base64_as<rsa_pub_key>(pub, made_from),
                                    decode_base64_as<rsa_priv_key>(priv, made_from)));
    }

    void privkey_packet(string const & args, string const & body) const
    {
      L(FL("read privkey key_packet"));
      validate_key(args);
      validate_base64(body);
      cons.consume_old_private_key(key_name(args, made_from),
                                   decode_base64_as<old_arc4_rsa_priv_key>(body, made_from));
    }

    void operator()(string const & type,
                    string const & args,
                    string const & body) const
    {
      if (type == "pubkey")
        pubkey_packet(args, body);
      else if (type == "keypair")
        keypair_packet(args, body);
      else if (type == "privkey")
        privkey_packet(args, body);
      else
        {
          W(F("unknown key_packet type: '%s'") % type);
          return;
        }
      ++count;
    }
  };
} // anonymous namespace

static size_t
extract_key_packets(string const & s, key_packet_consumer & cons)
{
  size_t count = 0;
  feed_key_packet_consumer feeder(count, cons, origin::user);

  string::const_iterator p, tbeg, tend, abeg, aend, bbeg, bend;

  enum extract_state {
    skipping, open_bracket, scanning_type, found_type,
    scanning_args, found_args, scanning_body,
    end_1, end_2, end_3, end_4, end_5
  } state = skipping;

  for (p = s.begin(); p != s.end(); p++)
    switch (state)
      {
      case skipping: if (*p == '[') state = open_bracket; break;
      case open_bracket:
        if (is_alpha (*p))
          state = scanning_type;
        else
          state = skipping;
        tbeg = p;
        break;
      case scanning_type:
        if (!is_alpha (*p))
          {
            state = is_space(*p) ? found_type : skipping;
            tend = p;
          }
        break;
      case found_type:
        if (!is_space (*p))
          {
            state = (*p != ']') ? scanning_args : skipping;
            abeg = p;
          }
        break;
      case scanning_args:
        if (*p == ']')
          {
            state = found_args;
            aend = p;
          }
        break;
      case found_args:
        state = (*p != '[' && *p != ']') ? scanning_body : skipping;
        bbeg = p;
        break;
      case scanning_body:
        if (*p == '[')
          {
            state = end_1;
            bend = p;
          }
        else if (*p == ']')
          state = skipping;
        break;

      case end_1: state = (*p == 'e') ? end_2 : skipping; break;
      case end_2: state = (*p == 'n') ? end_3 : skipping; break;
      case end_3: state = (*p == 'd') ? end_4 : skipping; break;
      case end_4:
        if (*p == ']')
          feeder(string(tbeg, tend), string(abeg, aend), string(bbeg, bend));
        state = skipping;
        break;
      default:
        I(false);
      }
  return count;
}

// this is same as rfind, but search area is haystack[start:] (from start to end of string)
// haystack is searched, needle is pattern
static size_t
rfind_in_substr(std::string const& haystack, size_t start, std::string const& needle)
{
  I(start <= haystack.size());
  const std::string::const_iterator result =
    std::find_end(haystack.begin() + start, haystack.end(),
                  needle.begin(), needle.end());

  if (result == haystack.end())
    return std::string::npos;
  else
    return distance(haystack.begin(), result);
}

size_t
read_key_packets(istream & in, key_packet_consumer & cons)
{
  string accum, tmp;
  size_t count = 0;
  size_t const bufsz = 0xff;
  char buf[bufsz];
  static string const end("[end]");
  while(in)
    {
      size_t const next_search_pos = (accum.size() >= end.size())
                                      ? accum.size() - end.size() : 0;
      in.read(buf, bufsz);
      accum.append(buf, in.gcount());
      string::size_type endpos = string::npos;
      endpos = rfind_in_substr(accum, next_search_pos, end);
      if (endpos != string::npos)
        {
          endpos += end.size();
          string tmp = accum.substr(0, endpos);
          count += extract_key_packets(tmp, cons);
          if (endpos < accum.size() - 1)
            accum = accum.substr(endpos+1);
          else
            accum.clear();
        }
    }
  return count;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
