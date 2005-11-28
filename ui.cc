// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains a couple utilities to deal with the user
// interface. the global user_interface object 'ui' owns clog, so no
// writing to it directly!

#include "config.h"
#include "platform.hh"
#include "sanity.hh"
#include "ui.hh"
#include "transforms.hh"
#include "constants.hh"

#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>

using namespace std;
using boost::lexical_cast;
struct user_interface ui;

ticker::ticker(string const & tickname, std::string const & s, size_t mod,
    bool kilocount) :
  ticks(0),
  mod(mod),
  kilocount(kilocount),
  name(tickname),
  shortname(s)
{
  I(ui.tickers.find(tickname) == ui.tickers.end());
  ui.tickers.insert(make_pair(tickname,this));
}

ticker::~ticker()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  if (ui.some_tick_is_dirty)
    {
      ui.write_ticks();
    }
  ui.tickers.erase(name);
  ui.finish_ticking();
}

void 
ticker::operator++()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  ticks++;
  ui.some_tick_is_dirty = true;
  if (ticks % mod == 0)
    ui.write_ticks();
}

void 
ticker::operator--()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  I(ticks);
  ticks--;
  ui.some_tick_is_dirty = true;
  if (ticks % mod == 0)
    ui.write_ticks();
}

void 
ticker::operator+=(size_t t)
{
  I(ui.tickers.find(name) != ui.tickers.end());
  size_t old = ticks;

  ticks += t;
  if (t != 0)
    {
      ui.some_tick_is_dirty = true;
      if (ticks % mod == 0 || (ticks / mod) > (old / mod))
        ui.write_ticks();
    }
}


tick_write_count::tick_write_count() : last_tick_len(0)
{
}

tick_write_count::~tick_write_count()
{
}

void tick_write_count::write_ticks()
{
  string tickline1, tickline2;
  bool first_tick = true;

  tickline1 = "monotone: ";
  tickline2 = "\rmonotone:";
  
  unsigned int width;
  unsigned int minwidth = 7;
  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      width = 1 + display_width(utf8(i->second->name));
      if (!first_tick)
        {
          tickline1 += " | ";
          tickline2 += " |";
        }
      first_tick = false;
      if (display_width(utf8(i->second->name)) < minwidth)
        {
          tickline1.append(minwidth - display_width(utf8(i->second->name)),' ');
          width += minwidth - display_width(utf8(i->second->name));
        }
      tickline1 += i->second->name;
      
      string count;
      if (i->second->kilocount && i->second->ticks >= 10000)
        { // automatic unit conversion is enabled
          float div;
          const char *message;
          if (i->second->ticks >= 1048576) {
          // ticks >=1MB, use Mb
            div = 1048576;
          // xgettext: mebibytes (2^20 bytes)
            message = N_("%.1f M");
          } else {
          // ticks <1MB, use kb
            div = 1024;
          // xgettext: kibibytes (2^10 bytes)
            message = N_("%.1f k");
          }
          // we reset the mod to the divider, to avoid spurious screen updates
          i->second->mod = static_cast<int>(div / 10.0);
          count = (F(message) % (i->second->ticks / div)).str();
        }
      else
        {
          // xgettext: bytes
          count = (F("%d") % i->second->ticks).str();
        }
        
      if (display_width(utf8(count)) < width)
        {
          tickline2.append(width - display_width(utf8(count)),' ');
        }
      else if (display_width(utf8(count)) > width)
        {
          // FIXME: not quite right, because substr acts on bytes rather than
          // characters; but there are always more bytes than characters, so
          // at worst this will just chop off a little too much.
          count = count.substr(display_width(utf8(count)) - width);
        }
      tickline2 += count;
    }

  if (!ui.tick_trailer.empty())
    {
      tickline2 += " ";
      tickline2 += ui.tick_trailer;
    }
  
  size_t curr_sz = display_width(utf8(tickline2));
  if (curr_sz < last_tick_len)
    tickline2.append(last_tick_len - curr_sz, ' ');
  last_tick_len = curr_sz;

  unsigned int tw = terminal_width();
  if(!ui.last_write_was_a_tick)
    {
      if (tw && display_width(utf8(tickline1)) > tw)
        {
          // FIXME: may chop off more than necessary (because we chop by
          // bytes, not by characters)
          tickline1.resize(tw);
        }
      clog << tickline1 << "\n";
    }
  if (tw && display_width(utf8(tickline2)) > tw)
    {
      // first character in tickline2 is "\r", which does not take up any
      // width, so we add 1 to compensate.
      // FIXME: may chop off more than necessary (because we chop by
      // bytes, not by characters)
      tickline2.resize(tw + 1);
    }
  clog << tickline2;
  clog.flush();
}

void tick_write_count::clear_line()
{
  clog << endl;
}


tick_write_dot::tick_write_dot()
{
}

