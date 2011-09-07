/*************************************************
* Gzip Compressor Header File                    *
* (C) 2001 Peter J Jones (pjones@pmade.org)      *
*     2001-2004 Jack Lloyd                       *
*************************************************/

#ifndef BOTAN_EXT_GZIP_H__
#define BOTAN_EXT_GZIP_H__

#include <botan/version.h>
#include <botan/filter.h>
#include <botan/pipe.h>

namespace Botan {

namespace GZIP {

   /* A basic header - we only need to set the IDs and compression method */
   const byte GZIP_HEADER[] = {
      0x1f, 0x8b, /* Magic ID bytes */
      0x08, /* Compression method of 'deflate' */
      0x00, /* Flags all empty */
      0x00, 0x00, 0x00, 0x00, /* MTIME */
      0x00, /* Extra flags */
      0xff, /* Operating system (unknown) */
   };

   const unsigned int HEADER_POS_OS = 9;

   const unsigned int FOOTER_LENGTH = 8;

}

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,9,11)
   typedef size_t filter_length_t;
#else
   typedef u32bit filter_length_t;
#endif

/*************************************************
* Gzip Compression Filter                        *
*************************************************/
class Gzip_Compression : public Filter
   {
   public:
      void write(const byte input[], filter_length_t length);
      void start_msg();
      void end_msg();
      std::string name() const { return "Gzip_Compression"; }

      Gzip_Compression(u32bit = 1);
      ~Gzip_Compression();
   private:
      void clear();
      void put_header();
      void put_footer();
      const u32bit level;
      SecureVector<byte> buffer;
      class Zlib_Stream* zlib;
      Pipe pipe; /* A pipe for the crc32 processing */
      u32bit count;
   };

/*************************************************
* Gzip Decompression Filter                      *
*************************************************/
class Gzip_Decompression : public Filter
   {
   public:
      void write(const byte input[], filter_length_t length);
      void start_msg();
      void end_msg();
      std::string name() const { return "Gzip_Decompression"; }

      Gzip_Decompression();
      ~Gzip_Decompression();
   private:
      u32bit eat_footer(const byte input[], u32bit length);
      void check_footer();
      void clear();
      SecureVector<byte> buffer;
      class Zlib_Stream* zlib;
      bool no_writes;
      u32bit pos; /* Current position in the message */
      Pipe pipe; /* A pipe for the crc32 processing */
      u32bit datacount; /* Amount of uncompressed output */
      SecureVector<byte> footer;
      bool in_footer;
   };

}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
