// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "vector.hh"

#include <boost/tokenizer.hpp>
#include <idna.h>
#include <stringprep.h>

#include "charset.hh"
#include "numeric_vocab.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "vocab_cast.hh"

using std::string;
using std::vector;
using std::free;

using boost::char_separator;

// General character code conversion routines.

static string
system_charset()
{
  char const * locale_charset_name = stringprep_locale_charset ();
  I(locale_charset_name != NULL);
  string sys_charset(locale_charset_name);
  return sys_charset;
}

void
charset_convert(string const & src_charset,
                string const & dst_charset,
                string const & src,
                string & dst,
                bool best_effort,
                origin::type whence)
{
  if (src_charset == dst_charset)
    dst = src;
  else
    {
      // Always try converting without special treatment first.
      char const * converted = stringprep_convert(src.c_str(),
                                                  dst_charset.c_str(),
                                                  src_charset.c_str());

      if (best_effort && !converted)
        {
          // Not all iconv implementations support this.
          string tmp_charset(dst_charset);
          tmp_charset += "//TRANSLIT";
          converted = stringprep_convert(src.c_str(),
                                         tmp_charset.c_str(),
                                         src_charset.c_str());

          // If that didn't work just give up.
          if (!converted)
            converted = src.c_str();
        }

      E(converted != NULL, whence,
        F("failed to convert string from %s to %s: '%s'")
        % src_charset % dst_charset % src);
      dst = string(converted);
      if (converted != src.c_str())
        free(const_cast<char *>(converted));
    }
}

size_t
display_width(utf8 const & utf)
{
  string const & u = utf();
  size_t sz = 0;
  string::const_iterator i = u.begin();
  while (i != u.end())
    {
      if (UNLIKELY(static_cast<u8>(*i) & static_cast<u8>(0x80)))
        {
          // A UTF-8 escape: consume the full escape.
          ++i;
          ++sz;
          while (i != u.end()
                 && (static_cast<u8>(*i) & static_cast<u8>(0x80))
                 && (!(static_cast<u8>(*i) & static_cast<u8>(0x40))))
            ++i;
        }
      else
        {
          // An ASCII-like character in the range 0..0x7F.
          ++i;
          ++sz;
        }
    }
  return sz;
}

// Lots of gunk to avoid charset conversion as much as possible.  Running
// iconv over every element of every path in a 30,000 file manifest takes
// multiple seconds, which then is a minimum bound on pretty much any
// operation we do...
static inline bool
system_charset_is_utf8_impl()
{
  string lc_encoding = lowercase(system_charset());
  return (lc_encoding == "utf-8"
          || lc_encoding == "utf_8"
          || lc_encoding == "utf8");
}

static inline bool
system_charset_is_utf8()
{
  static bool it_is = system_charset_is_utf8_impl();
  return it_is;
}

static inline bool
system_charset_is_ascii_extension_impl()
{
  if (system_charset_is_utf8())
    return true;
  string lc_encoding = lowercase(system_charset());
  // if your character set is identical to ascii in the lower 7 bits, then add
  // it here for a speed boost.
  return (lc_encoding.find("ascii") != string::npos
          || lc_encoding.find("8859") != string::npos
          || lc_encoding.find("ansi_x3.4") != string::npos
          || lc_encoding == "646" // another name for ascii
          // http://www.cs.mcgill.ca/~aelias4/encodings.html -- "EUC (Extended
          // Unix Code) is a simple and clean encoding, standard on Unix
          // systems.... It is backwards-compatible with ASCII (i.e. valid
          // ASCII implies valid EUC)."
          || lc_encoding.find("euc") != string::npos);
}

static inline bool
system_charset_is_ascii_extension()
{
  static bool it_is = system_charset_is_ascii_extension_impl();
  return it_is;
}

inline static bool
is_all_ascii(string const & utf)
{
  // could speed this up by vectorization -- mask against 0x80808080,
  // process a whole word at at time...
  for (string::const_iterator i = utf.begin(); i != utf.end(); ++i)
    if (0x80 & *i)
      return false;
  return true;
}

// this function must be fast.  do not make it slow.
void
utf8_to_system_strict(utf8 const & utf, string & ext)
{
  if (system_charset_is_utf8())
    ext = utf();
  else if (system_charset_is_ascii_extension()
           && is_all_ascii(utf()))
    ext = utf();
  else
    charset_convert("UTF-8", system_charset(), utf(), ext, false,
                    utf.made_from);
}

// this function must be fast.  do not make it slow.
void
utf8_to_system_best_effort(utf8 const & utf, string & ext)
{
  if (system_charset_is_utf8())
    ext = utf();
  else if (system_charset_is_ascii_extension()
           && is_all_ascii(utf()))
    ext = utf();
  else
    charset_convert("UTF-8", system_charset(), utf(), ext, true,
                    utf.made_from);
}

