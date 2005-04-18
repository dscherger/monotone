/*************************************************
* X.509 CRL Header File                          *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_X509_CRL_H__
#define BOTAN_X509_CRL_H__

#include <botan/x509_obj.h>
#include <botan/crl_ent.h>
#include <vector>

namespace Botan {

/*************************************************
* X.509 CRL                                      *
*************************************************/
class X509_CRL : public X509_Object
   {
   public:
      struct X509_CRL_Error : public Exception
         {
         X509_CRL_Error(const std::string& error) :
            Exception("X509_CRL: " + error) {}
         };

      std::vector<CRL_Entry> get_revoked() const;

      X509_DN issuer_dn() const;
      MemoryVector<byte> authority_key_id() const;

      u32bit crl_number() const;
      X509_Time this_update() const;
      X509_Time next_update() const;

      void force_decode();

      X509_CRL(DataSource&);
      X509_CRL(const std::string&);
   private:
      void handle_crl_extension(const Extension&);
      std::vector<CRL_Entry> revoked;
      MemoryVector<byte> issuer_key_id;
      X509_Time start, end;
      X509_DN issuer;
      u32bit version, crl_count;
   };

}

#endif
