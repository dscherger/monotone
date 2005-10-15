// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

#include "rev_file_info.hh"

#include <boost/lexical_cast.hpp>

rev_file_info::rev_file_info()
{
  fcontents.set_editable(false);
  fdiff.set_editable(false);
  current = fcontents.get_buffer()->create_tag();
  current->property_foreground() = "brown";
  fcommwin.add(fcomment);
  fcontwin.add(fcontents);
  fdiffwin.add(fdiff);
  append_page(fdiffwin, "File diff");
  append_page(fcontwin, "File contents");
  append_page(fcommwin, "File comment");
  Glib::RefPtr<Gtk::TextBuffer> b = fcomment.get_buffer();
  commentend = b->create_mark(b->end());
  fdiff.signal_button_press_event()
    .connect_notify(sigc::mem_fun(*this, &rev_file_info::diff_clicked));
}

void rev_file_info::diff_clicked(GdkEventButton *b)
{
//  Gtk::TextIter i;
//  fdiff.get_iter_at_location(i, int(b->x), int(b->y));
//  std::cout<<"Click!\n";
}

Glib::RefPtr<Gtk::TextTag>
rev_file_info::hypertag(Glib::RefPtr<Gtk::TextBuffer> b, int line, int eline)
{
  Glib::RefPtr<Gtk::TextTag> t = b->create_tag();
  t->property_underline() = Pango::UNDERLINE_SINGLE;
  t->property_foreground() = "blue";
  link_tag_to(t, line, eline);
  return t;
}

void rev_file_info::link_tag_to(Glib::RefPtr<Gtk::TextTag> &t,
                                int line, int eline)
{
  t->signal_event().connect(sigc::bind(
                            sigc::bind(sigc::mem_fun(*this,
                                       &rev_file_info::tag_event),
                            eline), line));
}
bool rev_file_info::tag_event(Glib::RefPtr<Glib::Object> const &p,
                              GdkEvent *e, Gtk::TextIter const &it,
                              int line, int eline)
{
  GdkEventType et = e->type;
  if (et == GDK_BUTTON_RELEASE)
    {
      Glib::RefPtr<Gtk::TextBuffer> b = fcontents.get_buffer();
      Gtk::TextIter i = b->get_iter_at_line(line);
      Gtk::TextIter j = b->get_iter_at_line(eline+1)--;
      fcontents.scroll_to(i);
      b->remove_tag(current, b->begin(), b->end());
      b->apply_tag(current, i, j);
      property_page() = 1;
      GdkEventButton *be = &e->button;
    }
  return false;
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

  try {
  // every line "@@ -a,b +c,d @@" gets linked to line c of "File contents"
  int n = 0, m = 0;
  while ((n = s.find("\n@@", m)) != s.npos && n >= m)
    {
      m = s.find("@@\n", n);
      if (m == s.npos)
        break;
      int plus = s.find("+", n);
      int comma = s.find(",", plus);
      int space = s.find(" ", comma);
      if (plus > comma || comma > space || space > m)
        continue;
      Glib::ustring line = s.substr(plus, comma-plus);
      Glib::ustring size = s.substr(comma+1, space-comma-1);
      int l = boost::lexical_cast<int>(line);
      int s = boost::lexical_cast<int>(size);
      Glib::RefPtr<Gtk::TextTag> t = hypertag(b, l, l+s);
      b->apply_tag(t, b->get_iter_at_offset(n+1), b->get_iter_at_offset(m+2));
    }
  } catch (std::exception &) {}

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
