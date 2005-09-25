// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

#include "rev_file_info.hh"

rev_file_info::rev_file_info()
{
  fcontents.set_editable(false);
  fdiff.set_editable(false);
  fcommwin.add(fcomment);
  fcontwin.add(fcontents);
  fdiffwin.add(fdiff);
  append_page(fdiffwin, "File diff");
  append_page(fcontwin, "File contents");
  append_page(fcommwin, "File comment");
  fcontents.set_editable(false);
  Glib::RefPtr<Gtk::TextBuffer> b = fcomment.get_buffer();
  commentend = b->create_mark(b->end());
}

void
rev_file_info::set_contents(Glib::ustring const & s)
{
  Glib::RefPtr<Gtk::TextBuffer> b = fcontents.get_buffer();
  b->set_text(s);
  Glib::RefPtr<Gtk::TextTag> t = b->create_tag();
  t->property_family() = "monospace";
  b->apply_tag(t, b->begin(), b->end());
}

void
rev_file_info::set_diff(Glib::ustring const & s)
{
  Glib::RefPtr<Gtk::TextBuffer> b = fdiff.get_buffer();
  b->set_text(s);
  Glib::RefPtr<Gtk::TextTag> t = b->create_tag();
  t->property_family() = "monospace";
  b->apply_tag(t, b->begin(), b->end());
}

void
rev_file_info::set_comment(std::vector<Glib::ustring> const & s,
                           Glib::ustring const & e)
{
  Glib::RefPtr<Gtk::TextBuffer> b = fcomment.get_buffer();
  Glib::RefPtr<Gtk::TextTag> yellow = b->create_tag();
  yellow->property_background() = "Yellow";
  Glib::RefPtr<Gtk::TextTag> red = b->create_tag();
  red->property_background() = "Red";
  Glib::RefPtr<Gtk::TextTag> ro = b->create_tag();
  ro->property_editable() = false;
  b->set_text("");
  b->apply_tag(ro, b->begin(), b->end());
  for (std::vector<Glib::ustring>::const_iterator i = s.begin();
       i != s.end(); ++i)
    {
      Glib::ustring::const_iterator j = i->end();
      --j;
      b->insert_with_tag(b->end(), "--------------------\n", red);
      if (*j != '\n')
        b->insert_with_tag(b->end(), *i + "\n", yellow);
      else
        b->insert_with_tag(b->end(), *i, yellow);
    }
  b->insert_with_tag(b->end(), "--------------------\n", red);
  b->move_mark(commentend, b->end());
  b->apply_tag(ro, b->begin(), b->end());
  b->insert(b->end(), e);
}

void
rev_file_info::clear_comment()
{
  Glib::RefPtr<Gtk::TextBuffer> b = fcomment.get_buffer();
  b->erase(commentend->get_iter(), b->end());
}

Glib::ustring
rev_file_info::get_comment()
{
  Glib::RefPtr<Gtk::TextBuffer> b = fcomment.get_buffer();
  return b->get_slice(commentend->get_iter(), b->end());
}
