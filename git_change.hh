// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __GIT_CHANGE_HH__
#define __GIT_CHANGE_HH__

#include "paths.hh"
#include "vocab.hh"

#include <vector>

typedef file_path git_delete;
typedef std::pair<file_path, file_path> git_rename;

struct git_add
{
    file_path path;
    file_id content;
    std::string mode;
    git_add(file_path path, file_id content, std::string mode) :
        path(path), content(content), mode(mode) {}
};

typedef std::vector<git_delete>::const_iterator delete_iterator;
typedef std::vector<git_rename>::const_iterator rename_iterator;
typedef std::vector<git_add>::const_iterator add_iterator;

struct git_change
{
    std::vector<git_delete> deletions;
    std::vector<git_rename> renames;
    std::vector<git_add> additions;
};

void get_change(roster_t const & left, roster_t const & right,
                git_change & change);

void reorder_renames(std::vector<git_rename> const & renames,
                     std::vector<git_rename> & reordered_renames);

#endif // __GIT_CHANGE_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
