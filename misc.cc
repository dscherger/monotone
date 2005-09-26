#include "misc.hh"
#include <fstream>
#include <iostream>

std::string readfile(std::string const & f)
{
  std::string out;
  std::ifstream is(f.c_str(), std::ios::binary);
  is.seekg(0, std::ios::end);
  int length = is.tellg();
  is.seekg(0, std::ios::beg);
  if (length != -1)
    {
      char *buffer = new char[length];
      is.read(buffer, length);
      out = std::string(buffer, length);
      delete[] buffer;
    }
  return out;
}

chooser::chooser(std::vector<std::string> const & options)
 : store(Gtk::ListStore::create(col)), view(store)
{
  view.append_column("Choose one...", col.name);
  get_vbox()->add(view);
  view.show();
  for (std::vector<std::string>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      Gtk::ListStore::Row row = *store->append();
      row[col.name] = *i;
    }
  add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  add_button("Select", Gtk::RESPONSE_OK);
}

std::string
chooser::result()
{
  Gtk::TreeModel::Path p;
  Gtk::TreeViewColumn *c;
  view.get_cursor(p, c);
  Gtk::TreeModel::Row r = *store->get_iter(p);
  return Glib::ustring(r[col.name]);
}
