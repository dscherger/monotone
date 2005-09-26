// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

#include "revdat.hh"
#include "misc.hh"

#include <iostream>
#include <fstream>
#include <set>


revdat::revdat(monotone *m, Gtk::Window *w): rfl(this), mtn(m), window(w)
{
  pack1(rfl, Gtk::FILL);
  pack2(rfi);
  rfl.set_wc(true);
}

std::string get_log_entry(std::string const & dir)
{
  std::string log = readfile(dir + "/MT/log");
  if (!log.empty())
    return log;
  log = readfile(dir + "/.mt-template");
  if (!log.empty())
    return log;

  // try the latest (top) ChangeLog entry
  // a ChangeLog can be big, so don't call readfile
  std::ifstream is((dir + "/ChangeLog").c_str(), std::ios::binary);
  is.seekg(0, std::ios::end);
  int length = is.tellg();
  is.seekg(0, std::ios::beg);
  if (length != -1)
    {
      length = 240;
      char *buffer = new char[length];
      std::string line;
      int count = 0;
      do
        {
          if (!line.empty())
            log += line + "\n";
          is.getline(buffer, length);
          line = std::string(buffer);
          if (!line.empty() && !(line[0] == ' ' || line[0] == '\t'))
            ++count;
        } while (count < 2);
      delete[] buffer;
    }
  return log;
}

class commit_edit_window : public Gtk::Window
{
  std::string & log;
  Glib::RefPtr<Gtk::ActionGroup> ag;
  Glib::RefPtr<Gtk::UIManager> ui;
  Gtk::VBox vb;
  Gtk::TextView tv;
  Gtk::TextView cv;
  Gtk::HSeparator sep;
public:
  commit_edit_window(std::string & l,
                     std::vector<Glib::ustring> const & sel,
                     std::vector<Glib::ustring> const & desel,
                     std::vector<Glib::ustring> const & commented,
                     bool wc)
   : log(l)
  {
    set_default_size(300, 200);
    ag = Gtk::ActionGroup::create();
    ag->add(Gtk::Action::create("Ok", Gtk::Stock::OK, "Commit"),
      sigc::mem_fun(*this, &commit_edit_window::ok));
    ag->add(Gtk::Action::create("Cancel", Gtk::Stock::CANCEL),
      sigc::mem_fun(*this, &commit_edit_window::cancel));
    ui = Gtk::UIManager::create();
    ui->insert_action_group(ag);
    Glib::ustring cmdxml =
        "<ui>"
        "  <toolbar name='Toolbar'>"
        "    <toolitem action='Ok'/>"
        "    <toolitem action='Cancel'/>"
        "  </toolbar>"
        "</ui>";
    ui->add_ui_from_string(cmdxml);
    add_accel_group(ui->get_accel_group());
    vb.pack_start(*ui->get_widget("/Toolbar"), Gtk::PACK_SHRINK);
    Glib::ustring commentstr("Will add comments to the following files:\n");
    for (std::vector<Glib::ustring>::const_iterator i = commented.begin();
         i != commented.end(); ++i)
      commentstr += *i + "\n";
    cv.set_editable(false);
    cv.get_buffer()->set_text(commentstr);
    cv.set_sensitive(false);
    vb.pack_end(cv, Gtk::PACK_SHRINK);
    vb.pack_end(sep, Gtk::PACK_SHRINK);
    vb.pack_end(tv);
    add(vb);
    if (wc)
      tv.get_buffer()->set_text(l);
    else
      tv.get_buffer()->set_text("Not a working copy commit,\n"
                                "only file comments are being added.\n");
    tv.set_sensitive(wc);
    show_all_children();
    Gtk::Main::run(*this);
  }
  void ok()
  {
    log = tv.get_buffer()->get_text();
    hide();
  }
  void cancel()
  {
    log = "";
    hide();
  }
};

