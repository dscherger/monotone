#ifndef __MISC_HH__
#define __MISC_HH__

#include <gtkmm.h>

#include <vector>
#include <string>

std::string readfile(std::string const & f);

class chooser: public Gtk::Dialog
{
  struct cols: public Gtk::TreeModel::ColumnRecord
  {
    Gtk::TreeModelColumn<Glib::ustring> name;
    cols() {add(name);}
  };
  cols col;
  Glib::RefPtr<Gtk::ListStore> store;
  Gtk::TreeView view;
public:
  chooser(std::vector<std::string> const & options);
  std::string result();
};

#endif
