// Copyright (C) 2010 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/transforms.hh"
#include "../../../src/vocab_cast.hh"
#include "../../../src/xdelta.hh"

#include "../../../src/key_packet.cc"
#include <iostream>
using std::ostringstream;

UNIT_TEST(validators)
{
  ostringstream oss;
  key_packet_writer pw(oss);
  size_t count;
  feed_key_packet_consumer f(count, pw, origin::user);

#define N_THROW(expr) UNIT_TEST_CHECK_NOT_THROW(expr, recoverable_failure)
#define Y_THROW(expr) UNIT_TEST_CHECK_THROW(expr, recoverable_failure)

  // validate_base64
  N_THROW(f.validate_base64("YmwK"));
  N_THROW(f.validate_base64(" Y m x h a A o = "));
  N_THROW(f.validate_base64("ABCD EFGH IJKL MNOP QRST UVWX YZ"
                            "abcd efgh ijkl mnop qrst uvwx yz"
                            "0123 4567 89/+ z\t=\r=\n="));

  Y_THROW(f.validate_base64(""));
  Y_THROW(f.validate_base64("!@#$"));

  // validate_key
  N_THROW(f.validate_key("graydon@venge.net"));
  N_THROW(f.validate_key("dscherger+mtn"));
  N_THROW(f.validate_key("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz"
                         "0123456789-.@+_"));
  Y_THROW(f.validate_key(""));
  Y_THROW(f.validate_key("graydon at venge dot net"));

  // validate_public_key_data
  N_THROW(f.validate_public_key_data(
    "test@lala.com",
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDS8J8cI0a"
    "Ab1Pd55UE0vlxHHBS9ZyDKGQXTf3dA+ywGeXfKYjBCAYgcZ"
    "obRxVSziKZ17SfYFSOa0HvMAXykpHc+Uy3SHHnFSJb+wFYp"
    "JdUrxecZMpzhySCR49lw8aFoGmpsZZmNiherpuP2CzLDCax"
    "IK1dbTgilMd0dfoy277M9QIDAQAB"
  ));
  // this is a private key
  Y_THROW(f.validate_public_key_data(
    "invalid0",
    "LS0tLS1CRUdJTiBQUklWQVRFIEtFWS0tLS0tCk1JSUNkUUl"
    "CQURBTkJna3Foa2lHOXcwQkFRRUZBQVNDQWw4d2dnSmJBZ0"
    "VBQW9HQkFOTHdueHdqUm9CdlU5M24KbFFUUytYRWNjRkwxb"
    "klNb1pCZE4vZDBEN0xBWjVkOHBpTUVJQmlCeG1odEhGVkxP"
    "SXBuWHRKOWdWSTVyUWU4dwpCZktTa2R6NVRMZEljZWNWSWx"
    "2N0FWaWtsMVN2RjV4a3luT0hKSUpIajJYRHhvV2dhYW14bG"
    "1ZMktGNnVtNC9ZCkxNc01KckVnclYxdE9DS1V4M1IxK2pMY"
    "nZzejFBZ01CQUFFQ2dZQUFsTlZyYm91SU15bm9IMTZURW43"
    "NUlzeVkKd0U3K0tVRDN2VURpRGNRQytuYi9uak81bGZUYWc"
    "3M3Yva1d1Tjc3YmpxZCtQQkpLUWNFTlV0ejMyaE45elBWSQ"
    "p5SzFRa1E4MmRlNHRCYlY4dFlDbmdXSFB3VWwxOHRrcFpzU"
    "HJpd3E1MUpWOC9SdTdUanpRZDNHLzExQVdxcnFpCm9mMGtI"
    "bC9PODBKbDNRZWJ3UUpCQU9pcEc1RlkzY1hOY0QwTjRiWjl"
    "YMjZ6WWpNQWlBTG5WbktGcGpGblFqTUkKcVhCRitraWI2SU"
    "11ZnZaRm1nT09LWG9vdzlyY3EyY2RwRlJ3bFVWQXdoRUNRU"
    "URvR2JZNXhDNFoxMEVuQjErVAp4dGx5SEZzQW9LMXY3eGtG"
    "c3RZV3hacXJUZ1hNemVkdkxiU2dHZ1lzMFNrZnlyQVFtREQ"
    "yNGpjL25SOW0yNG0zCnJqaWxBa0JFZDI5cmFIRnJBamZqWD"
    "dCcW1aNTUzMFFvcWlGY2FXT2hNLzlpVG5iR3VlZlM2R1RzO"
    "VNTSlppZHEKcGJUYkV2elZ2Q1ZXeE5XVDlMOGxNalJiT3VG"
    "aEFrQUZJcHgvaHJHbWJMYktVRVZ6RlpFMkR4Nk1Vd0hEV2p"
    "6cApmVjF6UDRmK2hrbG1rSit3UEFpbENpNWN5M3ZuY2lxWE"
    "UyYng3MnRkZ3ZKdzZpYVA0OURwQWtCTFlWZ3NaNHErCkxkL"
    "0VYWFJibTJGOEd6MjVCaTFNV0p5OWxQOXBoY2FPaDdpZlBh"
    "bVZDeTRlUGx4aTU3Wi9aTFByaC8wL2pzb3YKbExSTFdGVE8"
    "2aldLCi0tLS0tRU5EIFBSSVZBVEUgS0VZLS0tLS0K"
  ));
  Y_THROW(f.validate_public_key_data("invalid1", "invalid"));
  // the following are both valid base64
  Y_THROW(f.validate_public_key_data("invalid2", "YmwK"));
  Y_THROW(f.validate_public_key_data("invalid3", "Y m x h a A o = "));

  // validate_private_key_data
  N_THROW(f.validate_private_key_data(
    "test@lala.com",
    "LS0tLS1CRUdJTiBQUklWQVRFIEtFWS0tLS0tCk1JSUNkUUl"
    "CQURBTkJna3Foa2lHOXcwQkFRRUZBQVNDQWw4d2dnSmJBZ0"
    "VBQW9HQkFOTHdueHdqUm9CdlU5M24KbFFUUytYRWNjRkwxb"
    "klNb1pCZE4vZDBEN0xBWjVkOHBpTUVJQmlCeG1odEhGVkxP"
    "SXBuWHRKOWdWSTVyUWU4dwpCZktTa2R6NVRMZEljZWNWSWx"
    "2N0FWaWtsMVN2RjV4a3luT0hKSUpIajJYRHhvV2dhYW14bG"
    "1ZMktGNnVtNC9ZCkxNc01KckVnclYxdE9DS1V4M1IxK2pMY"
    "nZzejFBZ01CQUFFQ2dZQUFsTlZyYm91SU15bm9IMTZURW43"
    "NUlzeVkKd0U3K0tVRDN2VURpRGNRQytuYi9uak81bGZUYWc"
    "3M3Yva1d1Tjc3YmpxZCtQQkpLUWNFTlV0ejMyaE45elBWSQ"
    "p5SzFRa1E4MmRlNHRCYlY4dFlDbmdXSFB3VWwxOHRrcFpzU"
    "HJpd3E1MUpWOC9SdTdUanpRZDNHLzExQVdxcnFpCm9mMGtI"
    "bC9PODBKbDNRZWJ3UUpCQU9pcEc1RlkzY1hOY0QwTjRiWjl"
    "YMjZ6WWpNQWlBTG5WbktGcGpGblFqTUkKcVhCRitraWI2SU"
    "11ZnZaRm1nT09LWG9vdzlyY3EyY2RwRlJ3bFVWQXdoRUNRU"
    "URvR2JZNXhDNFoxMEVuQjErVAp4dGx5SEZzQW9LMXY3eGtG"
    "c3RZV3hacXJUZ1hNemVkdkxiU2dHZ1lzMFNrZnlyQVFtREQ"
    "yNGpjL25SOW0yNG0zCnJqaWxBa0JFZDI5cmFIRnJBamZqWD"
    "dCcW1aNTUzMFFvcWlGY2FXT2hNLzlpVG5iR3VlZlM2R1RzO"
    "VNTSlppZHEKcGJUYkV2elZ2Q1ZXeE5XVDlMOGxNalJiT3VG"
    "aEFrQUZJcHgvaHJHbWJMYktVRVZ6RlpFMkR4Nk1Vd0hEV2p"
    "6cApmVjF6UDRmK2hrbG1rSit3UEFpbENpNWN5M3ZuY2lxWE"
    "UyYng3MnRkZ3ZKdzZpYVA0OURwQWtCTFlWZ3NaNHErCkxkL"
    "0VYWFJibTJGOEd6MjVCaTFNV0p5OWxQOXBoY2FPaDdpZlBh"
    "bVZDeTRlUGx4aTU3Wi9aTFByaC8wL2pzb3YKbExSTFdGVE8"
    "2aldLCi0tLS0tRU5EIFBSSVZBVEUgS0VZLS0tLS0K"
  ));
  // this is a public key
  Y_THROW(f.validate_private_key_data(
    "invalid0",
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDS8J8cI0a"
    "Ab1Pd55UE0vlxHHBS9ZyDKGQXTf3dA+ywGeXfKYjBCAYgcZ"
    "obRxVSziKZ17SfYFSOa0HvMAXykpHc+Uy3SHHnFSJb+wFYp"
    "JdUrxecZMpzhySCR49lw8aFoGmpsZZmNiherpuP2CzLDCax"
    "IK1dbTgilMd0dfoy277M9QIDAQAB"
  ));
  Y_THROW(f.validate_private_key_data("invalid1", "invalid"));
  // the following are both valid base64
  Y_THROW(f.validate_private_key_data("invalid2", "YmwK"));
  Y_THROW(f.validate_private_key_data("invalid3", "Y m x h a A o = "));


  // validate_no_more_args
  {
    istringstream iss("a b");
    string a; iss >> a; UNIT_TEST_CHECK(a == "a");
    string b; iss >> b; UNIT_TEST_CHECK(b == "b");
    N_THROW(f.validate_no_more_args(iss));
  }
  {
    istringstream iss("a ");
    string a; iss >> a; UNIT_TEST_CHECK(a == "a");
    N_THROW(f.validate_no_more_args(iss));
  }
  {
    istringstream iss("a b");
    string a; iss >> a; UNIT_TEST_CHECK(a == "a");
    Y_THROW(f.validate_no_more_args(iss));
  }
}

