/*************************************************
* Simple ASN.1 String Types Source File          *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/der_enc.h>
#include <botan/ber_dec.h>
#include <botan/charset.h>
#include <botan/parsing.h>
#include <botan/config.h>

namespace Botan {

namespace {

/*************************************************
* Choose an encoding for the string              *
*************************************************/
ASN1_Tag choose_encoding(const std::string& str)
   {
   static const byte IS_PRINTABLE[256] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
      0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
      0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00 };

   for(u32bit j = 0; j != str.size(); ++j)
      if(!IS_PRINTABLE[(byte)str[j]])
         {
         const std::string type = global_config().option("x509/ca/str_type");

         if(type == "utf8")   return UTF8_STRING;
         if(type == "latin1") return T61_STRING;
         throw Invalid_Argument("Bad setting for x509/ca/str_type: " + type);
         }
   return PRINTABLE_STRING;
   }

}

/*************************************************
* Check if type is a known ASN.1 string type     *
*************************************************/
bool is_string_type(ASN1_Tag tag)
   {
   if(tag == NUMERIC_STRING || tag == PRINTABLE_STRING ||
      tag == VISIBLE_STRING || tag == T61_STRING || tag == IA5_STRING ||
      tag == UTF8_STRING || tag == BMP_STRING)
      return true;
   return false;
   }

/*************************************************
* Create an ASN1_String                          *
*************************************************/
ASN1_String::ASN1_String(const std::string& str, ASN1_Tag t) : tag(t)
   {
   iso_8859_str = Charset::transcode(str, LOCAL_CHARSET, LATIN1_CHARSET);

   if(tag == DIRECTORY_STRING)
      tag = choose_encoding(iso_8859_str);

   if(tag != NUMERIC_STRING &&
      tag != PRINTABLE_STRING &&
      tag != VISIBLE_STRING &&
      tag != T61_STRING &&
      tag != IA5_STRING &&
      tag != UTF8_STRING &&
      tag != BMP_STRING)
      throw Invalid_Argument("ASN1_String: Unknown string type " +
                             to_string(tag));
   }

/*************************************************
* Create an ASN1_String                          *
*************************************************/
ASN1_String::ASN1_String(const std::string& str)
   {
   iso_8859_str = Charset::transcode(str, LOCAL_CHARSET, LATIN1_CHARSET);
   tag = choose_encoding(iso_8859_str);
   }

/*************************************************
* Return this string in ISO 8859-1 encoding      *
*************************************************/
std::string ASN1_String::iso_8859() const
   {
   return iso_8859_str;
   }

/*************************************************
* Return this string in local encoding           *
*************************************************/
std::string ASN1_String::value() const
   {
   return Charset::transcode(iso_8859_str, LATIN1_CHARSET, LOCAL_CHARSET);
   }

/*************************************************
* Return the type of this string object          *
*************************************************/
ASN1_Tag ASN1_String::tagging() const
   {
   return tag;
   }

/*************************************************
* DER encode an ASN1_String                      *
*************************************************/
void ASN1_String::encode_into(DER_Encoder& encoder) const
   {
   std::string value = iso_8859();
   if(tagging() == UTF8_STRING)
      value = Charset::transcode(value, LATIN1_CHARSET, UTF8_CHARSET);
   encoder.add_object(tagging(), UNIVERSAL, value);
   }

/*************************************************
* Decode a BER encoded ASN1_String               *
*************************************************/
void ASN1_String::decode_from(BER_Decoder& source)
   {
   BER_Object obj = source.get_next_object();

   Character_Set charset_is;

   if(obj.type_tag == BMP_STRING)
      charset_is = UCS2_CHARSET;
   else if(obj.type_tag == UTF8_STRING)
      charset_is = UTF8_CHARSET;
   else
      charset_is = LATIN1_CHARSET;

   *this = ASN1_String(
      Charset::transcode(ASN1::to_string(obj), charset_is, LOCAL_CHARSET),
      obj.type_tag);
   }

}
