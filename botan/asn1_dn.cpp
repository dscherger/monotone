/*************************************************
* X509_DN Source File                            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/parsing.h>
#include <botan/oids.h>

namespace Botan {

/*************************************************
* Create an empty X509_DN                        *
*************************************************/
X509_DN::X509_DN()
   {
   }

/*************************************************
* Create an X509_DN                              *
*************************************************/
X509_DN::X509_DN(const std::multimap<OID, std::string>& args)
   {
   std::multimap<OID, std::string>::const_iterator j;
   for(j = args.begin(); j != args.end(); j++)
      add_attribute(j->first, j->second);
   }

/*************************************************
* Create an X509_DN                              *
*************************************************/
X509_DN::X509_DN(const std::multimap<std::string, std::string>& args)
   {
   std::multimap<std::string, std::string>::const_iterator j;
   for(j = args.begin(); j != args.end(); j++)
      add_attribute(OIDS::lookup(j->first), j->second);
   }

/*************************************************
* Add an attribute to a X509_DN                  *
*************************************************/
void X509_DN::add_attribute(const std::string& type,
                            const std::string& str)
   {
   OID oid = OIDS::lookup(type);
   add_attribute(oid, str);
   }

/*************************************************
* Add an attribute to a X509_DN                  *
*************************************************/
void X509_DN::add_attribute(const OID& oid, const std::string& str)
   {
   if(str == "")
      return;

   typedef std::multimap<OID, ASN1_String>::iterator rdn_iter;

   std::pair<rdn_iter, rdn_iter> range = dn_info.equal_range(oid);
   for(rdn_iter j = range.first; j != range.second; j++)
      if(j->second.value() == str)
         return;

   multimap_insert(dn_info, oid, ASN1_String(str));
   dn_bits.destroy();
   }

/*************************************************
* Get the attributes of this X509_DN             *
*************************************************/
std::multimap<OID, std::string> X509_DN::get_attributes() const
   {
   typedef std::multimap<OID, ASN1_String>::const_iterator rdn_iter;

   std::multimap<OID, std::string> retval;
   for(rdn_iter j = dn_info.begin(); j != dn_info.end(); j++)
      multimap_insert(retval, j->first, j->second.value());
   return retval;
   }

/*************************************************
* Get a single attribute type                    *
*************************************************/
std::vector<std::string> X509_DN::get_attribute(const std::string& attr) const
   {
   typedef std::multimap<OID, ASN1_String>::const_iterator rdn_iter;

   const OID oid = OIDS::lookup(deref_info_field(attr));
   std::pair<rdn_iter, rdn_iter> range = dn_info.equal_range(oid);

   std::vector<std::string> values;
   for(rdn_iter j = range.first; j != range.second; j++)
      values.push_back(j->second.value());
   return values;
   }

/*************************************************
* Handle the decoding operation of a DN          *
*************************************************/
void X509_DN::do_decode(const MemoryRegion<byte>& bits)
   {
   BER_Decoder sequence(bits);

   while(sequence.more_items())
      {
      BER_Decoder rdn = BER::get_subset(sequence);
      while(rdn.more_items())
         {
         OID oid;
         ASN1_String str;

         BER_Decoder ava = BER::get_subsequence(rdn);
         BER::decode(ava, oid);
         BER::decode(ava, str);
         ava.verify_end();

         add_attribute(oid, str.value());
         }
      }

   dn_bits = bits;
   }

/*************************************************
* Return the BER encoded data, if any            *
*************************************************/
SecureVector<byte> X509_DN::get_bits() const
   {
   return dn_bits;
   }

/*************************************************
* Deref aliases in a subject/issuer info request *
*************************************************/
std::string X509_DN::deref_info_field(const std::string& info)
   {
   if(info == "Name" || info == "CommonName") return "X520.CommonName";
   if(info == "SerialNumber")                 return "X520.SerialNumber";
   if(info == "Country")                      return "X520.Country";
   if(info == "Organization")                 return "X520.Organization";
   if(info == "Organizational Unit")          return "X520.OrganizationalUnit";
   if(info == "Locality")                     return "X520.Locality";
   if(info == "State" || info == "Province")  return "X520.State";
   if(info == "Email")                        return "RFC822";
   return info;
   }

