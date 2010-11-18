// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "maybe_workspace_updater.hh"

#include "app_state.hh"
#include "option.hh"
#include "project.hh"
#include "revision.hh"
#include "work.hh"

// defined in cmd_merging.cc
void update(app_state & app, args_vector const & args);

namespace {
  enum updatability { is_head, is_not_head, not_updatable };
  updatability get_updatability(app_state & app, project_t & project)
  {
    if (!workspace::found)
      return not_updatable;
    workspace work(app);
    revision_t rev;
    work.get_work_rev(rev);
    if (rev.edges.size() != 1)
      return not_updatable;
    options workspace_opts;
    work.get_options(workspace_opts);
    std::set<revision_id> heads;
    project.get_branch_heads(workspace_opts.branch, heads, false);
    
    revision_id parent = edge_old_revision(rev.edges.begin());
    if (heads.find(parent) != heads.end())
      return is_head;
    else
      return is_not_head;
  }
}

maybe_workspace_updater::maybe_workspace_updater(app_state & app,
                                                 project_t & project)
  : can_do_update(app.opts.auto_update),
    app(app), project(project)
{
  if (can_do_update)
    can_do_update = (get_updatability(app, project) == is_head);
}

void maybe_workspace_updater::maybe_do_update()
{
  if (can_do_update && (get_updatability(app, project) == is_not_head))
    {
      update(app, args_vector());
    }
  else
    {
      P(F("note: your workspace has not been updated"));
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
