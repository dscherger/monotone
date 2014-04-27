// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/netcmd.hh"
#include "../../../src/transforms.hh"
#include "../../../src/lexical_cast.hh"

using std::string;

UNIT_TEST(mac)
{
  netcmd out_cmd(constants::netcmd_current_protocol_version);
  netcmd in_cmd(constants::netcmd_current_protocol_version);
  string buf;
  netsync_session_key key(constants::netsync_key_initializer);
  {
    chained_hmac mac(key, true);
    // mutates mac
    out_cmd.write(buf, mac);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  buf[0] ^= 0xff;
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  buf[buf.size() - 1] ^= 0xff;
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }

  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  buf += '\0';
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK_THROW(in_cmd.read_string(buf, mac), bad_decode);
  }
}

static void
do_netcmd_roundtrip(netcmd const & out_cmd, netcmd & in_cmd, string & buf)
{
  netsync_session_key key(constants::netsync_key_initializer);
  {
    chained_hmac mac(key, true);
    out_cmd.write(buf, mac);
  }
  {
    chained_hmac mac(key, true);
    UNIT_TEST_CHECK(in_cmd.read_string(buf, mac));
  }
  UNIT_TEST_CHECK(in_cmd == out_cmd);
}

UNIT_TEST(functions)
{

  try
    {

      // error_cmd
      {
        L(FL("checking i/o round trip on error_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        string out_errmsg("your shoelaces are untied"), in_errmsg;
        string buf;
        out_cmd.write_error_cmd(out_errmsg);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_error_cmd(in_errmsg);
        UNIT_TEST_CHECK(in_errmsg == out_errmsg);
        L(FL("errmsg_cmd test done, buffer was %d bytes") % buf.size());
      }

      // hello_cmd
      {
        L(FL("checking i/o round trip on hello_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(0);
        string buf;
        key_name out_server_keyname("server@there"), in_server_keyname;
        rsa_pub_key out_server_key("9387938749238792874"), in_server_key;
        id out_nonce(raw_sha1("nonce it up"), origin::internal), in_nonce;
        out_cmd.write_hello_cmd(out_server_keyname, out_server_key, out_nonce);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        u8 ver(0);
        in_cmd.read_hello_cmd(ver, in_server_keyname, in_server_key, in_nonce);
        UNIT_TEST_CHECK(ver == constants::netcmd_current_protocol_version);
        UNIT_TEST_CHECK(in_server_keyname == out_server_keyname);
        UNIT_TEST_CHECK(in_server_key == out_server_key);
        UNIT_TEST_CHECK(in_nonce == out_nonce);
        L(FL("hello_cmd test done, buffer was %d bytes") % buf.size());
      }

      // bye_cmd
      {
        L(FL("checking i/o round trip on bye_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        u8 out_phase(1), in_phase;
        string buf;

        out_cmd.write_bye_cmd(out_phase);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_bye_cmd(in_phase);
        UNIT_TEST_CHECK(in_phase == out_phase);
        L(FL("bye_cmd test done, buffer was %d bytes") % buf.size());
      }

      // anonymous_cmd
      {
        L(FL("checking i/o round trip on anonymous_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        protocol_role out_role = source_and_sink_role, in_role;
        string buf;
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        globish out_include_pattern("radishes galore!", origin::internal),
          in_include_pattern;
        globish out_exclude_pattern("turnips galore!", origin::internal),
          in_exclude_pattern;

        out_cmd.write_anonymous_cmd(out_role, out_include_pattern, out_exclude_pattern, out_key);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_anonymous_cmd(in_role, in_include_pattern, in_exclude_pattern, in_key);
        UNIT_TEST_CHECK(in_key == out_key);
        UNIT_TEST_CHECK(in_include_pattern() == out_include_pattern());
        UNIT_TEST_CHECK(in_exclude_pattern() == out_exclude_pattern());
        UNIT_TEST_CHECK(in_role == out_role);
        L(FL("anonymous_cmd test done, buffer was %d bytes") % buf.size());
      }

      // auth_cmd
      {
        L(FL("checking i/o round trip on auth_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        protocol_role out_role = source_and_sink_role, in_role;
        string buf;
        key_id out_client(raw_sha1("happy client day"), origin::internal);
        id out_nonce1(raw_sha1("nonce me amadeus"), origin::internal);
        key_id in_client;
        id in_nonce1;
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        rsa_sha1_signature out_signature(raw_sha1("burble") + raw_sha1("gorby"),
                                         origin::internal), in_signature;
        globish out_include_pattern("radishes galore!", origin::user),
          in_include_pattern;
        globish out_exclude_pattern("turnips galore!", origin::user),
          in_exclude_pattern;

        out_cmd.write_auth_cmd(out_role, out_include_pattern, out_exclude_pattern
                               , out_client, out_nonce1, out_key, out_signature);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_auth_cmd(in_role, in_include_pattern, in_exclude_pattern,
                             in_client, in_nonce1, in_key, in_signature);
        UNIT_TEST_CHECK(in_client == out_client);
        UNIT_TEST_CHECK(in_nonce1 == out_nonce1);
        UNIT_TEST_CHECK(in_key == out_key);
        UNIT_TEST_CHECK(in_signature == out_signature);
        UNIT_TEST_CHECK(in_role == out_role);
        UNIT_TEST_CHECK(in_include_pattern() == out_include_pattern());
        UNIT_TEST_CHECK(in_exclude_pattern() == out_exclude_pattern());
        L(FL("auth_cmd test done, buffer was %d bytes") % buf.size());
      }

      // automate_cmd
      {
        L(FL("checking i/o round trip on auth_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        string buf;
        key_id out_client(raw_sha1("happy client day"), origin::internal);
        id out_nonce1(raw_sha1("nonce me amadeus"), origin::internal);
        key_id in_client;
        id in_nonce1;
        // total cheat, since we don't actually verify that rsa_oaep_sha_data
        // is sensible anywhere here...
        rsa_oaep_sha_data out_key("nonce start my heart"), in_key;
        rsa_sha1_signature out_signature(raw_sha1("burble") + raw_sha1("gorby"),
                                         origin::internal), in_signature;

        out_cmd.write_automate_cmd(out_client, out_nonce1, out_key, out_signature);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_automate_cmd(in_client, in_nonce1, in_key, in_signature);
        UNIT_TEST_CHECK(in_client == out_client);
        UNIT_TEST_CHECK(in_nonce1 == out_nonce1);
        UNIT_TEST_CHECK(in_key == out_key);
        UNIT_TEST_CHECK(in_signature == out_signature);
        L(FL("automate_cmd test done, buffer was %d bytes") % buf.size());
      }

      // confirm_cmd
      {
        L(FL("checking i/o round trip on confirm_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        string buf;
        out_cmd.write_confirm_cmd();
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_confirm_cmd();
        L(FL("confirm_cmd test done, buffer was %d bytes") % buf.size());
      }

      // refine_cmd
      {
        L(FL("checking i/o round trip on refine_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        string buf;
        refinement_type out_ty (refinement_query), in_ty(refinement_response);
        merkle_node out_node, in_node;

        out_node.set_raw_slot(0, id(raw_sha1("The police pulled Kris Kringle over"), origin::internal));
        out_node.set_raw_slot(3, id(raw_sha1("Kris Kringle tried to escape from the police"), origin::internal));
        out_node.set_raw_slot(8, id(raw_sha1("He was arrested for auto theft"), origin::internal));
        out_node.set_raw_slot(15, id(raw_sha1("He was whisked away to jail"), origin::internal));
        out_node.set_slot_state(0, subtree_state);
        out_node.set_slot_state(3, leaf_state);
        out_node.set_slot_state(8, leaf_state);
        out_node.set_slot_state(15, subtree_state);

        out_cmd.write_refine_cmd(out_ty, out_node);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_refine_cmd(in_ty, in_node);
        UNIT_TEST_CHECK(in_ty == out_ty);
        UNIT_TEST_CHECK(in_node == out_node);
        L(FL("refine_cmd test done, buffer was %d bytes") % buf.size());
      }

      // done_cmd
      {
        L(FL("checking i/o round trip on done_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        size_t out_n_items(12), in_n_items(0);
        netcmd_item_type out_type(key_item), in_type(revision_item);
        string buf;

        out_cmd.write_done_cmd(out_type, out_n_items);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_done_cmd(in_type, in_n_items);
        UNIT_TEST_CHECK(in_n_items == out_n_items);
        UNIT_TEST_CHECK(in_type == out_type);
        L(FL("done_cmd test done, buffer was %d bytes") % buf.size());
      }

      // data_cmd
      {
        L(FL("checking i/o round trip on data_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_id(raw_sha1("tuna is not yummy"), origin::internal), in_id;
        string out_dat("thank you for flying northwest"), in_dat;
        string buf;
        out_cmd.write_data_cmd(out_type, out_id, out_dat);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_data_cmd(in_type, in_id, in_dat);
        UNIT_TEST_CHECK(in_id == out_id);
        UNIT_TEST_CHECK(in_dat == out_dat);
        L(FL("data_cmd test done, buffer was %d bytes") % buf.size());
      }

      // delta_cmd
      {
        L(FL("checking i/o round trip on delta_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);
        netcmd_item_type out_type(file_item), in_type(key_item);
        id out_head(raw_sha1("your seat cusion can be reused"), origin::internal), in_head;
        id out_base(raw_sha1("as a floatation device"), origin::internal), in_base;
        delta out_delta("goodness, this is not an xdelta"), in_delta;
        string buf;

        out_cmd.write_delta_cmd(out_type, out_head, out_base, out_delta);
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_delta_cmd(in_type, in_head, in_base, in_delta);
        UNIT_TEST_CHECK(in_type == out_type);
        UNIT_TEST_CHECK(in_head == out_head);
        UNIT_TEST_CHECK(in_base == out_base);
        UNIT_TEST_CHECK(in_delta == out_delta);
        L(FL("delta_cmd test done, buffer was %d bytes") % buf.size());
      }

      // automate_command_cmd
      {
        L(FL("checking i/o round trip on automate_command_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);

        std::vector<string> in_args, out_args;
        std::vector<std::pair<string, string> > in_opts, out_opts;

        in_args.push_back("foo");
        in_args.push_back("bar");
        in_opts.push_back(std::make_pair("abc", "def"));

        out_cmd.write_automate_command_cmd(in_args, in_opts);
        string buf;
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_automate_command_cmd(out_args, out_opts);

        UNIT_TEST_CHECK(in_args == out_args);
        UNIT_TEST_CHECK(in_opts == out_opts);
        L(FL("automate_command_cmd test done, buffer was %d bytes") % buf.size());
      }

      // automate_packet_cmd
      {
        L(FL("checking i/o round trip on automate_packet_cmd"));
        netcmd out_cmd(constants::netcmd_current_protocol_version);
        netcmd in_cmd(constants::netcmd_current_protocol_version);

        int in_cmd_num(3), out_cmd_num;
        char in_stream('k'), out_stream;
        string in_data("this is some packet data"), out_data;

        out_cmd.write_automate_packet_cmd(in_cmd_num, in_stream, in_data);
        string buf;
        do_netcmd_roundtrip(out_cmd, in_cmd, buf);
        in_cmd.read_automate_packet_cmd(out_cmd_num, out_stream, out_data);

        UNIT_TEST_CHECK(in_cmd_num == out_cmd_num);
        UNIT_TEST_CHECK(in_stream == out_stream);
        UNIT_TEST_CHECK(in_data == out_data);
        L(FL("automate_packet_cmd test done, buffer was %d bytes") % buf.size());
      }

    }
  catch (bad_decode & d)
    {
      L(FL("bad decode exception: '%s'") % d.what);
      throw;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
