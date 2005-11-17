// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

// This provides a window to put the revdat widget in, and a toolbar
// to tell it what to do.

#include <gtkmm.h>

#include <string>
#include <vector>

// chdir
#include <unistd.h>

#include "monotone.hh"
#include "revdat.hh"
#include "misc.hh"

// The toolbar generator doesn't understand this kind of item.
// [Text entry here] [Go!]
//         <label>
class txt_toolitem : public Gtk::ToolItem
{
public:
  Gtk::Entry entry;
  Gtk::Button button;
  Gtk::Label label;
  Gtk::VBox vb;
  Gtk::HBox hb;
  Gtk::Image img;

  txt_toolitem(): label("Go to revision"),
    img(Gtk::Stock::JUMP_TO, Gtk::ICON_SIZE_SMALL_TOOLBAR)
  {
    add(vb);
    vb.pack_end(label);
    vb.pack_start(hb);
    hb.pack_start(entry);
    hb.pack_end(button);
    button.add(img);
    button.set_relief(Gtk::RELIEF_NONE);
  }
};

void on_delay()
{
  while (Gtk::Main::events_pending())
    Gtk::Main::iteration();
}

class mainwin : public Gtk::Window
{
  monotone mtn;
  revdat rd;
  Glib::RefPtr<Gtk::ActionGroup> ag;
  Glib::RefPtr<Gtk::UIManager> ui;
  Gtk::VBox stuff;
  txt_toolitem ti;
public:

  void update()
  {
    UpdateDialog ud(mtn);
    ud.run();
  }

  void sync()
  {
    SyncDialog sd(mtn);
    sd.run();
  }

  void setdb()
  {
    Gtk::FileChooserDialog dialog("Please choose a database",
                                  Gtk::FILE_CHOOSER_ACTION_OPEN, "");
    dialog.set_transient_for(*this);
    dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog.add_button("Select", Gtk::RESPONSE_OK);
    Gtk::FileFilter filter_db;
    filter_db.set_name("Database files");
    filter_db.add_pattern("*.db");
    dialog.add_filter(filter_db);
    Gtk::FileFilter filter_any;
    filter_any.set_name("All files");
    filter_any.add_pattern("*");
    dialog.add_filter(filter_any);
    int result = dialog.run();
    if (result == Gtk::RESPONSE_OK)
      {
        mtn.set_db(dialog.get_filename());
        rd.clear();
      }
  }
  void setdir()
  {
    Gtk::FileChooserDialog dialog("Please choose a working copy",
                                  Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER, "");
    dialog.set_transient_for(*this);
    dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog.add_button("Select", Gtk::RESPONSE_OK);
    int result = dialog.run();
    if (result == Gtk::RESPONSE_OK)
      {
        mtn.set_dir(dialog.get_filename());
        chdir(dialog.get_filename().c_str());
        rd.loadwork();
      }
  }
  void do_refresh()
  {
    if (rd.get_wc())
      rd.loadwork();
    else
      rd.loadrev(rd.get_rev());
  }
  void do_commit()
  {
    rd.commit();
  }
  void quit()
  {
    hide();
  }
  void to_rev()
  {
    std::vector<std::string> revs = mtn.select(ti.entry.get_text());
    if (revs.size() == 1)
      rd.loadrev(revs[0]);
  }
  void to_wc()
  {
    rd.loadwork();
  }
  mainwin(): rd(&mtn, this)
  {
    mtn.set_longwait_callback(&on_delay);
    set_default_size(675, 400);
    ag = Gtk::ActionGroup::create();
    ag->add(Gtk::Action::create("Setdir", Gtk::Stock::OPEN, "Set working dir"),
      sigc::mem_fun(*this, &mainwin::setdir));
    ag->add(Gtk::Action::create("Setdb", Gtk::Stock::HARDDISK, "Set database"),
      sigc::mem_fun(*this, &mainwin::setdb));
    ag->add(Gtk::Action::create("Refresh", Gtk::Stock::REFRESH),
      sigc::mem_fun(*this, &mainwin::do_refresh));
    ag->add(Gtk::Action::create("Workingcopy", Gtk::Stock::HOME, "Working copy"),
      sigc::mem_fun(*this, &mainwin::to_wc));
    ag->add(Gtk::Action::create("Commit", Gtk::Stock::SAVE, "Commit"),
      sigc::mem_fun(*this, &mainwin::do_commit));
    ag->add(Gtk::Action::create("Quit", Gtk::Stock::QUIT),
      sigc::mem_fun(*this, &mainwin::quit));
    ag->add(Gtk::Action::create("File_menu", "_File"));
    ag->add(Gtk::Action::create("Work_menu", "_Working dir"));
    ag->add(Gtk::Action::create("Database_menu", "_Database"));
    ag->add(Gtk::Action::create("Update", Gtk::Stock::GO_UP, "Update"),
      sigc::mem_fun(*this, &mainwin::update));
    ag->add(Gtk::Action::create("Sync", Gtk::Stock::NETWORK, "Sync"),
      sigc::mem_fun(*this, &mainwin::sync));
    ui = Gtk::UIManager::create();
    ui->insert_action_group(ag);
    Glib::ustring cmdxml =
        "<ui>"
        "  <menubar name='Menubar'>"
        "    <menu action='File_menu'>"
        "      <menuitem action='Setdir'/>"
        "      <menuitem action='Setdb'/>"
        "      <menuitem action='Refresh'/>"
        "      <separator/>"
        "      <menuitem action='Quit'/>"
        "    </menu>"
        "    <menu action='Work_menu'>"
        "      <menuitem action='Commit'/>"
        "      <menuitem action='Update'/>"
        "    </menu>"
        "    <menu action='Database_menu'>"
        "      <menuitem action='Sync'/>"
        "    </menu>"
        "  </menubar>"
        "  <toolbar name='Toolbar'>"
        "    <toolitem action='Refresh'/>"
        "    <toolitem action='Workingcopy'/>"
        "    <toolitem action='Commit'/>"
        "    <toolitem action='Quit'/>"
        "  </toolbar>"
        "</ui>";
    ui->add_ui_from_string(cmdxml);
    add_accel_group(ui->get_accel_group());
    Gtk::Toolbar *tb = static_cast<Gtk::Toolbar*>
                       (ui->get_widget("/Toolbar"));
    ti.button.signal_clicked().connect(sigc::mem_fun(*this, &mainwin::to_rev));
    ti.entry.signal_activate().connect(sigc::mem_fun(*this, &mainwin::to_rev));
    tb->insert(ti, 1);
    stuff.pack_start(*ui->get_widget("/Menubar"), Gtk::PACK_SHRINK);
    stuff.pack_start(*tb, Gtk::PACK_SHRINK);
    stuff.pack_end(rd);
    add(stuff);
    show_all_children();
    to_wc();
  }
};

int main(int argc, char * argv[])
{
  Gtk::Main m(argc, argv);
  mainwin x;
  m.run(x);
  return 0;
}
