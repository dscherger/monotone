// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>
#include <botan/botan.h>
#include <botan/rsa.h>

#include "cset.hh"
#include "constants.hh"
#include "packet.hh"
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

// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void
packet_writer::consume_file_data(file_id const & ident,
                                 file_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[fdata " << ident << "]\n"
      << trim(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_file_delta(file_id const & old_id,
                                  file_id const & new_id,
                                  file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[fdelta " << old_id << '\n'
      << "        " << new_id << "]\n"
      << trim(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_revision_data(revision_id const & ident,
                                     revision_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[rdata " << ident << "]\n"
      << trim(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_revision_cert(cert const & t)
{
  ost << "[rcert " << encode_hexenc(t.ident.inner()(),
                                    t.ident.inner().made_from) << '\n'
      << "       " << t.name() << '\n'
      << "       " << t.key.inner() << '\n'
      << "       " << trim(encode_base64(t.value)()) << "]\n"
      << trim(encode_base64(t.sig)()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_public_key(key_name const & ident,
                                  rsa_pub_key const & k)
{
  ost << "[pubkey " << ident() << "]\n"
      << trim(encode_base64(k)()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_key_pair(key_name const & ident,
                                keypair const & kp)
{
  ost << "[keypair " << ident() << "]\n"
      << trim(encode_base64(kp.pub)()) << "#\n"
      << trim(encode_base64(kp.priv)()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_old_private_key(key_name const & ident,
                                       old_arc4_rsa_priv_key const & k)
{
  ost << "[privkey " << ident() << "]\n"
      << trim(encode_base64(k)()) << '\n'
      << "[end]\n";
}


// --- reading packets from streams ---
namespace
{
  struct
  feed_packet_consumer : public origin_aware
  {
    size_t & count;
    packet_consumer & cons;
    feed_packet_consumer(size_t & count, packet_consumer & c,
                         origin::type whence)
      : origin_aware(whence), count(count), cons(c)
    {}
    void validate_id(string const & id) const
    {
      E(id.size() == constants::idlen
        && id.find_first_not_of(constants::legal_id_bytes) == string::npos,
        made_from,
        F("malformed packet: invalid identifier"));
    }
    void validate_base64(string const & s) const
    {
      E(!s.empty()
        && s.find_first_not_of(constants::legal_base64_bytes) == string::npos,
        made_from,
        F("malformed packet: invalid base64 block"));
    }
    void validate_arg_base64(string const & s) const
    {
      E(s.find_first_not_of(constants::legal_base64_bytes) == string::npos,
        made_from,
        F("malformed packet: invalid base64 block"));
    }
    void validate_key(string const & k) const
    {
      E(!k.empty()
        && k.find_first_not_of(constants::legal_key_name_bytes) == string::npos,
        made_from,
        F("malformed packet: invalid key name"));
    }
    void validate_public_key_data(string const & name, string const & keydata) const
    {
      string decoded = decode_base64_as<string>(keydata, origin::user);
      Botan::SecureVector<Botan::byte> key_block;
      key_block.set(reinterpret_cast<Botan::byte const *>(decoded.c_str()), decoded.size());
      try
        {
          Botan::X509::load_key(key_block);
        }
      catch (Botan::Decoding_Error const & e)
        {
          E(false, origin::user,
            F("malformed packet: invalid public key data for '%s': %s")
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
            F("malformed packet: invalid private key data for '%s': %s")
              % name % e.what());
        }
      // since we do not want to prompt for a password to decode it finally,
      // we ignore all other exceptions
      catch (Botan::Invalid_Argument) {}
    }
    void validate_certname(string const & cn) const
    {
      E(!cn.empty()
        && cn.find_first_not_of(constants::legal_cert_name_bytes) == string::npos,
        made_from,
        F("malformed packet: invalid cert name"));
    }
    void validate_no_more_args(istringstream & iss) const
    {
      string next;
      iss >> next;
      E(next.empty(), made_from,
        F("malformed packet: too many arguments in header"));
    }

    void data_packet(string const & args, string const & body,
                     bool is_revision) const
    {
      L(FL("read %s data packet") % (is_revision ? "revision" : "file"));
      validate_id(args);
      validate_base64(body);

      id hash(decode_hexenc_as<id>(args, made_from));
      data contents;
      unpack(base64<gzip<data> >(body, made_from), contents);
      if (is_revision)
        cons.consume_revision_data(revision_id(hash),
                                   revision_data(contents));
      else
        cons.consume_file_data(file_id(hash),
                               file_data(contents));
    }

    void fdelta_packet(string const & args, string const & body) const
    {
      L(FL("read delta packet"));
      istringstream iss(args);
      string src_id; iss >> src_id; validate_id(src_id);
      string dst_id; iss >> dst_id; validate_id(dst_id);
      validate_no_more_args(iss);
      validate_base64(body);

      id src_hash(decode_hexenc_as<id>(src_id, made_from)),
        dst_hash(decode_hexenc_as<id>(dst_id, made_from));
      delta contents;
      unpack(base64<gzip<delta> >(body, made_from), contents);
      cons.consume_file_delta(file_id(src_hash),
                              file_id(dst_hash),
                              file_delta(contents));
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
    void rcert_packet(string const & args, string const & body) const
    {
      L(FL("read cert packet"));
      istringstream iss(args);
      string certid; iss >> certid; validate_id(certid);
      string name;   iss >> name;   validate_certname(name);
      string keyid;  iss >> keyid;  validate_id(keyid);
      string val;
      read_rest(iss,val);           validate_arg_base64(val);

      revision_id hash(decode_hexenc_as<revision_id>(certid, made_from));
      validate_base64(body);

      // canonicalize the base64 encodings to permit searches
      cert t = cert(hash,
                    cert_name(name, made_from),
                    decode_base64_as<cert_value>(val, made_from),
                    decode_hexenc_as<key_id>(keyid, made_from),
                    decode_base64_as<rsa_sha1_signature>(body, made_from));
      cons.consume_revision_cert(t);
    }

    void pubkey_packet(string const & args, string const & body) const
    {
      L(FL("read pubkey packet"));
      validate_key(args);
      validate_base64(body);
      validate_public_key_data(args, body);

      cons.consume_public_key(key_name(args, made_from),
                              decode_base64_as<rsa_pub_key>(body, made_from));
    }

    void keypair_packet(string const & args, string const & body) const
    {
      L(FL("read keypair packet"));
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
      L(FL("read privkey packet"));
      validate_key(args);
      validate_base64(body);
      cons.consume_old_private_key(key_name(args, made_from),
                                   decode_base64_as<old_arc4_rsa_priv_key>(body, made_from));
    }

    void operator()(string const & type,
                    string const & args,
                    string const & body) const
    {
      if (type == "rdata")
        data_packet(args, body, true);
      else if (type == "fdata")
        data_packet(args, body, false);
      else if (type == "fdelta")
        fdelta_packet(args, body);
      else if (type == "rcert")
        rcert_packet(args, body);
      else if (type == "pubkey")
        pubkey_packet(args, body);
      else if (type == "keypair")
        keypair_packet(args, body);
      else if (type == "privkey")
        privkey_packet(args, body);
      else
        {
          W(F("unknown packet type: '%s'") % type);
          return;
        }
      ++count;
    }
  };
} // anonymous namespace

static size_t
extract_packets(string const & s, packet_consumer & cons)
{
  size_t count = 0;
  feed_packet_consumer feeder(count, cons, origin::user);

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
read_packets(istream & in, packet_consumer & cons)
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
          count += extract_packets(tmp, cons);
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
