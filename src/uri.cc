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
#include <vector>
#include <algorithm>

using std::string;
using std::vector;
typedef string::size_type stringpos;

void
parse_uri(string const & in, uri_t & uri, origin::type made_from)
{
  uri.clear();

  // this is a little tweak to recognize paths as authorities
  string modified = in;
  pcre::regex has_scheme("^\\w[\\w\\d\\+\\-\\.]*:[^\\d]+", origin::internal);
  if (!has_scheme.match(in, made_from))
    {
      L(FL("prepending pseudo scheme and authority marker"));
      modified = "ZZZ://" + in;
    }

  // RFC 3986, Appendix B
  pcre::regex matcher("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?",
                      origin::internal);
  vector<string> matches;
  E(matcher.match(modified, made_from, matches), made_from,
    F("unable to parse URI '%s'") % in);

  I(matches.size() == 10);

  //
  // scheme matching
  //
  if (matches[2] != "ZZZ")
    {
      uri.scheme.assign(matches[2]);
      std::transform(uri.scheme.begin(), uri.scheme.end(), uri.scheme.begin(), ::tolower);
      L(FL("matched URI scheme: '%s'") % uri.scheme);
    }

  //
  // host and port matching
  //
  if (!matches[4].empty())
    {
      L(FL("parsing host and optional port of '%s'") % matches[4]);

      // we do not allow non-bracketed IPv6, since
      // host matches like "abc:123" cannot be distinguished
      pcre::regex hostlike("^(([^@]+)@)?(([^:\\[\\]]+)|\\[([:0-9a-fA-F]+)\\])(:(\\d*))?$",
                           origin::internal);
      vector<string> hostlike_matches;

      E(hostlike.match(matches[4], made_from, hostlike_matches), made_from,
        F("unable to parse host of URI '%s'") % in);

      if (!hostlike_matches[2].empty())
        {
          uri.user.assign(hostlike_matches[2]);
          L(FL("matched URI user: '%s'") % uri.user);
        }

      if (!hostlike_matches[4].empty())
        {
          uri.host.assign(hostlike_matches[4]);

        }
      else if (!hostlike_matches[5].empty())
        {
          // for IPv6 we discard the square brackets
          uri.host.assign(hostlike_matches[5]);
        }
      else
        I(false);

      std::transform(uri.host.begin(), uri.host.end(), uri.host.begin(), ::tolower);
      L(FL("matched URI host: '%s'") % uri.host);

      if (!hostlike_matches[7].empty())
        {
          uri.port.assign(hostlike_matches[7]);
          L(FL("matched URI port: '%s'") % uri.port);
        }
    }

  //
  // path matching
  //
  if (!matches[5].empty())
    {
      // FIXME: we do not
      //  - remove dot components ("/./" and "/../")
      //  - check whether the path of authority-less URIs do not start with "//"
      //  - convert the path in "scheme:host/:foo" to "./:foo"
      uri.path.assign(urldecode(matches[5], made_from));
      L(FL("matched URI path: '%s'") % uri.path);
    }

  //
  // query matching
  //
  if (!matches[7].empty())
    {
      // FIXME: the query string is not broken up at this point
      // and therefor cannot be urldecoded without possible side effects
      uri.query.assign(matches[7]);
      L(FL("matched URI query: '%s'") % uri.query);
    }

  //
  // fragment matching
  //
  if (!matches[9].empty())
    {
      uri.fragment.assign(urldecode(matches[9], made_from));
      L(FL("matched URI fragment: '%s'") % uri.fragment);
    }
}

string
urldecode(string const & in, origin::type made_from)
{
  string out;

  for (string::const_iterator i = in.begin(); i != in.end(); ++i)
    {
      if (*i == '+')
        out += ' ';
      else if (*i != '%')
        out += *i;
      else
        {
          char d1, d2;
          ++i;
          E(i != in.end(), made_from, F("bad URLencoded string '%s'") % in);
          d1 = *i;
          ++i;
          E(i != in.end(), made_from, F("bad URLencoded string '%s'") % in);
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
            default: E(false, made_from, F("bad URLencoded string '%s'") % in);
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
            default: E(false, made_from, F("bad URLencoded string '%s'") % in);
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
