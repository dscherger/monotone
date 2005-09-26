// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

// Most things go here.
// This is a list of files associated with a revision.
// If it's displaying a working copy, it supports changing the state of
// files (add, drop, rename, revert) by use of a context menu.

// When a file is selected, it puts associated data into the rev_file_info
// in its parent revdat.

#ifndef __REV_FILE_LIST_HH_
#define __REV_FILE_LIST_HH_

#include <gtkmm.h>

#include "monotone.hh"

class revdat;


class rev_file_list: public Gtk::VBox
{
  class psel: public Gtk::VBox
  {
    rev_file_list * rfl;
    Gtk::Label head;
    std::vector<Gtk::RadioButton*> buttons;
    Gtk::RadioButtonGroup bg;
    Gtk::HSeparator sep;
  public:
    psel(rev_file_list * r): rfl(r),
      head("Parent to determine changes against:")
    {
      pack_start(head);
      pack_end(sep);
    }
    ~psel()
    {
      for (std::vector<Gtk::RadioButton*>::iterator i = buttons.begin();
           i != buttons.end(); ++i)
        delete *i;
    }
    Glib::ustring get_label(int n)
    {
      return buttons[n]->get_label();
    }
    void set_parents(std::vector<Glib::ustring> const & p)
    {
      for (std::vector<Gtk::RadioButton*>::iterator i = buttons.begin();
           i != buttons.end(); ++i)
        remove(**i);
      buttons.clear();
      int n = 0;
      for (std::vector<Glib::ustring>::const_iterator i = p.begin();
           i != p.end(); ++i, ++n)
        {
          buttons.push_back(new Gtk::RadioButton(*i));
          buttons.back()->set_manage();
          buttons.back()->set_group(bg);
          buttons.back()->signal_clicked().connect(sigc::bind(
                            sigc::mem_fun(*rfl, &rev_file_list::pchange), n));
          pack_start(*buttons.back());
        }
      buttons.front()->set_active(true);
      show_all_children();
    }
  };

  struct cols: public Gtk::TreeModel::ColumnRecord
  {
    Gtk::TreeModelColumn<bool> changed;
    Gtk::TreeModelColumn<bool> included;
    Gtk::TreeModelColumn<Glib::ustring> status;
    Gtk::TreeModelColumn<Glib::ustring> name;
    cols() {add(changed); add(included); add(status); add(name);}
  };
  cols col;

  Gtk::CheckButton show_changed;
  Gtk::CheckButton show_unchanged;
  Gtk::HButtonBox p_buttons;
  Gtk::CheckButton show_ignored;
  Gtk::CheckButton show_unknown;
  Gtk::HButtonBox wc_buttons;
  psel parents;

  bool wc;
  Glib::RefPtr<Gtk::ListStore> filelist;
  Gtk::TreeView files;
  Gtk::ScrolledWindow filewin;
  std::vector<inventory_item> inventory;
  std::vector<std::vector<inventory_item> > pchanges;
  std::string parent;
  std::string rev;
  revdat *rd;
  std::vector<cert> certs;
  std::map<std::string, Glib::ustring> comments;
  std::string current_file;

  Gtk::Menu menu;
  Gtk::TreeModel::Row menurow;
  bool needscan;

  void menuadd();
  void menudrop();
  void menurename();
  void menuundrop();
  void menurevert();

  void drag_get(const Glib::RefPtr<Gdk::DragContext> & dc,
                Gtk::SelectionData& sel_data,
                guint a, guint b);

  void dosel(Gtk::TreeModel::Path const & p);
  bool selchanged(Glib::RefPtr<Gtk::TreeModel> const & model,
                  Gtk::TreeModel::Path const & path,
                  bool path_currently_selected);
  void selfile(Gtk::TreeModel::Path const & p, Gtk::TreeView::Column *c);
  void clicked(GdkEventButton *b);
  void set_menu(std::vector<bool> const & v);
public:
  rev_file_list(revdat *r);
  void set_wc(bool w);
  bool get_wc(){return wc;}
  void get_sel(std::vector<Glib::ustring> & inc,
               std::vector<Glib::ustring> & exc);
  void rescan();
  void pchange(int n);
  void set_parents(std::vector<Glib::ustring> const & pvec,
                   std::vector<std::vector<inventory_item> > const & pch);
  void set_rev(std::string const & r);
  std::string get_rev(){return rev;}
  void set_files(std::vector<inventory_item> const & f);
  void clear_comments();
  std::string get_current(){return current_file;}
  std::map<std::string, Glib::ustring> get_comments(){return comments;}
};

#endif
