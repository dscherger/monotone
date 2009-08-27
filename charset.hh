// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __CHARSET_HH__
#define __CHARSET_HH__

#include "vocab.hh"

// Charset conversions.

void charset_convert(std::string const & src_charset,
                     std::string const & dst_charset,
                     std::string const & src,
                     std::string & dst,
                     bool best_effort,
                     origin::type whence);
void system_to_utf8(external const & system, utf8 & utf);
void utf8_to_system_strict(utf8 const & utf, external & system);
void utf8_to_system_strict(utf8 const & utf, std::string & system);
void utf8_to_system_best_effort(utf8 const & utf, external & system);
void utf8_to_system_best_effort(utf8 const & utf, std::string & system);
bool utf8_validate(utf8 const & utf);

// These are exposed for unit testing only.
void ace_to_utf8(std::string const & a, utf8 & utf, origin::type whence);
void utf8_to_ace(utf8 const & utf, std::string & a);


// Returns length in characters (not bytes).
// Is not aware of combining and invisible characters.
size_t display_width(utf8 const & utf);

// Specific internal / external conversions for various vocab terms.
void internalize_cert_name(utf8 const & utf, cert_name & c);
void internalize_key_name(utf8 const & utf, key_name & key);
void internalize_var_domain(utf8 const & utf, var_domain & d);
void externalize_var_domain(var_domain const & d, external & ext);

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
