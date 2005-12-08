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

// mtn->whatever() is called from a timeout so that we'll return first,
// and it will be called from the event loop, *after* our window exists,
// and can continue to run the event loop itself.
ProgressDialog::ProgressDialog(monotone & m)
 : mtn(&m)
{
  get_vbox()->add(tv);
  tv.set_editable(false);
  get_vbox()->show_all_children();
  cancelbtn = add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  okbtn = add_button("Done", Gtk::RESPONSE_OK);
  okbtn->set_sensitive(false);
  Glib::signal_timeout().connect(sigc::mem_fun(this, &ProgressDialog::timer), 0);
}

ProgressDialog::~ProgressDialog()
{
}

bool ProgressDialog::timer()
{
  callmtn();
  okbtn->set_sensitive(true);
  cancelbtn->set_sensitive(false);
  return false;
}

// Run the event loop while waiting for monotone to finish.
void ProgressDialog::do_wait(string & fin)
{
  while (mtn->is_busy())
    {
      for (int i = 0; i < 100 && mtn->is_busy()
                      && Gtk::Main::events_pending(); ++i)
        Gtk::Main::iteration();
      string & str(mtn->output_err);
      if (str.size() == 0)
        continue;

      // This gets rid of lines hidden by \r .
      int r = str.rfind("\r");
      int n = str.rfind("\n", r);
      if (r != string::npos && n != string::npos)
        str = str.substr(0, n+1) + str.substr(r+1);

      Glib::RefPtr<Gtk::TextBuffer> b = tv.get_buffer();
      b->set_text(str);
      Glib::RefPtr<Gtk::TextTag> t = b->create_tag();
      t->property_family() = "monospace";
      b->apply_tag(t, b->begin(), b->end());
    }
}

void SyncDialog::callmtn()
{
  std::cerr<<"frob\n";
  mtn->sync();
  std::cerr<<"friz\n";
  do_wait(output);
  std::cerr<<"franz\n";
}

void UpdateDialog::callmtn()
{
  std::vector<std::string> rr;
  mtn->update(rr, output);
  do_wait(output);
  if (!rr.empty())
    {
      chooser c(rr);
      int result = c.run();
      if (result == Gtk::RESPONSE_OK)
        {
          string rev = c.result();
          if (!rev.empty())
            {
              mtn->update(rev, output);
              do_wait(output);
            }
        }
    }
}
