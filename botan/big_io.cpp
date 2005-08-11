/*************************************************
* BigInt Input/Output Source File                *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/bigint.h>
#include <iostream>

namespace Botan {

/*************************************************
* Write the BigInt into a stream                 *
*************************************************/
std::ostream& operator<<(std::ostream& stream, const BigInt& n)
   {
   BigInt::Base base = BigInt::Decimal;
   if(stream.flags() & std::ios::hex)
      base = BigInt::Hexadecimal;
   else if(stream.flags() & std::ios::oct)
      base = BigInt::Octal;

   if(n == 0)
      stream.write("0", 1);
   else
      {
      if(n < 0)
         stream.write("-", 1);
      SecureVector<byte> buffer = BigInt::encode(n, base);
      u32bit skip = 0;
      while(buffer[skip] == '0' && skip < buffer.size())
         skip++;
      stream.write((const char*)buffer.begin() + skip, buffer.size() - skip);
      }
   if(!stream.good())
      throw Stream_IO_Error("BigInt output operator has failed");
   return stream;
   }

/*************************************************
* Read the BigInt from a stream                  *
*************************************************/
std::istream& operator>>(std::istream& stream, BigInt& n)
   {
   std::string str;
   std::getline(stream, str);
   if(stream.bad() || (stream.fail() && !stream.eof()))
      throw Stream_IO_Error("BigInt input operator has failed");
   n = BigInt(str);
   return stream;
   }

}
