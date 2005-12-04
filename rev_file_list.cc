// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

#include "rev_file_list.hh"
#include "revdat.hh"
#include "misc.hh"

// getcwd
#include <unistd.h>
// rand, srand
#include <cstdlib>
// rename
#include <cstdio>

template<typename V, typename A, typename T>
std::vector<V, A> & operator%(std::vector<V, A> & v, T t)
{
  v.push_back(t);
  return v;
}

bool operator<(trr const & l, trr const & r)
{
  Glib::ustring ls = l.get_path().to_string();
  Glib::ustring rs = r.get_path().to_string();
  return ls < rs;
}

bool operator==(trr const & l, trr const & r)
{
  Glib::ustring ls = l.get_path().to_string();
  Glib::ustring rs = r.get_path().to_string();
  return ls == rs;
}

namespace states
{
  const std::string added("add");
  const std::string dropped("drop");
  const std::string patched("patch");
  const std::string renamed("rename");
  const std::string unknown("unknown");
  const std::string ignored("ignored");
  const std::string unchanged("");
}

rev_file_list::rev_file_list(revdat *r): col(),
  show_changed("Changed"), show_unchanged("Unchanged"),
  show_ignored("Ignored"), show_unknown("Unknown"), parents(this), wc(true),
  filelist(Gtk::TreeStore::create(col)), files(filelist), rd(r)
{
  needscan = false;
  p_buttons.pack_start(show_changed);
  show_changed.set_active(true);
  p_buttons.pack_start(show_unchanged);
  wc_buttons.pack_start(show_unknown);
  wc_buttons.pack_start(show_ignored);
  show_changed.signal_toggled()
    .connect(sigc::mem_fun(*this, &rev_file_list::rescan));
  show_unchanged.signal_toggled()
    .connect(sigc::mem_fun(*this, &rev_file_list::rescan));
  show_unknown.signal_toggled()
    .connect(sigc::mem_fun(*this, &rev_file_list::rescan));
  show_ignored.signal_toggled()
    .connect(sigc::mem_fun(*this, &rev_file_list::rescan));
  pack_start(p_buttons, Gtk::PACK_SHRINK);
  pack_start(wc_buttons, Gtk::PACK_SHRINK);
  {
    int n = files.append_column_editable("Include", col.included);
    Gtk::TreeView::Column *c = files.get_column(n-1);
    Gtk::CellRenderer *cr = files.get_column_cell_renderer(n-1);
    Gtk::CellRendererToggle *inc = static_cast<Gtk::CellRendererToggle*>(cr);
    if(c)
    {
      c->add_attribute(inc->property_visible(), col.changed);
    }
  }
  files.append_column("State", col.status);
  int namecol = files.append_column("File name", col.name);
  files.set_expander_column(*files.get_column(namecol-1));
  filewin.add(files);
  pack_end(filewin);
  files.signal_row_activated()
    .connect(sigc::mem_fun(*this, &rev_file_list::selfile));
  files.get_selection()->set_select_function(sigc::mem_fun(*this,
                                                  &rev_file_list::selchanged));
  files.signal_button_press_event()
    .connect_notify(sigc::mem_fun(*this, &rev_file_list::clicked));

  {
    std::vector<Glib::ustring> names;
    std::vector<void(rev_file_list::*)()> funcs;
    names % "Add" % "Drop" % "Rename" % "Undo Drop" % "Revert";
    funcs % &rev_file_list::menuadd % &rev_file_list::menudrop
          % &rev_file_list::menurename % &rev_file_list::menuundrop
          % &rev_file_list::menurevert;
    std::vector<Glib::ustring>::iterator i;
    std::vector<void(rev_file_list::*)()>::iterator j;
    for (i = names.begin(), j = funcs.begin(); i != names.end(); ++i, ++j)
      {
        Gtk::MenuItem *mi;
        mi = new Gtk::MenuItem(*i);
        mi->set_manage();
        mi->set_sensitive(false);
        mi->signal_activate().connect(sigc::mem_fun(*this,*j));
        menu.append(*mi);
      }
  }

  std::list<Gtk::TargetEntry> listTargets;
  listTargets.push_back(Gtk::TargetEntry("text/uri-list"));
  files.drag_source_set(listTargets);
  files.signal_drag_data_get()
    .connect(sigc::mem_fun(*this, &rev_file_list::drag_get));
}

void
rev_file_list::drag_get(const Glib::RefPtr<Gdk::DragContext> & dc,
                        Gtk::SelectionData& sel_data,
                        guint a, guint b)
{
  int s = 512;
  char *cwd = new char[s];
  getcwd(cwd, s);
  std::string dir(cwd);
  sel_data.set(sel_data.get_target(), dir + "/" + current_file);
}