UNIT_TEST(roundabout)
{
  string tmp;

  {
    ostringstream oss;
    key_packet_writer pw(oss);

    keypair kp;
    // a public key packet
    kp.pub = rsa_pub_key(decode_base64_as<string>(
      "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDS8J8cI0a"
      "Ab1Pd55UE0vlxHHBS9ZyDKGQXTf3dA+ywGeXfKYjBCAYgcZ"
      "obRxVSziKZ17SfYFSOa0HvMAXykpHc+Uy3SHHnFSJb+wFYp"
      "JdUrxecZMpzhySCR49lw8aFoGmpsZZmNiherpuP2CzLDCax"
      "IK1dbTgilMd0dfoy277M9QIDAQAB", origin::internal
    ), origin::internal);
    pw.consume_public_key(key_name("test1@lala.com"), kp.pub);

    // a keypair packet
    kp.priv = rsa_priv_key(decode_base64_as<string>(
      "LS0tLS1CRUdJTiBQUklWQVRFIEtFWS0tLS0tCk1JSUNkUUl"
      "CQURBTkJna3Foa2lHOXcwQkFRRUZBQVNDQWw4d2dnSmJBZ0"
      "VBQW9HQkFOTHdueHdqUm9CdlU5M24KbFFUUytYRWNjRkwxb"
      "klNb1pCZE4vZDBEN0xBWjVkOHBpTUVJQmlCeG1odEhGVkxP"
      "SXBuWHRKOWdWSTVyUWU4dwpCZktTa2R6NVRMZEljZWNWSWx"
      "2N0FWaWtsMVN2RjV4a3luT0hKSUpIajJYRHhvV2dhYW14bG"
      "1ZMktGNnVtNC9ZCkxNc01KckVnclYxdE9DS1V4M1IxK2pMY"
      "nZzejFBZ01CQUFFQ2dZQUFsTlZyYm91SU15bm9IMTZURW43"
      "NUlzeVkKd0U3K0tVRDN2VURpRGNRQytuYi9uak81bGZUYWc"
      "3M3Yva1d1Tjc3YmpxZCtQQkpLUWNFTlV0ejMyaE45elBWSQ"
      "p5SzFRa1E4MmRlNHRCYlY4dFlDbmdXSFB3VWwxOHRrcFpzU"
      "HJpd3E1MUpWOC9SdTdUanpRZDNHLzExQVdxcnFpCm9mMGtI"
      "bC9PODBKbDNRZWJ3UUpCQU9pcEc1RlkzY1hOY0QwTjRiWjl"
      "YMjZ6WWpNQWlBTG5WbktGcGpGblFqTUkKcVhCRitraWI2SU"
      "11ZnZaRm1nT09LWG9vdzlyY3EyY2RwRlJ3bFVWQXdoRUNRU"
      "URvR2JZNXhDNFoxMEVuQjErVAp4dGx5SEZzQW9LMXY3eGtG"
      "c3RZV3hacXJUZ1hNemVkdkxiU2dHZ1lzMFNrZnlyQVFtREQ"
      "yNGpjL25SOW0yNG0zCnJqaWxBa0JFZDI5cmFIRnJBamZqWD"
      "dCcW1aNTUzMFFvcWlGY2FXT2hNLzlpVG5iR3VlZlM2R1RzO"
      "VNTSlppZHEKcGJUYkV2elZ2Q1ZXeE5XVDlMOGxNalJiT3VG"
      "aEFrQUZJcHgvaHJHbWJMYktVRVZ6RlpFMkR4Nk1Vd0hEV2p"
      "6cApmVjF6UDRmK2hrbG1rSit3UEFpbENpNWN5M3ZuY2lxWE"
      "UyYng3MnRkZ3ZKdzZpYVA0OURwQWtCTFlWZ3NaNHErCkxkL"
      "0VYWFJibTJGOEd6MjVCaTFNV0p5OWxQOXBoY2FPaDdpZlBh"
      "bVZDeTRlUGx4aTU3Wi9aTFByaC8wL2pzb3YKbExSTFdGVE8"
      "2aldLCi0tLS0tRU5EIFBSSVZBVEUgS0VZLS0tLS0K", origin::internal
    ), origin::internal);
    pw.consume_key_pair(key_name("test2@lala.com"), kp);

    // an old privkey packet
    old_arc4_rsa_priv_key oldpriv("and neither is this!");
    pw.consume_old_private_key(key_name("test3@lala.com"), oldpriv);

    tmp = oss.str();
  }

  for (int i = 0; i < 10; ++i)
    {
      // now spin around sending and receiving this a few times
      ostringstream oss;
      key_packet_writer pw(oss);
      istringstream iss(tmp);
      read_key_packets(iss, pw);
      UNIT_TEST_CHECK(oss.str() == tmp);
      tmp = oss.str();
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
