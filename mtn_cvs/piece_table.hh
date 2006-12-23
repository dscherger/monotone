// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// copyright (C) 2005 christof petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>

// piece table stuff

/* 
 * this code efficiently constructs a new revision of a file by breaking
 * it into lines and applying a rcs diff
 */

struct 
piece
{
  piece(std::string::size_type p, std::string::size_type l, unsigned long id) :
    pos(p), len(l), string_id(id) {}
  std::string::size_type pos;
  std::string::size_type len;
  unsigned long string_id;
  std::string operator*() const;
  
  typedef std::vector<piece> piece_table;

  struct piece_store;  
  static piece_store global_pieces;

  static void index_deltatext(std::string const & dt, piece_table & pieces);
  static void build_string(piece_table const & pieces, std::string & out);
  static void apply_diff(piece_table const & source_lines,
                  piece_table & dest_lines,
                  std::string const & deltatext);

  // free allocated storage (invalidates all existing pieces)
  static void reset();
};