void
rev_file_list::menuadd()
{
  Gtk::TreeModel::Row menurow = *menuiter;
  menurow[col.included] = true;
  menurow[col.changed] = true;
  menurow[col.status] = states::added;
  std::string name = Glib::ustring(menurow[col.postname]);
  if (name.empty())
    name = Glib::ustring(menurow[col.prename]);
  rd->mtn->add(name);
  needscan = true;
}

void
rev_file_list::menudrop()
{
  Gtk::TreeModel::Row menurow = *menuiter;
  if (menurow[col.status] == states::added)
    {
      menurow[col.included] = false;
      menurow[col.changed] = false;
      menurow[col.status] = states::unknown;
    }
  else
    {
      menurow[col.included] = true;
      menurow[col.changed] = true;
      menurow[col.status] = states::dropped;
    }
  std::string name = Glib::ustring(menurow[col.postname]);
  if (name.empty())
    name = Glib::ustring(menurow[col.prename]);
  rd->mtn->drop(name);
  needscan = true;
}

void
rev_file_list::menurename()
{
  Gtk::TreeModel::Row menurow = *menuiter;
  Gtk::FileChooserDialog dialog("Please choose a new name",
                                Gtk::FILE_CHOOSER_ACTION_SAVE, "");
  dialog.set_transient_for(*rd->window);
  std::string cwd = dialog.get_current_folder();
  dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  dialog.add_button("Rename", Gtk::RESPONSE_OK);
  int result = dialog.run();
  if (result == Gtk::RESPONSE_OK)
    {
      std::string newname = dialog.get_filename();
      if (newname.find(cwd) != 0)
        return;
      newname = newname.substr(cwd.size() + 1);
      std::string n1 = Glib::ustring(menurow[col.prename]);
      std::string n2 = Glib::ustring(menurow[col.postname]);
      if (newname == n2 || (n2.empty() && newname == n1))
        return;
      rd->mtn->rename(n2, newname);
      menurow[col.postname] = newname;
      if (newname == n1)
        {// undo rename
          recalc_name(menuiter);
          if (menurow[col.status] == states::renamed)
            {
              menurow[col.included] = false;
              menurow[col.changed] = false;
              menurow[col.status] = states::unchanged;
            }
          return;
        }
      if (menurow[col.status] == states::unchanged)
        {
          menurow[col.included] = true;
          menurow[col.changed] = true;
          menurow[col.status] = states::renamed;
        }
      recalc_name(menuiter);
      needscan = true;
    }
}

void// not supported by monotone
rev_file_list::menuundrop()
{
  Gtk::TreeModel::Row menurow = *menuiter;
  std::string name = Glib::ustring(menurow[col.postname]);
  if (name.empty())
    name = Glib::ustring(menurow[col.prename]);
  std::string tmpname = "undrop-temp-file-";
  srand(time(0));
  srand(rand());
  char c[] = "0123456789abc";
  for (int i = 0; i < 6; ++i)
    tmpname += c[rand() % 13];
  std::string fullname;
  if (!rd->mtn->get_dir().empty())
    {
      fullname = rd->mtn->get_dir() + "/" + name;
      tmpname = rd->mtn->get_dir() + "/" + tmpname;
    }
  else
    fullname = name;
  rename(fullname.c_str(), tmpname.c_str());
  rd->mtn->revert(name);
  rename(tmpname.c_str(), fullname.c_str());
  menurow[col.status] = states::unchanged;
  needscan = true;
}

void
rev_file_list::menurevert()
{
  Gtk::TreeModel::Row menurow = *menuiter;
  if (menurow[col.status] == states::added)
    menurow[col.status] = states::unknown;
  else
    menurow[col.status] = states::unchanged;
  menurow[col.changed] = false;
  std::string name = Glib::ustring(menurow[col.postname]);
  if (name.empty())
    name = Glib::ustring(menurow[col.prename]);
  rd->mtn->revert(name);
  recalc_name(menuiter);
  needscan = true;
}

void rev_file_list::clicked(GdkEventButton *b)
{
  if (b->button != 3)
    return;
  if (!wc)
    return;
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn * column;
  int cellx, celly;
  bool go = files.get_path_at_pos(int(b->x), int(b->y),
                                  path, column, cellx, celly);
  if (!go)
    return;
  bool add_(false), drop_(false), rename_(false);
  bool undrop_(false), unmove_(false), revert_(false);
  menuiter = filelist->get_iter(path);
  Gtk::TreeModel::Row menurow = *menuiter;
  if (menurow[col.status] == states::added)
    drop_ = rename_ = revert_ = true;
  else if (menurow[col.status] == states::patched)
    drop_ = rename_ = revert_ = true;
  else if (menurow[col.status] == states::renamed)
    drop_ = rename_ = revert_ = true;
  else if (menurow[col.status] == states::dropped)
    add_ = undrop_ = revert_ = true;
  else if (menurow[col.status] == states::unknown)
    add_ = true;
  else if (menurow[col.status] == states::unchanged)
    drop_ = rename_ = true;
  std::vector<bool> v;
  set_menu(v % add_ % drop_ % rename_ % undrop_ % revert_);
  menu.popup(b->button, b->time);
  menu.show_all_children();
}

