// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#ifndef __AUTOMATE_READER_HH__
#define __AUTOMATE_READER_HH__

#include <vector>

class automate_reader
{
  std::istream & in;
  enum location {opt, cmd, none, eof};
  location loc;
  bool get_string(std::string & out);
  std::streamsize read(char *buf, size_t nbytes, bool eof_ok = false);
  void go_to_next_item();
public:
  automate_reader(std::istream & is);
  bool get_command(std::vector<std::pair<std::string, std::string> > & params,
                   std::vector<std::string> & cmdline);
  void reset();
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