void revdat::commit()
{
  std::string rev = rfl.get_rev();
  std::vector<Glib::ustring> sel, desel, commented;
  std::vector<std::string> args;
  if (rfl.get_wc())
    {
      rfl.get_sel(sel, desel);
      if (sel.empty())
        {
          std::cout<<"No changes!\n";
          return;
        }
      if (sel.size() < desel.size())
        {
          for (std::vector<Glib::ustring>::iterator i = sel.begin();
               i != sel.end(); ++i)
            args.push_back(*i);
        }
      else
        {
          for (std::vector<Glib::ustring>::iterator i = desel.begin();
               i != desel.end(); ++i)
            args.push_back("--exclude=" + *i);
        }
    }
  std::map<std::string, Glib::ustring> comments = rfl.get_comments();
  comments[rfl.get_current()] = rfi.get_comment();
  for (std::map<std::string, Glib::ustring>::iterator i = comments.begin();
       i != comments.end(); ++i)
    {
      if (i->second.empty())
        continue;
      commented.push_back(i->first);
    }
  if (commented.empty() && !rfl.get_wc())
    {
      std::cout<<"No changes!\n";
      return;
    }
  std::string msg = get_log_entry(mtn->get_dir());
  commit_edit_window cew(msg, sel, desel, commented, rfl.get_wc());
  if (msg.empty())
    return;
  if (rfl.get_wc())
    {
      args.push_back("--message=" + msg);
      rev = mtn->commit(args);
    }
  for (std::map<std::string, Glib::ustring>::iterator i = comments.begin();
       i != comments.end(); ++i)
    {
      if (i->second.empty())
        continue;
      mtn->make_cert(rev, "file-comment", i->first + "\n" + i->second);
    }
  rfl.clear_comments();
  rfi.clear_comment();
  if (rfl.get_wc())
    loadwork();
  else
    loadrev(rev);
}

void revdat::loadwork()
{
  rfl.set_wc(true);
  std::vector<inventory_item> res;
  mtn->inventory(res);
  rfl.set_files(res);
}

void revdat::clear()
{
  rfl.set_wc(false);
  rfl.set_rev("");
  std::vector<Glib::ustring> pvec;
  std::vector<std::vector<inventory_item> > pchanges;
  rfl.set_parents(pvec, pchanges);
}

void revdat::loadrev(std::string const & rev)
{
  revision = rev;
  rfl.set_wc(false);
  std::vector<Glib::ustring> pvec;
  std::vector<std::vector<inventory_item> > pchanges;
  std::string res = mtn->get_revision(rev);
  std::string rename_from, man;
  std::set<std::string> changed;
  std::map<std::string, int> pmap;
  for (int begin = 0, end = res.find('\n'); begin != res.size();
       begin = end + 1, end = res.find('\n', begin))
    {
      std::string line = res.substr(begin, end-begin);
      int lpos = line.find_first_of("[\"");
      int rpos = line.find_first_of("]\"", lpos + 1);
      std::string contents = line.substr(lpos + 1, rpos - lpos - 1);
      if (line.find("new_manifest") < lpos)
        {
          man = contents;
        }
      else if (line.find("old_revision") < lpos)
        {
          pvec.push_back(contents);
          pchanges.push_back(std::vector<inventory_item>());
          pmap.clear();
        }
      else if (line.find("add") < lpos)
        {
          changed.insert(contents);
          pmap.insert(std::make_pair(contents, pchanges.back().size()));
          inventory_item ii;
          ii.postname = contents;
          pchanges.back().push_back(ii);
        }
      else if (line.find("drop") < lpos)
        {
          inventory_item ii;
          ii.prename = contents;
          pchanges.back().push_back(ii);
        }
      else if (line.find("rename_file") < lpos)
        {
          rename_from = contents;
        }
      else if (line.find("to") < lpos && !rename_from.empty())
        {
          changed.insert(contents);
          pmap.insert(std::make_pair(contents, pchanges.back().size()));
          inventory_item ii;
          ii.prename = rename_from;
          ii.postname = contents;
          pchanges.back().push_back(ii);
          rename_from = "";
        }
      else if (line.find("patch") < lpos)
        {
          changed.insert(contents);
          std::map<std::string, int>::iterator i = pmap.find(contents);
          int pos;
          if (i == pmap.end())
            {
              pos = pchanges.back().size();
              inventory_item ii;
              ii.prename = ii.postname = contents;
              pchanges.back().push_back(ii);
            }
          else
            pos = i->second;
          pchanges.back()[pos].state = inventory_item::patched;
        }
    }
  res = mtn->get_manifest(man);
  for (int begin = 0, end = res.find('\n'); begin != res.size();
       begin = end + 1, end = res.find('\n', begin))
    {
      std::string line = res.substr(begin, end-begin);
      int lpos = line.find_first_of(" \t");
      int rpos = line.find_first_not_of(" \t", lpos);
      std::string contents = line.substr(rpos);
      if (changed.find(contents) == changed.end())
        {
          inventory_item ii;
          ii.prename = ii.postname = contents;
          for (std::vector<std::vector<inventory_item> >::iterator
                 i = pchanges.begin(); i != pchanges.end(); ++i)
            i->push_back(ii);
        }
    }
  rfl.set_rev(rev);
  rfl.set_parents(pvec, pchanges);
}
