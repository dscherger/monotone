/*************************************************
* X.509 Certificate Authority Header File        *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_X509_CA_H__
#define BOTAN_X509_CA_H__

#include <botan/x509cert.h>
#include <botan/x509_crl.h>
#include <botan/x509_ext.h>
#include <botan/pkcs8.h>
#include <botan/pkcs10.h>
#include <botan/pubkey.h>

namespace Botan {

/*************************************************
* X.509 Certificate Authority                    *
*************************************************/
class X509_CA
   {
   public:
      X509_Certificate sign_request(const PKCS10_Request&, u32bit = 0) const;

      X509_Certificate ca_certificate() const;

      X509_CRL new_crl(u32bit = 0) const;
      X509_CRL update_crl(const X509_CRL&, const std::vector<CRL_Entry>&,
                          u32bit = 0) const;

      static X509_Certificate make_cert(PK_Signer*, const AlgorithmIdentifier&,
                                        const MemoryRegion<byte>&,
                                        const X509_Time&, const X509_Time&,
                                        const X509_DN&, const X509_DN&,
                                        const Extensions&);

      X509_CA(const X509_Certificate&, const Private_Key&);
      ~X509_CA();
   private:
      X509_CA(const X509_CA&) {}
      X509_CA& operator=(const X509_CA&) { return (*this); }

      X509_CRL make_crl(const std::vector<CRL_Entry>&, u32bit, u32bit) const;

      AlgorithmIdentifier ca_sig_algo;
      X509_Certificate cert;
      PK_Signer* signer;
   };

/*************************************************
* Choose a signing format for the key            *
*************************************************/
PK_Signer* choose_sig_format(const Private_Key&, AlgorithmIdentifier&);


}

#endif
