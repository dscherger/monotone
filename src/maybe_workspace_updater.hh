// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.
#ifndef __MAYBE_WORKSPACE_UPDATER_HH__
#define __MAYBE_WORKSPACE_UPDATER_HH__

class app_state;
class project_t;

class maybe_workspace_updater
{
  bool can_do_update;
  app_state & app;
  project_t & project;
public:
  maybe_workspace_updater(app_state & app, project_t & project);
  void maybe_do_update();
};

#endif
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
