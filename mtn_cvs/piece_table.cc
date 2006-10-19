// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// copyright (C) 2005 christof petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "piece_table.hh"
#include "sanity.hh"

using namespace std;

struct 
piece::piece_store
{
  std::vector< std::string > texts;
  void index_deltatext(std::string const & dt,
                       std::vector<piece> & pieces);
  void build_string(std::vector<piece> const & pieces,
                    std::string & out);
  void reset() { texts.clear(); }
};

piece::piece_store piece::global_pieces;

string
piece::operator*() const
{
  return string(global_pieces.texts.at(string_id).data() + pos, len);
}

void 
piece::piece_store::build_string(piece_table const & pieces,
                          string & out)
{
  out.clear();
  out.reserve(pieces.size() * 60);
  for(piece_table::const_iterator i = pieces.begin();
      i != pieces.end(); ++i)
    out.append(texts.at(i->string_id), i->pos, i->len);
}

void 
piece::piece_store::index_deltatext(std::string const & dt,
                             piece_table & pieces)
{
  pieces.clear();
  pieces.reserve(dt.size() / 30);  
  texts.push_back(dt);
  unsigned long id = texts.size() - 1;
  string::size_type begin = 0;
  string::size_type end = dt.find('\n');
  while(end != string::npos)
    {
      // nb: the piece includes the '\n'
      pieces.push_back(piece(begin, (end - begin) + 1, id));
      begin = end + 1;
      end = dt.find('\n', begin);
    }
  if (begin != dt.size())
    {
      // the text didn't end with '\n', so neither does the piece
      end = dt.size();
      pieces.push_back(piece(begin, end - begin, id));
    }
}

static void 
process_one_hunk(piece::piece_table const & source,
                 piece::piece_table & dest,
                 piece::piece_table::const_iterator & i,
                 int & cursor)
{
  string directive = **i;
  assert(directive.size() > 1);
  ++i;

  try 
    {
      char code;
      int pos, len;
      if (sscanf(directive.c_str(), " %c %d %d", &code, &pos, &len) != 3)
              E(false, F("illformed directive '%s'\n") % directive);

      if (code == 'a')
        {
          // 'ax y' means "copy from source to dest until cursor == x, then
          // copy y lines from delta, leaving cursor where it is"
          while (cursor < pos)
            dest.push_back(source.at(cursor++));
          I(cursor == pos);
          while (len--)
            dest.push_back(*i++);
        }
      else if (code == 'd')
        {      
          // 'dx y' means "copy from source to dest until cursor == x-1,
          // then increment cursor by y, ignoring those y lines"
          while (cursor < (pos - 1))
            dest.push_back(source.at(cursor++));
          I(cursor == pos - 1);
          cursor += len;
        }
      else
        E(false,F("unknown directive '%s'\n") % directive);
    } 
  catch (std::out_of_range & oor)
    {
      E(false, F("out_of_range while processing '%s' with source.size() == %d and cursor == %d")
          % directive % source.size() % cursor);
    }  
}

void
piece::apply_diff(piece_table const & source_lines,
                  piece_table & dest_lines,
                  std::string const & deltatext)
{
  dest_lines.clear();
  dest_lines.reserve(source_lines.size());

  piece_table deltalines;
  global_pieces.index_deltatext(deltatext, deltalines);
  
  int cursor = 0;
  for (piece_table::const_iterator i = deltalines.begin(); 
       i != deltalines.end(); )
    process_one_hunk(source_lines, dest_lines, i, cursor);
  while (cursor < static_cast<int>(source_lines.size()))
    dest_lines.push_back(source_lines[cursor++]);
}

void
piece::reset()
{ 
  global_pieces.reset();
}

void
piece::index_deltatext(std::string const & dt, piece_table & pieces)
{
  global_pieces.index_deltatext(dt,pieces);
}

void
piece::build_string(piece_table const & pieces, std::string & out)
{
  global_pieces.build_string(pieces, out);
}
