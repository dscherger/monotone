#ifndef __GIT_EXPORT_HH__
#define __GIT_EXPORT_HH__

// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

struct file_delete
{
    file_path path;
    file_delete(file_path path) : 
        path(path) {}
};

struct file_rename
{
    file_path old_path;
    file_path new_path;
    file_rename(file_path old_path, file_path new_path) : 
        old_path(old_path), new_path(new_path) {}
};

struct file_add
{
    file_path path;
    file_id content;
    std::string mode;
    file_add(file_path path, file_id content, std::string mode) :
        path(path), content(content), mode(mode) {}
};

struct file_changes
{
    std::vector<file_delete> deletions;
    std::vector<file_rename> renames;
    std::vector<file_add> additions;
};


void read_mappings(system_path const & path, 
                   std::map<std::string, std::string> & mappings);

void import_marks(system_path const & marks_file, 
                  std::map<revision_id, size_t> & marked_revs);

void export_marks(system_path const & marks_file, 
                  std::map<revision_id, size_t> const & marked_revs);

void load_changes(database & db,
                  std::vector<revision_id> const & revisions, 
                  std::map<revision_id, file_changes> & change_map);

void export_changes(database & db,
                    std::vector<revision_id> const & revisions, 
                    std::map<revision_id, size_t> & marked_revs,
                    std::map<std::string, std::string> const & author_map,
                    std::map<std::string, std::string> const & branch_map,
                    std::map<revision_id, file_changes> const & change_map,
                    bool log_revids, bool log_certs);
 
void export_rev_refs(std::vector<revision_id> const & revisions,
                     std::map<revision_id, size_t> & marked_revs);

void export_root_refs(database & db,
                     std::map<revision_id, size_t> & marked_revs);

void export_leaf_refs(database & db,
                     std::map<revision_id, size_t> & marked_revs);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __RCS_IMPORT_HH__
