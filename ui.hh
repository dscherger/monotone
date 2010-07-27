// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __UI_HH__
#define __UI_HH__

// this file contains a couple utilities to deal with the user
// interface. the global user_interface object 'ui' owns cerr, so
// no writing to it directly!

#include "vector.hh"

struct i18n_format;
class system_path;
struct usage;
struct options;

struct ticker
{
  size_t ticks;
  size_t mod;
  size_t total;
  size_t previous_total;
  bool kilocount;
  bool use_total;
  bool may_skip_display;
  std::string keyname;
  std::string name; // translated name
  std::string shortname;
  size_t count_size;
  ticker(std::string const & n, std::string const & s, size_t mod = 64,
      bool kilocount=false, bool skip_display=false);
  void set_total(size_t tot) { use_total = true; total = tot; }
  void set_count_size(size_t csiz) { count_size = csiz; }
  void operator++();
  void operator+=(size_t t);
  void operator--();
  ~ticker();
};

struct tick_writer;
struct tick_write_count;
struct tick_write_dot;
struct tick_write_stdio;

struct user_interface
{
public:
  void initialize();
  void deinitialize();
  void warn(std::string const & warning);
  void warn(format_base const & fmt) { warn(fmt.str()); }
  void fatal(std::string const & fatal);
  void fatal(format_base const & fmt) { fatal(fmt.str()); }
  void fatal_db(std::string const & fatal);
  void fatal_db(format_base const & fmt) { fatal_db(fmt.str()); }
  void inform(std::string const & line);
  void inform(format_base const & fmt) { inform(fmt.str()); }
  void inform_usage(usage const & u, options & opts);
  int fatal_exception(std::exception const & ex);
  int fatal_exception();
  void set_tick_trailer(std::string const & trailer);
  void set_tick_write_dot();
  void set_tick_write_count();
  void set_tick_write_stdio();
  void set_tick_write_nothing();
  void ensure_clean_line();
  void redirect_log_to(system_path const & filename);
  void enable_timestamps();

  std::string output_prefix();

private:
  void finish_ticking();
  void write_ticks();

  struct impl;
  impl * imp;
  bool timestamps_enabled;
  enum ticker_type { count=1, dot, stdio, none } tick_type;

  friend struct ticker;
  friend struct tick_write_count;
  friend struct tick_write_dot;
  friend struct tick_write_stdio;
};

extern struct user_interface ui;

// Wrapper class which ensures proper setup and teardown of the global ui
// object.  (We do not want to use global con/destructors for this, as they
// execute outside the protection of main.cc's signal handlers.)
struct ui_library
{
  ui_library() { ui.initialize(); }
  ~ui_library() { ui.deinitialize(); }
};

// like platform.hh's "terminal_width", but always returns a sensible value
// (even if there is no terminal)
unsigned int guess_terminal_width();

std::string format_text(std::string const & text,
                        size_t const col = 0, size_t curcol = 0);
std::string format_text(i18n_format const & text,
                        size_t const col = 0, size_t curcol = 0);

#endif // __UI_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
