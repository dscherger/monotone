/*************************************************
* AlternativeName Source File                    *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/der_enc.h>
#include <botan/ber_dec.h>
#include <botan/oids.h>
#include <botan/stl_util.h>
#include <botan/charset.h>

namespace Botan {

/*************************************************
* Create an AlternativeName                      *
*************************************************/
AlternativeName::AlternativeName(const std::string& email_addr,
                                 const std::string& uri,
                                 const std::string& dns)
   {
   add_attribute("RFC822", email_addr);
   add_attribute("DNS", dns);
   add_attribute("URI", uri);
   }

/*************************************************
* Add an attribute to an alternative name        *
*************************************************/
void AlternativeName::add_attribute(const std::string& type,
                                    const std::string& str)
   {
   if(type == "" || str == "")
      return;

   typedef std::multimap<std::string, std::string>::iterator iter;
   std::pair<iter, iter> range = alt_info.equal_range(type);
   for(iter j = range.first; j != range.second; ++j)
      if(j->second == str)
         return;

   multimap_insert(alt_info, type, str);
   }

/*************************************************
* Add an OtherName field                         *
*************************************************/
void AlternativeName::add_othername(const OID& oid, const std::string& value,
                                    ASN1_Tag type)
   {
   if(value == "")
      return;
   multimap_insert(othernames, oid, ASN1_String(value, type));
   }

/*************************************************
* Get the attributes of this alternative name    *
*************************************************/
std::multimap<std::string, std::string> AlternativeName::get_attributes() const
   {
   return alt_info;
   }

/*************************************************
* Get the otherNames                             *
*************************************************/
std::multimap<OID, ASN1_String> AlternativeName::get_othernames() const
   {
   return othernames;
   }

/*************************************************
* Return all of the alternative names            *
*************************************************/
std::multimap<std::string, std::string> AlternativeName::contents() const
   {
   std::multimap<std::string, std::string> names;

   typedef std::multimap<std::string, std::string>::const_iterator rdn_iter;
   for(rdn_iter j = alt_info.begin(); j != alt_info.end(); ++j)
      multimap_insert(names, j->first, j->second);

   typedef std::multimap<OID, ASN1_String>::const_iterator on_iter;
   for(on_iter j = othernames.begin(); j != othernames.end(); ++j)
      multimap_insert(names, OIDS::lookup(j->first), j->second.value());

   return names;
   }

/*************************************************
* Return if this object has anything useful      *
*************************************************/
bool AlternativeName::has_items() const
   {
   return (alt_info.size() > 0 || othernames.size() > 0);
   }

namespace {

/*************************************************
* DER encode an AlternativeName entry            *
*************************************************/
void encode_entries(DER_Encoder& encoder,
                    const std::multimap<std::string, std::string>& attr,
                    const std::string& type, ASN1_Tag tagging)
   {
   typedef std::multimap<std::string, std::string>::const_iterator iter;

   std::pair<iter, iter> range = attr.equal_range(type);
   for(iter j = range.first; j != range.second; ++j)
      {
      ASN1_String asn1_string(j->second, IA5_STRING);
      encoder.add_object(tagging, CONTEXT_SPECIFIC, asn1_string.iso_8859());
      }
   }

}

/*************************************************
* DER encode an AlternativeName extension        *
*************************************************/
void AlternativeName::encode_into(DER_Encoder& der) const
   {
   der.start_cons(SEQUENCE);

   encode_entries(der, alt_info, "RFC822", ASN1_Tag(1));
   encode_entries(der, alt_info, "DNS", ASN1_Tag(2));
   encode_entries(der, alt_info, "URI", ASN1_Tag(6));

   std::multimap<OID, ASN1_String>::const_iterator i;
   for(i = othernames.begin(); i != othernames.end(); ++i)
      {
      der.start_explicit(0)
         .encode(i->first)
         .start_explicit(0)
            .encode(i->second)
         .end_explicit()
      .end_explicit();
      }

   der.end_cons();
   }

/*************************************************
* Decode a BER encoded AlternativeName           *
*************************************************/
void AlternativeName::decode_from(BER_Decoder& source)
   {
   BER_Decoder names = source.start_cons(SEQUENCE);

   while(names.more_items())
      {
      BER_Object obj = names.get_next_object();
      if((obj.class_tag != CONTEXT_SPECIFIC) &&
         (obj.class_tag != (CONTEXT_SPECIFIC | CONSTRUCTED)))
         continue;

      ASN1_Tag tag = obj.type_tag;

      if(tag == 0)
         {
         BER_Decoder othername(obj.value);

         OID oid;
         othername.decode(oid);
         if(othername.more_items())
            {
            BER_Object othername_value_outer = othername.get_next_object();
            othername.verify_end();

            if(othername_value_outer.type_tag != ASN1_Tag(0) ||
               othername_value_outer.class_tag !=
                   (CONTEXT_SPECIFIC | CONSTRUCTED)
               )
               throw Decoding_Error("Invalid tags on otherName value");

            BER_Decoder othername_value_inner(othername_value_outer.value);

            BER_Object value = othername_value_inner.get_next_object();
            othername_value_inner.verify_end();

            ASN1_Tag value_type = value.type_tag;

            if(is_string_type(value_type) && value.class_tag == UNIVERSAL)
               add_othername(oid, ASN1::to_string(value), value_type);
            }
         }
      else if(tag == 1 || tag == 2 || tag == 6)
         {
         const std::string value = Charset::transcode(ASN1::to_string(obj),
                                                      LATIN1_CHARSET,
                                                      LOCAL_CHARSET);

         if(tag == 1) add_attribute("RFC822", value);
         if(tag == 2) add_attribute("DNS", value);
         if(tag == 6) add_attribute("URI", value);
         }

      }
   }

}
