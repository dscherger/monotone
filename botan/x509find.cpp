/*************************************************
* X.509 Certificate Store Searching Source File  *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/x509stor.h>
#include <algorithm>
#include <memory>

namespace Botan {

namespace X509_Store_Search {

namespace {

/*************************************************
* Comparison Function Pointer                    *
*************************************************/
typedef bool (*compare_fn)(const std::string&, const std::string&);

/*************************************************
* Predicate for caseless searching               *
*************************************************/
bool caseless_cmp(char a, char b)
   {
   return (to_lower(a) == to_lower(b));
   }

/*************************************************
* Compare based on case-insensive substrings     *
*************************************************/
bool substring_match(const std::string& searching_for,
                     const std::string& found)
   {
   if(std::search(found.begin(), found.end(), searching_for.begin(),
                  searching_for.end(), caseless_cmp) != found.end())
      return true;
   return false;
   }

/*************************************************
* Compare based on case-insensive match          *
*************************************************/
bool ignore_case(const std::string& searching_for, const std::string& found)
   {
   if(searching_for.size() != found.size())
      return false;

   return std::equal(found.begin(), found.end(),
                     searching_for.begin(), caseless_cmp);
   }

/*************************************************
* Search based on the contents of a DN entry     *
*************************************************/
class DN_Check : public X509_Store::Search_Func
   {
   public:
      bool match(const X509_Certificate& cert) const
         {
         return compare(looking_for, cert.subject_info(dn_entry));
         }

      DN_Check(const std::string& entry, const std::string& target,
               compare_fn func) :
         compare(func), dn_entry(entry), looking_for(target) {}
   private:
      compare_fn compare;
      const std::string dn_entry;
      const std::string looking_for;
   };

/*************************************************
* Search based on the key id                     *
*************************************************/
class KeyID_Match : public X509_Store::Search_Func
   {
   public:
      bool match(const X509_Certificate& cert) const
         {
         std::auto_ptr<X509_PublicKey> key(cert.subject_public_key());
         return (key->key_id() == key_id);
         }
      KeyID_Match(u64bit id) : key_id(id) {}
   private:
      u64bit key_id;
   };

/*************************************************
* Search based on the issuer and serial number   *
*************************************************/
class IandS_Match : public X509_Store::Search_Func
   {
   public:
      bool match(const X509_Certificate& cert) const
         {
         if(cert.serial_number() != serial)
            return false;
         return (cert.issuer_dn() == issuer);
         }
      IandS_Match(const X509_DN& i, const MemoryRegion<byte>& s) :
         issuer(i), serial(s) {}
   private:
      X509_DN issuer;
      MemoryVector<byte> serial;
   };

/*************************************************
* Search based on the subject key id             *
*************************************************/
class SKID_Match : public X509_Store::Search_Func
   {
   public:
      bool match(const X509_Certificate& cert) const
         {
         return (cert.subject_key_id() == skid);
         }
      SKID_Match(const MemoryRegion<byte>& s) : skid(s) {}
   private:
      MemoryVector<byte> skid;
   };

}

/*************************************************
* Search for a certificate by email address      *
*************************************************/
std::vector<X509_Certificate> by_email(const X509_Store& store,
                                       const std::string& email)
   {
   DN_Check search_params("RFC822", email, ignore_case);
   return store.get_certs(search_params);
   }

/*************************************************
* Search for a certificate by CommonName         *
*************************************************/
std::vector<X509_Certificate> by_name(const X509_Store& store,
                                      const std::string& name)
   {
   DN_Check search_params("CommonName", name, substring_match);
   return store.get_certs(search_params);
   }

/*************************************************
* Search for a certificate by DNS name           *
*************************************************/
std::vector<X509_Certificate> by_dns(const X509_Store& store,
                                     const std::string& dns)
   {
   DN_Check search_params("DNS", dns, ignore_case);
   return store.get_certs(search_params);
   }

/*************************************************
* Search for a certificate by key id             *
*************************************************/
std::vector<X509_Certificate> by_keyid(const X509_Store& store, u64bit key_id)
   {
   KeyID_Match search_params(key_id);
   return store.get_certs(search_params);
   }

/*************************************************
* Search for a certificate by issuer/serial      *
*************************************************/
std::vector<X509_Certificate> by_iands(const X509_Store& store,
                                       const X509_DN& issuer,
                                       const MemoryRegion<byte>& serial)
   {
   IandS_Match search_params(issuer, serial);
   return store.get_certs(search_params);
   }

/*************************************************
* Search for a certificate by subject keyid      *
*************************************************/
std::vector<X509_Certificate> by_SKID(const X509_Store& store,
                                      const MemoryRegion<byte>& skid)
   {
   SKID_Match search_params(skid);
   return store.get_certs(search_params);
   }

}

}
