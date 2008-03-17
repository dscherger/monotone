#ifndef __JSON_MSGS_HH__
#define __JSON_MSGS_HH__

// Copyright (C) 2008 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file holds all the JSON message structuring and destructuring
// facilities. It's used by the client and server sides of any
// JSON-speaking bulk I/O interface, to avoid cluttering them up and
// ensure that both sides of an interface make common assumptions
// about message structure.

#include "base.hh"
#include "graph.hh"
#include "json_io.hh"
#include "revision.hh"
#include "vocab.hh"

json_io::json_value_t encode_msg_error(std::string const & note);
bool decode_msg_error(json_io::json_value_t val, std::string & note);

// inquire

json_io::json_value_t encode_msg_inquire_request(std::set<revision_id> const & revs);
bool decode_msg_inquire_request(json_io::json_value_t val,
                                std::set<revision_id> & revs);
json_io::json_value_t encode_msg_inquire_response(std::set<revision_id> const & revs);
bool decode_msg_inquire_response(json_io::json_value_t val,
                                 std::set<revision_id> & revs);

// descendants

json_io::json_value_t encode_msg_descendants_request(std::set<revision_id> const & revs);
bool decode_msg_descendants_request(json_io::json_value_t val,
                                    std::set<revision_id> & revs);
json_io::json_value_t encode_msg_descendants_response(std::vector<revision_id> const & revs);
bool decode_msg_descendants_response(json_io::json_value_t val,
                                     std::vector<revision_id> & revs);

// revs

json_io::json_value_t encode_msg_get_rev_request(revision_id const & rid);
bool decode_msg_get_rev_request(json_io::json_value_t val,
                                revision_id & rid);
json_io::json_value_t encode_msg_get_rev_response(revision_t const & rev);
bool decode_msg_get_rev_response(json_io::json_value_t val,
                                 revision_t & rev);

json_io::json_value_t encode_msg_put_rev_request(revision_id const & rid,
                                                 revision_t const & rev);
bool decode_msg_put_rev_request(json_io::json_value_t val,
                                revision_id & rid,
                                revision_t & rev);
json_io::json_value_t encode_msg_put_rev_response();
bool decode_msg_put_rev_response(json_io::json_value_t val);


// files

json_io::json_value_t encode_msg_get_file_data(file_id const & fid);
bool decode_msg_get_file_data(json_io::json_value_t val, file_id & fid);

json_io::json_value_t encode_msg_get_file_delta(file_id const & src_id,
                                                file_id const & dst_id);
bool decode_msg_get_file_delta(json_io::json_value_t val,
                               file_id & src_id,
                               file_id & dst_id);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __JSON_MSGS_HH__
