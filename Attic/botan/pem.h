/*************************************************
* PEM Encoding/Decoding Header File              *
* (C) 1999-2007 Jack Lloyd                       *
*************************************************/

#ifndef BOTAN_PEM_H__
#define BOTAN_PEM_H__

#include <botan/data_src.h>

namespace Botan {

namespace PEM_Code {

/*************************************************
* PEM Encoding/Decoding                          *
*************************************************/
BOTAN_DLL std::string encode(const byte[], u32bit,
                             const std::string&, u32bit = 64);
BOTAN_DLL std::string encode(const MemoryRegion<byte>&,
                             const std::string&, u32bit = 64);

BOTAN_DLL SecureVector<byte> decode(DataSource&, std::string&);
BOTAN_DLL SecureVector<byte> decode_check_label(DataSource&,
                                                const std::string&);
BOTAN_DLL bool matches(DataSource&, const std::string& = "",
                       u32bit search_range = 4096);

}

}

#endif