void
rev_file_list::set_menu(std::vector<bool> const & v)
{
  Gtk::MenuShell::MenuList c = menu.items();
  Gtk::MenuShell::MenuList::iterator i;
  std::vector<bool>::const_iterator j;
  for (i = c.begin(), j = v.begin(); i != c.end() && j != v.end(); ++i, ++j)
    i->set_sensitive(*j);
}

void rev_file_list::set_parents(std::vector<Glib::ustring> const & pvec,
                    std::vector<std::vector<inventory_item> > const & pch)
{
  pchanges = pch;
  parents.set_parents(pvec);
  if (pvec.empty())
    {
      set_files(std::vector<inventory_item>());
      parent = "";
    }
  else
    {
      set_files(pch[0]);
      parent = pvec[0];
    }
}

void rev_file_list::set_rev(std::string const & r)
{
  rev = r;
  if (r.empty())
    certs.clear();
  else
    {
      rd->mtn->certs(r, certs);
      rd->mtn->waitfor();
    }
}

void rev_file_list::pchange(int n)
{
  set_files(pchanges[n]);
  parent = parents.get_label(n);
}

void rev_file_list::clear_comments()
{
  comments.clear();
}

void rev_file_list::set_wc(bool w)
{
  if (wc == w)
    return;
  wc = w;
  if (wc)
    {
      remove(parents);
      pack_start(wc_buttons, Gtk::PACK_SHRINK);
      reorder_child(wc_buttons, 1);
    }
  else
    {
      pack_start(parents, Gtk::PACK_SHRINK);
      remove(wc_buttons);
      reorder_child(parents, 0);
      show_all_children();
    }
}

void
rev_file_list::get_sel(std::vector<Glib::ustring> & inc,
                       std::vector<Glib::ustring> & exc)
{
  inc.clear();
  exc.clear();
  typedef Gtk::ListStore::Children children;
  children ch(filelist->children());
  for (children::iterator i = ch.begin(); i != ch.end(); ++i)
    {
      if (!(*i)[col.changed])
        continue;
      Glib::ustring n = (*i)[col.postname];
      if (n.empty())
        n = (*i)[col.prename];
      if ((*i)[col.included])
        inc.push_back(n);
      else
        exc.push_back(n);
    }
}

bool
rev_file_list::selchanged(Glib::RefPtr<Gtk::TreeModel> const & model,
                          Gtk::TreeModel::Path const & path,
                          bool path_currently_selected)
{
  if (path_currently_selected)
    return true;
  dosel(path);
  return true;
}

void // double-click
rev_file_list::selfile(Gtk::TreeModel::Path const & p,
                       Gtk::TreeView::Column *c)
{
//  dosel(p);
}

void
rev_file_list::dosel(Gtk::TreeModel::Path const & p)
{
  Gtk::TreeModel::Row row = *filelist->get_iter(p);
  std::string filename, fullname;
  filename = Glib::ustring(row[col.postname]).raw();
  if (filename.empty())
    filename = Glib::ustring(row[col.prename]).raw();
  if (!rd->mtn->get_dir().empty())
    fullname = rd->mtn->get_dir() + "/" + filename;
  else
    fullname = filename;

  if (row[col.status] != states::added && row[col.status] != states::ignored
      && row[col.status] != states::unknown)
    {
      string diff;
      if (wc)
        rd->mtn->diff(filename, diff);
      else
        rd->mtn->diff(filename, parent, rev, diff);
      rd->mtn->waitfor();
      rd->rfi.set_diff(diff);
    }
  else
    rd->rfi.set_diff("No diff available.");

    if (wc)
      rd->rfi.set_contents(readfile(fullname));
    else if (row[col.status] != states::dropped)
      {
        string foo;
        rd->mtn->cat(filename, rev, foo);
        rd->mtn->waitfor();
        rd->rfi.set_contents(foo);
      }
    else
      rd->rfi.set_contents("File dropped -- contents not available");

  std::vector<Glib::ustring> c;
  for (std::vector<cert>::iterator i = certs.begin(); i != certs.end(); ++i)
    {
      if (i->name != "file-comment")
        continue;
      int p = i->value.find('\n');
      if (i->value.substr(0, p) != filename)
        continue;
      c.push_back(i->value.substr(p+1));
    }
  if (!current_file.empty())
    comments[current_file] = rd->rfi.get_comment();
  rd->rfi.set_comment(c, comments[filename]);
  current_file = filename;
}

