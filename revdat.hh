// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

// This is the main widget. It shows a revision, either from the database
// or from a working copy. If showing a working copy, it can also make changes.

// If it's showing a working copy, 'commit' will commit that working copy.
// Also, regardless of whether its showing a working copy, commit will add file
// comments, in the form of file-comment certs attached to the revision.

#ifndef __REVDAT_HH__
#define __REVDAT_HH__

#include <gtkmm.h>

#include "rev_file_list.hh"
#include "rev_file_info.hh"

class revdat: public Gtk::HPaned
{
  std::string revision;
  rev_file_list rfl;
  rev_file_info rfi;
  monotone *mtn;
  Gtk::Window *window;
  friend class rev_file_list;
public:
  revdat(monotone *m, Gtk::Window *w);
  void commit();
  void loadwork();
  std::string get_rev(){return rfl.get_rev();}
  bool get_wc(){return rfl.get_wc();}
  void loadrev(std::string const & rev);
  void clear();
};

#endif