/*************************************************
* Compare two X509_DNs for equality              *
*************************************************/
bool operator==(const X509_DN& dn1, const X509_DN& dn2)
   {
   typedef std::multimap<OID, std::string>::const_iterator rdn_iter;

   std::multimap<OID, std::string> attr1 = dn1.get_attributes();
   std::multimap<OID, std::string> attr2 = dn2.get_attributes();

   if(attr1.size() != attr2.size()) return false;

   rdn_iter p1 = attr1.begin();
   rdn_iter p2 = attr2.begin();

   while(true)
      {
      if(p1 == attr1.end() && p2 == attr2.end())
         break;
      if(p1 == attr1.end())      return false;
      if(p2 == attr2.end())      return false;
      if(p1->first != p2->first) return false;
      if(!x500_name_cmp(p1->second, p2->second))
         return false;
      p1++;
      p2++;
      }
   return true;
   }

/*************************************************
* Compare two X509_DNs for inequality            *
*************************************************/
bool operator!=(const X509_DN& dn1, const X509_DN& dn2)
   {
   return !(dn1 == dn2);
   }

/*************************************************
* Compare two X509_DNs                           *
*************************************************/
bool operator<(const X509_DN& dn1, const X509_DN& dn2)
   {
   typedef std::multimap<OID, std::string>::const_iterator rdn_iter;

   std::multimap<OID, std::string> attr1 = dn1.get_attributes();
   std::multimap<OID, std::string> attr2 = dn2.get_attributes();

   if(attr1.size() < attr2.size()) return true;
   if(attr1.size() > attr2.size()) return false;

   for(rdn_iter p1 = attr1.begin(); p1 != attr1.end(); p1++)
      {
      std::multimap<OID, std::string>::const_iterator p2;
      p2 = attr2.find(p1->first);
      if(p2 == attr2.end())       return false;
      if(p1->second > p2->second) return false;
      if(p1->second < p2->second) return true;
      }
   return false;
   }

namespace DER {

namespace {

/*************************************************
* DER encode a RelativeDistinguishedName         *
*************************************************/
void do_ava(DER_Encoder& encoder, std::multimap<OID, std::string>& dn_info,
            ASN1_Tag string_type, const std::string& oid_str,
            bool must_exist = false)
   {
   typedef std::multimap<OID, std::string>::iterator rdn_iter;

   const OID oid = OIDS::lookup(oid_str);
   const bool exists = (dn_info.find(oid) != dn_info.end());

   if(!exists && must_exist)
      throw Encoding_Error("X509_DN: No entry for " + oid_str);
   if(!exists) return;

   std::pair<rdn_iter, rdn_iter> range = dn_info.equal_range(oid);

   for(rdn_iter j = range.first; j != range.second; j++)
      {
      ASN1_String asn1_string(j->second, string_type);

      encoder.start_set();
      encoder.start_sequence();
      DER::encode(encoder, oid);
      DER::encode(encoder, asn1_string);
      encoder.end_sequence();
      encoder.end_set();
      }
   }

}

/*************************************************
* DER encode a DistinguishedName                 *
*************************************************/
void encode(DER_Encoder& encoder, const X509_DN& dn)
   {
   std::multimap<OID, std::string> dn_info = dn.get_attributes();
   SecureVector<byte> bits = dn.get_bits();

   encoder.start_sequence();

   if(bits.has_items())
      encoder.add_raw_octets(bits);
   else
      {
      do_ava(encoder, dn_info, PRINTABLE_STRING, "X520.Country", true);
      do_ava(encoder, dn_info, DIRECTORY_STRING, "X520.State");
      do_ava(encoder, dn_info, DIRECTORY_STRING, "X520.Locality");
      do_ava(encoder, dn_info, DIRECTORY_STRING, "X520.Organization");
      do_ava(encoder, dn_info, DIRECTORY_STRING, "X520.OrganizationalUnit");
      do_ava(encoder, dn_info, DIRECTORY_STRING, "X520.CommonName", true);
      do_ava(encoder, dn_info, PRINTABLE_STRING, "X520.SerialNumber");
      }
   encoder.end_sequence();
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded DistinguishedName         *
*************************************************/
void decode(BER_Decoder& source, X509_DN& dn)
   {
   dn = X509_DN();
   BER_Decoder sequence = BER::get_subsequence(source);
   SecureVector<byte> bits = sequence.get_remaining();
   dn.do_decode(bits);
   }

}

}