void
utf8_to_system_strict(utf8 const & utf, external & ext)
{
  string out;
  utf8_to_system_strict(utf, out);
  ext = external(out, utf.made_from);
}

void
utf8_to_system_best_effort(utf8 const & utf, external & ext)
{
  string out;
  utf8_to_system_best_effort(utf, out);
  ext = external(out, utf.made_from);
}

void
system_to_utf8(external const & ext, utf8 & utf)
{
  if (system_charset_is_utf8())
    utf = typecast_vocab<utf8>(ext);
  else if (system_charset_is_ascii_extension()
           && is_all_ascii(ext()))
    utf = typecast_vocab<utf8>(ext);
  else
    {
      string out;
      charset_convert(system_charset(), "UTF-8", ext(), out, false,
                      ext.made_from);
      utf = utf8(out, ext.made_from);
      I(utf8_validate(utf));
    }
}

// utf8_validate and the helper functions is_valid_unicode_char and
// utf8_consume_continuation_char g_utf8_validate and supporting functions
// from the file gutf8.c of the GLib library.

static bool
is_valid_unicode_char(u32 c)
{
  return (c < 0x110000 &&
          ((c & 0xfffff800) != 0xd800) &&
          (c < 0xfdd0 || c > 0xfdef) &&
          (c & 0xfffe) != 0xfffe);
}

static bool
utf8_consume_continuation_char(u8 c, u32 & val)
{
  if ((c & 0xc0) != 0x80)
    return false;
  val <<= 6;
  val |= c & 0x3f;
  return true;
}

bool
utf8_validate(utf8 const & utf)
{
  string::size_type left = utf().size();
  u32 min, val;

  for (string::const_iterator i = utf().begin();
       i != utf().end(); ++i, --left)
    {
      u8 c = *i;
      if (c < 128)
        continue;
      if ((c & 0xe0) == 0xc0)
        {
          if (left < 2)
            return false;
          if ((c & 0x1e) == 0)
            return false;
          ++i; --left; c = *i;
          if ((c & 0xc0) != 0x80)
            return false;
        }
      else
        {
          if ((c & 0xf0) == 0xe0)
            {
              if (left < 3)
                return false;
              min = 1 << 11;
              val = c & 0x0f;
              goto two_remaining;
            }
          else if ((c & 0xf8) == 0xf0)
            {
              if (left < 4)
                return false;
              min = 1 << 16;
              val = c & 0x07;
            }
          else
            return false;
          ++i; --left; c = *i;
          if (!utf8_consume_continuation_char(c, val))
            return false;
two_remaining:
          ++i; --left; c = *i;
          if (!utf8_consume_continuation_char(c, val))
            return false;
          ++i; --left; c = *i;
          if (!utf8_consume_continuation_char(c, val))
            return false;
          if (val < min)
            return false;
          if (!is_valid_unicode_char(val))
            return false;
        }
    }
  return true;
}

static string
decode_idna_error(int err)
{
  switch (static_cast<Idna_rc>(err))
    {
    case IDNA_STRINGPREP_ERROR: return "stringprep error"; break;
    case IDNA_PUNYCODE_ERROR: return "punycode error"; break;
    case IDNA_CONTAINS_NON_LDH: return "non-LDH characters"; break;
    case IDNA_CONTAINS_MINUS: return "leading / trailing hyphen-minus character"; break;
    case IDNA_INVALID_LENGTH: return "invalid length (output must be between 1 and 63 chars)"; break;
    case IDNA_NO_ACE_PREFIX: return "no ace prefix"; break;
    case IDNA_ROUNDTRIP_VERIFY_ERROR: return "roundtrip verify error"; break;
    case IDNA_CONTAINS_ACE_PREFIX: return "contains ACE prefix (\"xn--\")"; break;
    case IDNA_ICONV_ERROR: return "iconv error"; break;
    case IDNA_MALLOC_ERROR: return "malloc error"; break;
    default: return "unknown error"; break;
    }
  return "unknown error";
}

void
ace_to_utf8(string const & a, utf8 & utf, origin::type whence)
{
  char * out = NULL;
  L(FL("converting %d bytes from IDNA ACE to UTF-8") % a.size());
  int res = idna_to_unicode_8z8z(a.c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  E(res == IDNA_SUCCESS || res == IDNA_NO_ACE_PREFIX, whence,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % a.size()
    % decode_idna_error(res));
  utf = utf8(string(out), whence);
  free(out);
}

void
utf8_to_ace(utf8 const & utf, string & a)
{
  char * out = NULL;
  L(FL("converting %d bytes from UTF-8 to IDNA ACE") % utf().size());
  int res = idna_to_ascii_8z(utf().c_str(), &out, IDNA_USE_STD3_ASCII_RULES);
  E(res == IDNA_SUCCESS, utf.made_from,
    F("error converting %d UTF-8 bytes to IDNA ACE: %s")
    % utf().size()
    % decode_idna_error(res));
  a = string(out);
  free(out);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