void
rev_file_list::recalc_name(Gtk::TreeModel::iterator & i)
{
  Glib::ustring pre = (*i)[col.prename];
  Glib::ustring post = (*i)[col.postname];
  Glib::ustring disp;
  std::map<trr, Glib::ustring>::iterator j = rdirs.end();
  if (filelist->iter_depth(i))
    {
      Gtk::TreeModel::iterator k = i->parent();
      Gtk::TreeModel::RowReference ref(filelist, filelist->get_path(k));
      j = rdirs.find(ref);
    }
  Glib::ustring parent;
  if (j != rdirs.end())
    parent = j->second + "/";
  if (pre.find(parent) == 0)
    pre = pre.substr(parent.size());
  else
    pre = "/" + pre;
  if (post.find(parent) == 0)
    post = post.substr(parent.size());
  else
    post = "/" + post;
  if (pre.empty())
    disp = post;
  else if (post.empty())
    disp = pre;
  else if (pre == post)
    disp = pre;
  else
    disp = pre + "\n" + post;
  (*i)[col.name] = disp;
}

void
rev_file_list::rescan()
{
  if (needscan)
    {
      needscan = false;
      if (wc)
        rd->loadwork();
      else
        rd->loadrev(rev);
      return;
    }
  rd->rfi.set_diff("");
  rd->rfi.set_contents("");
  if (!current_file.empty())
    comments[current_file] = rd->rfi.get_comment();
  rd->rfi.set_comment(std::vector<Glib::ustring>(), "");
  current_file = "";
  dirs.clear();
  rdirs.clear();

  while (!filelist->children().empty())
    filelist->erase(filelist->children().begin());
  for (std::vector<inventory_item>::const_iterator i = inventory.begin();
        i != inventory.end(); ++i)
    {
      bool changed = false;
      // ignored
      if (i->state == inventory_item::ignored)
        {
          if (!show_ignored.get_active())
            continue;
        }
      // unknown
      else if (i->state == inventory_item::unknown)
        {
          if (!show_unknown.get_active())
            continue;
        }
      // unchanged
      else if (i->state != inventory_item::patched
                && i->prename == i->postname)
        {
          if (!show_unchanged.get_active())
            continue;
        }
      // changed
      else
        {
          changed = true;
          if (!show_changed.get_active())
            continue;
        }
      Gtk::TreeModel::Children parent_row = filelist->children();
      Glib::ustring workstr = i->postname;
      if (workstr.empty())
        workstr = i->prename;
      if (workstr.empty())
        {
//          std::cout<<"Nameless item!\n";
          continue;
        }
      std::vector<Glib::ustring> stk;
      while (true)
        {
          Glib::ustring pdir = workstr.substr(0, workstr.rfind('/'));
          std::map<Glib::ustring, trr>::iterator j = dirs.find(pdir);
          if (j != dirs.end() || pdir.size() == workstr.size())
            {
              if (j != dirs.end())
                {
                  Gtk::TreeModel::Path p = j->second.get_path();
                  Gtk::TreeModel::iterator iter = filelist->get_iter(p);
                  parent_row = iter->children();
                }
              while (!stk.empty())
                {
                  Gtk::TreeModel::iterator row = filelist->append(parent_row);
                  Glib::ustring leaf = workstr.substr(workstr.rfind('/')+1);
                  (*row)[col.name] = leaf;
                  (*row)[col.prename] = workstr;
                  (*row)[col.postname] = workstr;
                  (*row)[col.status] = " ";
                  Gtk::TreeModel::RowReference
                    rr(filelist, filelist->get_path(row));
                  dirs.insert(std::make_pair(workstr, rr));
                  rdirs.insert(std::make_pair(rr, workstr));
                  parent_row = row->children();
                  workstr = stk.back();
                  stk.pop_back();
                }
              break;
            }
          stk.push_back(workstr);
          workstr = pdir;
        }
      Gtk::TreeModel::iterator iter = filelist->append(parent_row);
      Gtk::TreeModel::Row row = *iter;
      row[col.included] = changed;
      row[col.changed] = wc && changed;
      if (i->prename.empty())
        row[col.status] = states::added;
      else if (i->state == inventory_item::patched)
        row[col.status] = states::patched;
      else if (i->prename.size() && i->postname.size()
                && i->prename != i->postname)
        row[col.status] = states::renamed;
      else if (i->postname.empty())
        row[col.status] = states::dropped;
      else if (i->state == inventory_item::ignored)
        row[col.status] = states::ignored;
      else if (i->state == inventory_item::unknown)
        row[col.status] = states::unknown;
      else
        row[col.status] = states::unchanged;

      row[col.prename] = i->prename;
      row[col.postname] = i->postname;
      recalc_name(iter);
    }
}

void
rev_file_list::set_files(std::vector<inventory_item> const & f)
{
  inventory = f;
  rescan();
}
