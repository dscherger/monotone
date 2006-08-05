// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <stdio.h>
#include <stdarg.h>

#include <algorithm>
#include <iterator>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include "constants.hh"
#include "platform.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "ui.hh"
#include "mt_version.hh"

using std::exception;
using std::locale;
using std::logic_error;
using std::ofstream;
using std::ostream;
using std::ostream_iterator;
using std::ostringstream;
using std::string;
using std::vector;

using boost::format;

// debugging / logging system

sanity global_sanity;

sanity::sanity() :
  debug(false), quiet(false), reallyquiet(false), relaxed(false), logbuf(0xffff),
  already_dumping(false)
{}

sanity::~sanity()
{}

void
sanity::initialize(int argc, char ** argv, char const * lc_all)
{
  // set up some marked strings, so even if our logbuf overflows, we'll get
  // this data in a crash.
  // This is probably the only place PERM_MM should ever be used.

#ifndef IN_TESTER
  string full_version_string;
  get_full_version(full_version_string);
  PERM_MM(full_version_string);
#endif

  // this is in full_version_string already, so don't MM it
  string system_flavour;
  get_system_flavour(system_flavour);
  L(FL("started up on %s") % system_flavour);

  string cmdline_string;
  {
    ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << "'" << argv[i] << "'";
      }
    cmdline_string = cmdline_ss.str();
  }
  PERM_MM(cmdline_string);
  L(FL("command line: %s") % cmdline_string);

  if (!lc_all)
    lc_all = "n/a";
  PERM_MM(string(lc_all));
  L(FL("set locale: LC_ALL=%s") % lc_all);
}

void
sanity::dump_buffer()
{
  if (!filename.empty())
    {
      ofstream out(filename.as_external().c_str());
      if (out)
        {
          copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(out));
          copy(gasp_dump.begin(), gasp_dump.end(), ostream_iterator<char>(out));
          ui.inform((FL("wrote debugging log to %s\n"
                        "if reporting a bug, please include this file")
                       % filename).str());
        }
      else
        ui.inform((FL("failed to write debugging log to %s") % filename).str());
    }
  else
    ui.inform("discarding debug log, because I have nowhere to write it\n"
              "(maybe you want --debug or --dump?)");
}

void
sanity::set_debug()
{
  quiet = false;
  reallyquiet = false;
  debug = true;

  // it is possible that some pre-setting-of-debug data
  // accumulated in the log buffer (during earlier option processing)
  // so we will dump it now
  ostringstream oss;
  vector<string> lines;
  copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(oss));
  split_into_lines(oss.str(), lines);
  for (vector<string>::const_iterator i = lines.begin(); i != lines.end(); ++i)
    ui.inform((*i) + "\n");
}

void
sanity::set_brief()
{
  brief = true;
}

void
sanity::set_quiet()
{
  debug = false;
  quiet = true;
  reallyquiet = false;
}

void
sanity::set_reallyquiet()
{
  debug = false;
  quiet = true;
  reallyquiet = true;
}

void
sanity::set_relaxed(bool rel)
{
  relaxed = rel;
}

string
sanity::do_format(format_base const & fmt, char const * file, int line)
{
  try
    {
      return fmt.str();
    }
  catch (...)
    {
      ui.inform(F("fatal: formatter failed on %s:%d")
                % file
                % line);
      throw;
    }
}


