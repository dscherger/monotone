#include "misc.hh"
#include "monotone.hh"
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

namespace {
  SyncDialog *sd;
  void sd_lwcb()
  {
    int r = sd->output.rfind("\r");
    int n = sd->output.rfind("\n", r);
    if (r != string::npos && n != string::npos)
      sd->output = sd->output.substr(0, n+1) + sd->output.substr(r+1);
    Glib::RefPtr<Gtk::TextBuffer> b = sd->tv.get_buffer();
    b->set_text(sd->output);
    Glib::RefPtr<Gtk::TextTag> t = b->create_tag();
    t->property_family() = "monospace";
    b->apply_tag(t, b->begin(), b->end());
    while (Gtk::Main::events_pending())
      Gtk::Main::iteration();
  }
};

// mtn->sync() is called from a timeout so that we'll return first,
// and it will be called from the event loop, *after* our window exists,
// and can continue to run the event loop itself.
SyncDialog::SyncDialog(monotone & m)
 : mtn(&m), prev_lwcb(m.get_longwait_callback())
{
  get_vbox()->add(tv);
  tv.set_editable(false);
  get_vbox()->show_all_children();
  cancelbtn = add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  okbtn = add_button("Done", Gtk::RESPONSE_OK);
  okbtn->set_sensitive(false);
  sd = this;
  mtn->set_longwait_callback(sd_lwcb);
  Glib::signal_timeout().connect(sigc::mem_fun(this, &SyncDialog::timer), 0);
}

SyncDialog::~SyncDialog()
{
  mtn->set_longwait_callback(prev_lwcb);
}

bool SyncDialog::timer()
{
  mtn->sync(output);
  okbtn->set_sensitive(true);
  cancelbtn->set_sensitive(false);
  return false;
}
