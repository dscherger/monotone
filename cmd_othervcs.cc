// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <fstream>

#include "cmd.hh"
#include "app_state.hh"
#include "rcs_import.hh"
#include "svn_import.hh"

using std::vector;
using std::cin;
using std::ifstream;

CMD(rcs_import, N_("debug"), N_("RCSFILE..."),
    N_("parse versions in RCS files\n"
       "this command doesn't reconstruct or import revisions."
       "you probably want cvs_import"),
    options::opts::branch)
{
  if (args.size() < 1)
    throw usage(name);

  for (vector<utf8>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      test_parse_rcs_file(system_path((*i)()), app.db);
    }
}


CMD(cvs_import, N_("rcs"), N_("CVSROOT"), 
    N_("import all versions in CVS repository"),
    options::opts::branch)
{
  if (args.size() != 1)
    throw usage(name);

  import_cvs_repo(system_path(idx(args, 0)()), app);
}


CMD(svn_import, N_("debug"), N_("[SVNDUMP]"),
    N_("import subversion repositories"),
    options::opts::branch)
{
  if (args.size() > 1)
    throw usage(name);

  if (args.empty())
    {
      import_svn_repo(cin, app);
    }
  else
    {
      system_path in_file = system_path(idx(args, 0));

      N(!file_exists(in_file),
        F("File %s does not exist.") % in_file);

      ifstream ifs(in_file.as_external().c_str());
      import_svn_repo(ifs, app);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