tick_write_dot::~tick_write_dot()
{
}

void tick_write_dot::write_ticks()
{
  static const string tickline_prefix = "monotone: ";
  string tickline1, tickline2;
  bool first_tick = true;

  if (ui.last_write_was_a_tick)
    {
      tickline1 = "";
      tickline2 = "";
    }
  else
    {
      tickline1 = "monotone: ticks: ";
      tickline2 = "\n" + tickline_prefix;
      chars_on_line = tickline_prefix.size();
    }

  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      map<string,size_t>::const_iterator old = last_ticks.find(i->first);

      if (!ui.last_write_was_a_tick)
        {
          if (!first_tick)
            tickline1 += ", ";

          tickline1 +=
            i->second->shortname + "=\"" + i->second->name + "\""
            + "/" + lexical_cast<string>(i->second->mod);
          first_tick = false;
        }

      if (old == last_ticks.end()
          || ((i->second->ticks / i->second->mod)
              > (old->second / i->second->mod)))
        {
          chars_on_line += i->second->shortname.size();
          if (chars_on_line > guess_terminal_width())
            {
              chars_on_line = tickline_prefix.size() + i->second->shortname.size();
              tickline2 += "\n" + tickline_prefix;
            }
          tickline2 += i->second->shortname;

          if (old == last_ticks.end())
            last_ticks.insert(make_pair(i->first, i->second->ticks));
          else
            last_ticks[i->first] = i->second->ticks;
        }
    }

  clog << tickline1 << tickline2;
  clog.flush();
}

void tick_write_dot::clear_line()
{
  clog << endl;
}


user_interface::user_interface() :
  last_write_was_a_tick(false),
  t_writer(0)
{
  cout.exceptions(ios_base::badbit);
#ifdef SYNC_WITH_STDIO_WORKS
  clog.sync_with_stdio(false);
#endif
  clog.unsetf(ios_base::unitbuf);
  if (have_smart_terminal()) 
    set_tick_writer(new tick_write_count);
  else
    set_tick_writer(new tick_write_dot);
}

user_interface::~user_interface()
{
  delete t_writer;
}

void 
user_interface::finish_ticking()
{
  if (tickers.size() == 0 && 
      last_write_was_a_tick)
    {
      tick_trailer = "";
      t_writer->clear_line();
      last_write_was_a_tick = false;
    }
}

void 
user_interface::set_tick_trailer(string const & t)
{
  tick_trailer = t;
}

void 
user_interface::set_tick_writer(tick_writer * t)
{
  if (t_writer != 0)
    delete t_writer;
  t_writer = t;
}

void 
user_interface::write_ticks()
{
  t_writer->write_ticks();
  last_write_was_a_tick = true;
  some_tick_is_dirty = false;
}

void 
user_interface::warn(string const & warning)
{
  if (issued_warnings.find(warning) == issued_warnings.end())
    inform("warning: " + warning);
  issued_warnings.insert(warning);
}

void 
user_interface::fatal(string const & fatal)
{
  inform(F("fatal: %s\n"
           "this is almost certainly a bug in monotone.\n"
           "please send this error message, the output of 'monotone --full-version',\n"
           "and a description of what you were doing to %s.\n")
         % fatal % PACKAGE_BUGREPORT);
}


static inline string 
sanitize(string const & line)
{
  // FIXME: you might want to adjust this if you're using a charset
  // which has safe values in the sub-0x20 range. ASCII, UTF-8, 
  // and most ISO8859-x sets do not.
  string tmp;
  tmp.reserve(line.size());
  for (size_t i = 0; i < line.size(); ++i)
    {
      if ((line[i] == '\n')
          || (static_cast<unsigned char>(line[i]) >= static_cast<unsigned char>(0x20) 
              && line[i] != static_cast<char>(0x7F)))
        tmp += line[i];
      else
        tmp += ' ';
    }
  return tmp;
}

void
user_interface::ensure_clean_line()
{
  if (last_write_was_a_tick)
    {
      write_ticks();
      t_writer->clear_line();
    }
  last_write_was_a_tick = false;
}

void 
user_interface::inform(string const & line)
{
  string prefixedLine;
  prefix_lines_with(_("monotone: "), line, prefixedLine);
  ensure_clean_line();
  clog << sanitize(prefixedLine) << endl;
  clog.flush();
}

unsigned int
guess_terminal_width()
{
  unsigned int w = terminal_width();
  if (!w)
    w = constants::default_terminal_width;
  return w;
}

const locale &
get_user_locale()
{
  // this is awkward because if LC_CTYPE is set to something the
  // runtime doesn't know about, it will fail. in that case,
  // the default will have to do.
  static bool init = false;
  static locale user_locale;
  if (!init)
    {
      init = true;
      try
        {
          user_locale = locale("");
        }
      catch( ... )
        {}
    }
  return user_locale;
}
