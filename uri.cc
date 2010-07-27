// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "pcrewrap.hh"
#include "sanity.hh"
#include "uri.hh"

using std::string;
typedef string::size_type stringpos;

static void
parse_authority(string const & in, uri_t & uri, origin::type made_from)
{
  L(FL("matched URI authority: '%s'") % in);

  stringpos p = 0;

  // First, there might be a user: one or more non-@ characters followed
  // by an @.
  stringpos user_end = in.find('@', p);
  if (user_end != 0 && user_end < in.size())
    {
      uri.user.assign(in, 0, user_end);
      p = user_end + 1;
      L(FL("matched URI user: '%s'") % uri.user);
    }

  // The next thing must either be an ipv6 address, which has the form
  // \[ [0-9A-Za-z:]+ \] and we discard the square brackets, or some other
  // sort of hostname, [^:]+.  (A host-part can be terminated by /, ?, or #
  // as well as :, but our caller has taken care of that.)
  if (p < in.size() && in.at(p) == '[')
    {
      p++;
      stringpos ipv6_end = in.find(']', p);
      E(ipv6_end != string::npos, made_from,
        F("IPv6 address in URI has no closing ']'"));

      uri.host.assign(in, p, ipv6_end - p);
      p = ipv6_end + 1;
      L(FL("matched URI host (IPv6 address): '%s'") % uri.host);
    }
  else
    {
      stringpos host_end = in.find(':', p);
      uri.host.assign(in, p, host_end - p);
      p = host_end;
      L(FL("matched URI host: '%s'") % uri.host);
    }

  // Finally, if the host-part was ended by a colon, there is a port number
  // following, which must consist entirely of digits.
  if (p < in.size() && in.at(p) == ':')
    {
      p++;
      E(p < in.size(), made_from,
        F("explicit port-number specification in URI has no digits"));

      E(in.find_first_not_of("0123456789", p) == string::npos, made_from,
        F("explicit port-number specification in URI contains nondigits"));

      uri.port.assign(in, p, string::npos);
      L(FL("matched URI port: '%s'") % uri.port);
    }
}

static bool
try_parse_bare_authority(string const & in, uri_t & uri, origin::type made_from)
{
  if (in.empty())
    return false;

  pcre::regex hostlike("^("
                       "[^:/]+(:[0-9]+)?" // hostname or ipv4
                       "|"
                       "[:0-9a-fA-F]+" // non-bracketed ipv6
                       "|"
                       "\\[[:0-9a-fA-F]+\\](:[0-9]+)?" // bracketed ipv6
                       ")$",
                       origin::internal);

  if (hostlike.match(in, made_from))
    {
      parse_authority(in, uri, made_from);
      return true;
    }
  else
    {
      return false;
    }
}

void
parse_uri(string const & in, uri_t & uri, origin::type made_from)
{
  uri.clear();

  if (try_parse_bare_authority(in, uri, made_from))
    {
      return;
    }

  stringpos p = 0;

  // This is a simplified URI grammar. It does the basics.

  // First there may be a scheme: one or more characters which are not
  // ":/?#", followed by a colon.
  stringpos scheme_end = in.find_first_of(":/?#", p);

  if (scheme_end != 0 && scheme_end < in.size() && in.at(scheme_end) == ':')
    {
      uri.scheme.assign(in, p, scheme_end - p);
      p = scheme_end + 1;
      L(FL("matched URI scheme: '%s'") % uri.scheme);
    }

  // Next, there may be an authority: "//" followed by zero or more
  // characters which are not "/?#".

  if (p + 1 < in.size() && in.at(p) == '/' && in.at(p+1) == '/')
    {
      p += 2;
      stringpos authority_end = in.find_first_of("/?#", p);
      if (authority_end != p)
        {
          parse_authority(string(in, p, authority_end - p), uri, made_from);
          p = authority_end;
        }
      if (p >= in.size())
        return;
    }

  // Next, a path: zero or more characters which are not "?#".
  {
    stringpos path_end = in.find_first_of("?#", p);
    uri.path.assign(in, p, path_end - p);
    p = path_end;
    L(FL("matched URI path: '%s'") % uri.path);
    if (p >= in.size())
      return;
  }

  // Next, perhaps a query: "?" followed by zero or more characters
  // which are not "#".
  if (in.at(p) == '?')
    {
      p++;
      stringpos query_end = in.find('#', p);
      uri.query.assign(in, p, query_end - p);
      p = query_end;
      L(FL("matched URI query: '%s'") % uri.query);
      if (p >= in.size())
        return;
    }

  // Finally, if there is a '#', then whatever comes after it in the string
  // is a fragment identifier.
  if (in.at(p) == '#')
    {
      uri.fragment.assign(in, p + 1, string::npos);
      L(FL("matched URI fragment: '%s'") % uri.fragment);
    }
}

string
urldecode(string const & in, origin::type made_from)
{
  string out;

  for (string::const_iterator i = in.begin(); i != in.end(); ++i)
    {
      if (*i != '%')
        out += *i;
      else
        {
          char d1, d2;
          ++i;
          E(i != in.end(), made_from, F("Bad URLencoded string '%s'") % in);
          d1 = *i;
          ++i;
          E(i != in.end(), made_from, F("Bad URLencoded string '%s'") % in);
          d2 = *i;

          char c = 0;
          switch(d1)
            {
            case '0': c += 0; break;
            case '1': c += 1; break;
            case '2': c += 2; break;
            case '3': c += 3; break;
            case '4': c += 4; break;
            case '5': c += 5; break;
            case '6': c += 6; break;
            case '7': c += 7; break;
            case '8': c += 8; break;
            case '9': c += 9; break;
            case 'a': case 'A': c += 10; break;
            case 'b': case 'B': c += 11; break;
            case 'c': case 'C': c += 12; break;
            case 'd': case 'D': c += 13; break;
            case 'e': case 'E': c += 14; break;
            case 'f': case 'F': c += 15; break;
            default: E(false, made_from, F("Bad URLencoded string '%s'") % in);
            }
          c *= 16;
          switch(d2)
            {
            case '0': c += 0; break;
            case '1': c += 1; break;
            case '2': c += 2; break;
            case '3': c += 3; break;
            case '4': c += 4; break;
            case '5': c += 5; break;
            case '6': c += 6; break;
            case '7': c += 7; break;
            case '8': c += 8; break;
            case '9': c += 9; break;
            case 'a': case 'A': c += 10; break;
            case 'b': case 'B': c += 11; break;
            case 'c': case 'C': c += 12; break;
            case 'd': case 'D': c += 13; break;
            case 'e': case 'E': c += 14; break;
            case 'f': case 'F': c += 15; break;
            default: E(false, made_from, F("Bad URLencoded string '%s'") % in);
            }
          out += c;
        }
    }

  return out;
}



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