void
sanity::log(plain_format const & fmt,
            char const * file, int line)
{
  string str = do_format(fmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (debug)
    ui.inform(str);
}

void
sanity::progress(i18n_format const & i18nfmt,
                 char const * file, int line)
{
  string str = do_format(i18nfmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (! quiet)
    ui.inform(str);
}

void
sanity::warning(i18n_format const & i18nfmt,
                char const * file, int line)
{
  string str = do_format(i18nfmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  string str2 = "warning: " + str;
  copy(str2.begin(), str2.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (! reallyquiet)
    ui.warn(str);
}

void
sanity::naughty_failure(string const & expr, i18n_format const & explain,
                        string const & file, int line)
{
  string message;
  log(FL("%s:%d: usage constraint '%s' violated") % file % line % expr,
      file.c_str(), line);
  prefix_lines_with(_("misuse: "), do_format(explain, file.c_str(), line), message);
  gasp();
  throw informative_failure(message);
}

void
sanity::error_failure(string const & expr, i18n_format const & explain,
                      string const & file, int line)
{
  string message;
  log(FL("%s:%d: detected error '%s' violated") % file % line % expr,
      file.c_str(), line);
  prefix_lines_with(_("error: "), do_format(explain, file.c_str(), line), message);
  throw informative_failure(message);
}

void
sanity::invariant_failure(string const & expr,
                          string const & file, int line)
{
  char const * pattern = N_("%s:%d: invariant '%s' violated");
  log(FL(pattern) % file % line % expr, file.c_str(), line);
  gasp();
  throw logic_error((F(pattern) % file % line % expr).str());
}

void
sanity::index_failure(string const & vec_expr,
                      string const & idx_expr,
                      unsigned long sz,
                      unsigned long idx,
                      string const & file, int line)
{
  char const * pattern = N_("%s:%d: index '%s' = %d overflowed vector '%s' with size %d\n");
  log(FL(pattern) % file % line % idx_expr % idx % vec_expr % sz,
      file.c_str(), line);
  gasp();
  throw logic_error((F(pattern)
                     % file % line % idx_expr % idx % vec_expr % sz).str());
}

// Last gasp dumps

void
sanity::gasp()
{
  if (already_dumping)
    {
      L(FL("ignoring request to give last gasp; already in process of dumping"));
      return;
    }
  already_dumping = true;
  L(FL("saving current work set: %i items") % musings.size());
  ostringstream out;
  out << F("Current work set: %i items\n") % musings.size();
  for (vector<MusingI const *>::const_iterator
         i = musings.begin(); i != musings.end(); ++i)
    {
      string tmp;
      try
        {
          (*i)->gasp(tmp);
          out << tmp;
        }
      catch (logic_error)
        {
          out << tmp;
          out << "<caught logic_error>\n";
          L(FL("ignoring error trigged by saving work set to debug log"));
        }
      catch (informative_failure)
        {
          out << tmp;
          out << "<caught informative_failure>\n";
          L(FL("ignoring error trigged by saving work set to debug log"));
        }
    }
  gasp_dump = out.str();
  L(FL("finished saving work set"));
  if (debug)
    {
      ui.inform("contents of work set:");
      ui.inform(gasp_dump);
    }
  already_dumping = false;
}

MusingI::MusingI()
{
  if (!global_sanity.already_dumping)
    global_sanity.musings.push_back(this);
}

MusingI::~MusingI()
{
  if (!global_sanity.already_dumping)
    {
      I(global_sanity.musings.back() == this);
      global_sanity.musings.pop_back();
    }
}

template <> void
dump(string const & obj, string & out)
{
  out = obj;
}

void MusingBase::gasp_head(string & out) const
{
  out = (boost::format("----- begin '%s' (in %s, at %s:%d)\n")
         % name % func % file % line
         ).str();
}

void MusingBase::gasp_body(const string & objstr, string & out) const
{
  out += (boost::format("%s%s"
                        "-----   end '%s' (in %s, at %s:%d)\n")
          % objstr
          % (*(objstr.end() - 1) == '\n' ? "" : "\n")
          % name % func % file % line
          ).str();
}


struct
format_base::impl
{
  format fmt;
  ostringstream oss;

  impl(impl const & other) : fmt(other.fmt)
  {}

  impl & operator=(impl const & other)
  {
    if (&other != this)
      {
	fmt = other.fmt;
	oss.str(string());
      }
    return *this;
  }

  impl(char const * pattern)
    : fmt(pattern)
  {}
  impl(string const & pattern)
    : fmt(pattern)
  {}
  impl(char const * pattern, locale const & loc)
    : fmt(pattern, loc)
  {}
  impl(string const & pattern, locale const & loc)
    : fmt(pattern, loc)
  {}
};

format_base::format_base(format_base const & other)
  : pimpl(other.pimpl ? new impl(*(other.pimpl)) : NULL)
{

}

format_base::~format_base()
{
	delete pimpl;
}

format_base &
format_base::operator=(format_base const & other)
{
  if (&other != this)
    {
      impl * tmp = NULL;

      try
	{
	  if (other.pimpl)
	    tmp = new impl(*(other.pimpl));
	}
      catch (...)
	{
	  if (tmp)
	    delete tmp;
	}

      if (pimpl)
	delete pimpl;

      pimpl = tmp;
    }
  return *this;
}

format_base::format_base(char const * pattern)
  : pimpl(new impl(pattern))
{}

format_base::format_base(std::string const & pattern)
  : pimpl(new impl(pattern))
{}

format_base::format_base(char const * pattern, locale const & loc)
  : pimpl(new impl(pattern, loc))
{}

format_base::format_base(string const & pattern, locale const & loc)
  : pimpl(new impl(pattern, loc))
{}

ostream &
format_base::get_stream() const
{
  return pimpl->oss;
}

void
format_base::flush_stream() const
{
  pimpl->fmt % pimpl->oss.str();
  pimpl->oss.str(string());
}

void format_base::put_and_flush_signed(int64_t const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(int32_t const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(int16_t const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(int8_t const & s) const { pimpl->fmt % s; }

void format_base::put_and_flush_unsigned(uint64_t const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(uint32_t const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(uint16_t const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(uint8_t const & u) const { pimpl->fmt % u; }

void format_base::put_and_flush_float(float const & f) const { pimpl->fmt % f; }
void format_base::put_and_flush_double(double const & d) const { pimpl->fmt % d; }

std::string
format_base::str() const
{
  return pimpl->fmt.str();
}

i18n_format::i18n_format(const char * localized_pattern)
  : format_base(localized_pattern, get_user_locale())
{
}

i18n_format::i18n_format(std::string const & localized_pattern)
  : format_base(localized_pattern, get_user_locale())
{
}

ostream &
operator<<(ostream & os, format_base const & fmt)
{
  return os << fmt.str();
}

i18n_format F(const char * str)
{
  return i18n_format(gettext(str));
}


i18n_format FP(const char * str1, const char * strn, unsigned long count)
{
  return i18n_format(ngettext(str1, strn, count));
}

plain_format FL(const char * str)
{
  return plain_format(str);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
