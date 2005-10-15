// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

// Shows three pieces of text, in three notebook pages.
// Generally, these will be the contents of a file, the diff on that file
// for  the revision of interest, and any comments on that file.
// The "File conents" and "File diff" pages are read-only.
// The "File comment" page shows a vector of strings, and is editable.
// Each string provided in the vector is shown in yellow, followed by
// a red bar (These cannot be edited). Edits placed after the end of these
// are available with get_comment.

// TODO: tag the diff with colors and links into the file
// maybe color edited parts of the file, with links into the diff

#ifndef __REV_FILE_INFO_HH__
#define __REV_FILE_INFO_HH__

#include <gtkmm.h>

#include <vector>

class rev_file_info: public Gtk::Notebook
{
  Gtk::TextView fcomment;
  Gtk::TextView fcontents;
  Gtk::TextView fdiff;
  Gtk::ScrolledWindow fcommwin;
  Gtk::ScrolledWindow fcontwin;
  Gtk::ScrolledWindow fdiffwin;
  Glib::RefPtr<Gtk::TextMark> commentend;
  Glib::RefPtr<Gtk::TextTag> current;

  void diff_clicked(GdkEventButton *b);
  Glib::RefPtr<Gtk::TextTag>
   hypertag(Glib::RefPtr<Gtk::TextBuffer> b, int line, int eline);
  bool tag_event(Glib::RefPtr<Glib::Object> const &p,
                   GdkEvent *e, Gtk::TextIter const &it,
                   int line, int eline);
  void link_tag_to(Glib::RefPtr<Gtk::TextTag> &t, int line, int eline);
public:
  rev_file_info();

  void set_contents(Glib::ustring const & s);
  void set_diff(Glib::ustring const & s);
  void set_comment(std::vector<Glib::ustring> const & s,
                   Glib::ustring const & e);
  Glib::ustring get_comment();
  void clear_comment();
};

#endif
