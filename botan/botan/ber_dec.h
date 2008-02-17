/*************************************************
* BER Decoder Header File                        *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#ifndef BOTAN_BER_DECODER_H__
#define BOTAN_BER_DECODER_H__

#include <botan/asn1_oid.h>
#include <botan/data_src.h>

namespace Botan {

/*************************************************
* BER Decoding Object                            *
*************************************************/
class BER_Decoder
   {
   public:
      BER_Object get_next_object();
      void push_back(const BER_Object&);

      bool more_items() const;
      BER_Decoder& verify_end();
      BER_Decoder& discard_remaining();

      BER_Decoder  start_cons(ASN1_Tag);
      BER_Decoder& end_cons();

      BER_Decoder& raw_bytes(MemoryRegion<byte>&);

      BER_Decoder& decode_null();
      BER_Decoder& decode(bool&);
      BER_Decoder& decode(u32bit&);
      BER_Decoder& decode(class BigInt&);
      BER_Decoder& decode(MemoryRegion<byte>&, ASN1_Tag);

      BER_Decoder& decode(bool&, ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      BER_Decoder& decode(u32bit&, ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      BER_Decoder& decode(class BigInt&,
                          ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      BER_Decoder& decode(MemoryRegion<byte>&, ASN1_Tag,
                          ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);

      BER_Decoder& decode(class ASN1_Object&);

      template<typename T>
      BER_Decoder& decode_optional(T&, ASN1_Tag, ASN1_Tag, const T& = T());

      template<typename T>
      BER_Decoder& decode_list(std::vector<T>&, bool = true);

      BER_Decoder& decode_optional_string(MemoryRegion<byte>&,
                                          ASN1_Tag, u16bit);

      BER_Decoder(DataSource&);
      BER_Decoder(const byte[], u32bit);
      BER_Decoder(const MemoryRegion<byte>&);
      BER_Decoder(const BER_Decoder&);
      ~BER_Decoder();
   private:
      BER_Decoder& operator=(const BER_Decoder&) { return (*this); }

      BER_Decoder* parent;
      DataSource* source;
      BER_Object pushed;
      mutable bool owns;
   };

/*************************************************
* Decode an OPTIONAL or DEFAULT element          *
*************************************************/
template<typename T>
BER_Decoder& BER_Decoder::decode_optional(T& out,
                                          ASN1_Tag type_tag,
                                          ASN1_Tag class_tag,
                                          const T& default_value)
   {
   BER_Object obj = get_next_object();

   if(obj.type_tag == type_tag && obj.class_tag == class_tag)
      {
      if(class_tag & CONSTRUCTED)
         BER_Decoder(obj.value).decode(out).verify_end();
      else
         {
         push_back(obj);
         decode(out, type_tag, class_tag);
         }
      }
   else
      {
      out = default_value;
      push_back(obj);
      }

   return (*this);
   }

/*************************************************
* Decode a list of homogenously typed values     *
*************************************************/
template<typename T>
BER_Decoder& BER_Decoder::decode_list(std::vector<T>& vec, bool clear_it)
   {
   if(clear_it)
      vec.clear();

   while(more_items())
      {
      T value;
      decode(value);
      vec.push_back(value);
      }
   return (*this);
   }

/*************************************************
* BER Decoding Functions                         *
*************************************************/
namespace BER {

void decode(BER_Decoder&, Key_Constraints&);

}

}

#endif
