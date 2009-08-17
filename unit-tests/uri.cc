// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "uri.hh"

using std::string;

static void
test_one_uri(string scheme,
             string user,
             string ipv6_host,
             string normal_host,
             string port,
             string path,
             string query,
             string fragment)
{
  string built;

  if (!scheme.empty())
    built += scheme + ':';

  string host;

  if (! ipv6_host.empty())
    {
      I(normal_host.empty());
      host += '[';
      host += (ipv6_host + ']');
    }
  else
    host = normal_host;

  if (! (user.empty()
         && host.empty()
         && port.empty()))
    {
      built += "//";

      if (! user.empty())
        built += (user + '@');

      if (! host.empty())
        built += host;

      if (! port.empty())
        {
          built += ':';
          built += port;
        }
    }

  if (! path.empty())
    {
      I(path[0] == '/');
      built += path;
    }

  if (! query.empty())
    {
      built += '?';
      built += query;
    }

  if (! fragment.empty())
    {
      built += '#';
      built += fragment;
    }

  L(FL("testing parse of URI '%s'") % built);
  uri_t uri;
  UNIT_TEST_CHECK_NOT_THROW(parse_uri(built, uri, origin::user), recoverable_failure);
  UNIT_TEST_CHECK(uri.scheme == scheme);
  UNIT_TEST_CHECK(uri.user == user);
  if (!normal_host.empty())
    UNIT_TEST_CHECK(uri.host == normal_host);
  else
    UNIT_TEST_CHECK(uri.host == ipv6_host);
  UNIT_TEST_CHECK(uri.port == port);
  UNIT_TEST_CHECK(uri.path == path);
  UNIT_TEST_CHECK(uri.query == query);
  UNIT_TEST_CHECK(uri.fragment == fragment);
}

UNIT_TEST(basic)
{
  test_one_uri("ssh", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "graydon", "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "", "venge.net", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "",        "fe:00:01::04:21", "", "",   "/tmp/foo.mtn", "", "");
  test_one_uri("file", "",       "", "",          "",   "/tmp/foo.mtn", "", "");
  test_one_uri("", "", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("http", "graydon", "", "venge.net", "8080", "/foo.cgi", "branch=foo", "tip");
  test_one_uri("http", "graydon", "", "192.168.0.104", "8080", "/foo.cgi", "branch=foo", "tip");
  test_one_uri("http", "graydon", "fe:00:01::04:21", "", "8080", "/foo.cgi", "branch=foo", "tip");
}

UNIT_TEST(bizarre)
{
  test_one_uri("", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("", "", "", "", "", "/graydon@venge.net:22/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "graydon", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh", "", "", "", "", "/graydon@venge.net:22/tmp/foo.mtn", "", "");
}

UNIT_TEST(invalid)
{
  uri_t uri;

  UNIT_TEST_CHECK_THROW(parse_uri("http://[f3:03:21/foo/bar", uri, origin::internal), unrecoverable_failure);
  UNIT_TEST_CHECK_THROW(parse_uri("http://example.com:/foo/bar", uri, origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(parse_uri("http://example.com:1a4/foo/bar", uri, origin::user), recoverable_failure);
}

UNIT_TEST(urldecode)
{
  UNIT_TEST_CHECK(urldecode("foo%20bar", origin::internal) == "foo bar");
  UNIT_TEST_CHECK(urldecode("%61", origin::user) == "a");
  UNIT_TEST_CHECK_THROW(urldecode("%xx", origin::internal), unrecoverable_failure);
  UNIT_TEST_CHECK_THROW(urldecode("%", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(urldecode("%5", origin::user), recoverable_failure);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
