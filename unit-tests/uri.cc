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
test_one_uri(string full,
             string scheme,
             string user,
             string host,
             string port,
             string path,
             string query,
             string fragment)
{
  L(FL("testing parse of URI '%s'") % full);
  uri_t uri;
  UNIT_TEST_CHECK_NOT_THROW(parse_uri(full, uri, origin::user), recoverable_failure);
  UNIT_TEST_CHECK(uri.scheme == scheme);
  UNIT_TEST_CHECK(uri.user == user);
  UNIT_TEST_CHECK(uri.host == host);
  UNIT_TEST_CHECK(uri.port == port);
  UNIT_TEST_CHECK(uri.path == path);
  UNIT_TEST_CHECK(uri.query == query);
  UNIT_TEST_CHECK(uri.fragment == fragment);
}

UNIT_TEST(basic)
{
  test_one_uri("ssh://graydon@venge.net:22/tmp/foo.mtn",
               "ssh", "graydon", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh://graydon@venge.net/tmp/foo.mtn",
               "ssh", "graydon", "venge.net", "", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh://venge.net:22/tmp/foo.mtn",
               "ssh", "", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("ssh://venge.net/tmp/foo.mtn",
               "ssh", "", "venge.net", "", "/tmp/foo.mtn", "", "");
  // the parser does not know the inner characteristics of the 'ssh' scheme,
  // i.e. that a tilde isn't expanded when its inside a path, so we treat this
  // as correct in this place. the leading slash is then however stripped off
  // in get_netsync_connect_command in std_hooks.lua so that ssh will handle
  // it properly
  test_one_uri("ssh://graydon@venge.net/~/foo.mtn",
               "ssh", "graydon", "venge.net", "",   "/~/foo.mtn", "", "");
  test_one_uri("ssh://[fe:00:01::04:21]/tmp/foo.mtn",
               "ssh", "", "fe:00:01::04:21", "", "/tmp/foo.mtn", "", "");
  test_one_uri("file:///tmp/foo.mtn",
               "file", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("file://tmp/foo.mtn",
               "file", "", "tmp", "", "/foo.mtn", "", "");
  test_one_uri("file:/tmp/foo.mtn",
               "file", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("file:tmp/foo.mtn",
               "file", "", "", "", "tmp/foo.mtn", "", "");
  test_one_uri("file:foo.mtn",
               "file", "", "", "", "foo.mtn", "", "");
  test_one_uri("/tmp/foo.mtn",
               "", "", "", "", "/tmp/foo.mtn", "", "");
  test_one_uri("http://graydon@venge.net:8080/foo.cgi?branch=foo#tip",
               "http", "graydon", "venge.net", "8080", "/foo.cgi", "branch=foo", "tip");
  test_one_uri("http://graydon@192.168.0.104:8080/foo.cgi?branch=foo#tip",
               "http", "graydon", "192.168.0.104", "8080", "/foo.cgi", "branch=foo", "tip");
  test_one_uri("http://graydon@[fe:00:01::04:21]:8080/foo.cgi?branch=foo#tip",
               "http", "graydon", "fe:00:01::04:21", "8080", "/foo.cgi", "branch=foo", "tip");
}

UNIT_TEST(bizarre)
{
  test_one_uri("venge.net",
               "", "", "venge.net", "", "", "", "");
  test_one_uri("venge.net:4692",
               "", "", "venge.net", "4692", "", "", "");
  test_one_uri("venge.net/monotone",
               "", "", "venge.net", "", "/monotone", "", "");
  test_one_uri("venge.net:4692/monotone",
               "", "", "venge.net", "4692", "/monotone", "", "");
  test_one_uri("graydon@venge.net:22/tmp/foo.mtn",
               "", "graydon", "venge.net", "22", "/tmp/foo.mtn", "", "");
  test_one_uri("file:///graydon@venge.net:22/tmp/foo.mtn",
               "file", "", "", "", "/graydon@venge.net:22/tmp/foo.mtn", "", "");
  test_one_uri("ssh://graydon@venge.net:22//tmp/foo.mtn",
               "ssh", "graydon", "venge.net", "22", "//tmp/foo.mtn", "", "");
  test_one_uri("mTn://coDe.monoTone.ca/monotone",
               "mtn", "", "code.monotone.ca", "", "/monotone", "", "");
  test_one_uri("MTN://[FE:00:01::04:21]/monoTone?NET.VENGE.MONOTONE{,.*}",
               "mtn", "", "fe:00:01::04:21", "", "/monoTone", "NET.VENGE.MONOTONE{,.*}", "");
  test_one_uri("http://venge.net/foo+bar%20baz?bla%20bla#foo+bar%20baz",
               "http", "", "venge.net", "", "/foo bar baz", "bla%20bla", "foo bar baz");
  test_one_uri("mtn://venge.net:/monotone",
               "mtn", "", "venge.net", "", "/monotone", "", "");
}

UNIT_TEST(invalid)
{
  uri_t uri;
  UNIT_TEST_CHECK_THROW(parse_uri("http://[f3:03:21/foo/bar", uri, origin::internal), unrecoverable_failure);
  UNIT_TEST_CHECK_THROW(parse_uri("http://example.com:1a4/foo/bar", uri, origin::user), recoverable_failure);
}

UNIT_TEST(urldecode)
{
  UNIT_TEST_CHECK(urldecode("foo%20bar", origin::internal) == "foo bar");
  UNIT_TEST_CHECK(urldecode("foo+bar", origin::internal) == "foo bar");
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
