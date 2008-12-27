/*************************************************
* X.509 Certificate Authority Source File        *
* (C) 1999-2008 Jack Lloyd                       *
*************************************************/

#include <botan/x509_ca.h>
#include <botan/x509stor.h>
#include <botan/der_enc.h>
#include <botan/ber_dec.h>
#include <botan/libstate.h>
#include <botan/lookup.h>
#include <botan/look_pk.h>
#include <botan/numthry.h>
#include <botan/parsing.h>
#include <botan/oids.h>
#include <botan/util.h>
#include <algorithm>
#include <typeinfo>
#include <iterator>
#include <memory>
#include <set>

namespace Botan {

/*************************************************
* Load the certificate and private key           *
*************************************************/
X509_CA::X509_CA(const X509_Certificate& c,
                 const Private_Key& key) : cert(c)
   {
   const Private_Key* key_pointer = &key;
   if(!dynamic_cast<const PK_Signing_Key*>(key_pointer))
      throw Invalid_Argument("X509_CA: " + key.algo_name() + " cannot sign");

   if(!cert.is_CA_cert())
      throw Invalid_Argument("X509_CA: This certificate is not for a CA");

   signer = choose_sig_format(key, ca_sig_algo);
   }

/*************************************************
* Sign a PKCS #10 certificate request            *
*************************************************/
X509_Certificate X509_CA::sign_request(const PKCS10_Request& req,
                                       RandomNumberGenerator& rng,
                                       const X509_Time& not_before,
                                       const X509_Time& not_after)
   {
   Key_Constraints constraints;
   if(req.is_CA())
      constraints = Key_Constraints(KEY_CERT_SIGN | CRL_SIGN);
   else
      {
      std::auto_ptr<Public_Key> key(req.subject_public_key());
      constraints = X509::find_constraints(*key, req.constraints());
      }

   Extensions extensions;

   extensions.add(new Cert_Extension::Authority_Key_ID(cert.subject_key_id()));
   extensions.add(new Cert_Extension::Subject_Key_ID(req.raw_public_key()));

   extensions.add(
      new Cert_Extension::Basic_Constraints(req.is_CA(), req.path_limit()));

   extensions.add(new Cert_Extension::Key_Usage(constraints));
   extensions.add(
      new Cert_Extension::Extended_Key_Usage(req.ex_constraints()));

   extensions.add(
      new Cert_Extension::Subject_Alternative_Name(req.subject_alt_name()));

   return make_cert(signer, rng, ca_sig_algo, req.raw_public_key(),
                    not_before, not_after,
                    cert.subject_dn(), req.subject_dn(),
                    extensions);
   }

/*************************************************
* Create a new certificate                       *
*************************************************/
X509_Certificate X509_CA::make_cert(PK_Signer* signer,
                                    RandomNumberGenerator& rng,
                                    const AlgorithmIdentifier& sig_algo,
                                    const MemoryRegion<byte>& pub_key,
                                    const X509_Time& not_before,
                                    const X509_Time& not_after,
                                    const X509_DN& issuer_dn,
                                    const X509_DN& subject_dn,
                                    const Extensions& extensions)
   {
   const u32bit X509_CERT_VERSION = 3;
   const u32bit SERIAL_BITS = 128;

   BigInt serial_no(rng, SERIAL_BITS);

   DataSource_Memory source(X509_Object::make_signed(signer, rng, sig_algo,
         DER_Encoder().start_cons(SEQUENCE)
            .start_explicit(0)
               .encode(X509_CERT_VERSION-1)
            .end_explicit()

            .encode(serial_no)

            .encode(sig_algo)
            .encode(issuer_dn)

            .start_cons(SEQUENCE)
               .encode(not_before)
               .encode(not_after)
            .end_cons()

            .encode(subject_dn)
            .raw_bytes(pub_key)

            .start_explicit(3)
               .start_cons(SEQUENCE)
                  .encode(extensions)
                .end_cons()
            .end_explicit()
         .end_cons()
      .get_contents()
   ));

   return X509_Certificate(source);
   }

/*************************************************
* Create a new, empty CRL                        *
*************************************************/
X509_CRL X509_CA::new_crl(RandomNumberGenerator& rng,
                          u32bit next_update) const
   {
   std::vector<CRL_Entry> empty;
   return make_crl(empty, 1, next_update, rng);
   }

/*************************************************
* Update a CRL with new entries                  *
*************************************************/
X509_CRL X509_CA::update_crl(const X509_CRL& crl,
                             const std::vector<CRL_Entry>& new_revoked,
                             RandomNumberGenerator& rng,
                             u32bit next_update) const
   {
   std::vector<CRL_Entry> already_revoked = crl.get_revoked();
   std::vector<CRL_Entry> all_revoked;

   X509_Store store;
   store.add_cert(cert, true);
   if(store.add_crl(crl) != VERIFIED)
      throw Invalid_Argument("X509_CA::update_crl: Invalid CRL provided");

   std::set<SecureVector<byte> > removed_from_crl;
   for(u32bit j = 0; j != new_revoked.size(); ++j)
      {
      if(new_revoked[j].reason_code() == DELETE_CRL_ENTRY)
         removed_from_crl.insert(new_revoked[j].serial_number());
      else
         all_revoked.push_back(new_revoked[j]);
      }

   for(u32bit j = 0; j != already_revoked.size(); ++j)
      {
      std::set<SecureVector<byte> >::const_iterator i;
      i = removed_from_crl.find(already_revoked[j].serial_number());

      if(i == removed_from_crl.end())
         all_revoked.push_back(already_revoked[j]);
      }
   std::sort(all_revoked.begin(), all_revoked.end());

   std::vector<CRL_Entry> cert_list;
   std::unique_copy(all_revoked.begin(), all_revoked.end(),
                    std::back_inserter(cert_list));

   return make_crl(cert_list, crl.crl_number() + 1, next_update, rng);
   }

/*************************************************
* Create a CRL                                   *
*************************************************/
X509_CRL X509_CA::make_crl(const std::vector<CRL_Entry>& revoked,
                           u32bit crl_number, u32bit next_update,
                           RandomNumberGenerator& rng) const
   {
   const u32bit X509_CRL_VERSION = 2;

   if(next_update == 0)
      next_update = timespec_to_u32bit(
         global_state().option("x509/crl/next_update"));

   // Totally stupid: ties encoding logic to the return of std::time!!
   const u64bit current_time = system_time();

   Extensions extensions;
   extensions.add(
      new Cert_Extension::Authority_Key_ID(cert.subject_key_id()));
   extensions.add(new Cert_Extension::CRL_Number(crl_number));

   DataSource_Memory source(X509_Object::make_signed(signer, rng, ca_sig_algo,
         DER_Encoder().start_cons(SEQUENCE)
            .encode(X509_CRL_VERSION-1)
            .encode(ca_sig_algo)
            .encode(cert.issuer_dn())
            .encode(X509_Time(current_time))
            .encode(X509_Time(current_time + next_update))
            .encode_if(revoked.size() > 0,
                 DER_Encoder()
                    .start_cons(SEQUENCE)
                       .encode_list(revoked)
                    .end_cons()
               )
            .start_explicit(0)
               .start_cons(SEQUENCE)
                  .encode(extensions)
               .end_cons()
            .end_explicit()
         .end_cons()
      .get_contents()
   ));

   return X509_CRL(source);
   }

/*************************************************
* Return the CA's certificate                    *
*************************************************/
X509_Certificate X509_CA::ca_certificate() const
   {
   return cert;
   }

/*************************************************
* X509_CA Destructor                             *
*************************************************/
X509_CA::~X509_CA()
   {
   delete signer;
   }

/*************************************************
* Choose a signing format for the key            *
*************************************************/
PK_Signer* choose_sig_format(const Private_Key& key,
                             AlgorithmIdentifier& sig_algo)
   {
   std::string padding;
   Signature_Format format;

   const std::string algo_name = key.algo_name();

   if(algo_name == "RSA")
      {
      std::string hash = global_state().option("x509/ca/rsa_hash");

      if(hash == "")
         throw Invalid_State("No value set for x509/ca/rsa_hash");

      hash = global_state().deref_alias(hash);

      padding = "EMSA3(" + hash + ")";
      format = IEEE_1363;
      }
   else if(algo_name == "DSA")
      {
      std::string hash = global_state().deref_alias("SHA-1");
      padding = "EMSA1(" + hash + ")";
      format = DER_SEQUENCE;
      }
   else
      throw Invalid_Argument("Unknown X.509 signing key type: " + algo_name);

   sig_algo.oid = OIDS::lookup(algo_name + "/" + padding);

   std::auto_ptr<X509_Encoder> encoding(key.x509_encoder());
   if(!encoding.get())
      throw Encoding_Error("Key " + algo_name + " does not support "
                           "X.509 encoding");

   sig_algo.parameters = encoding->alg_id().parameters;

   const PK_Signing_Key& sig_key = dynamic_cast<const PK_Signing_Key&>(key);

   return get_pk_signer(sig_key, padding, format);
   }

}
