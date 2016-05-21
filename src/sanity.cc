// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2014-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <iterator>
#include <fstream>
#include <sstream>
#include "vector.hh"

#include <boost/format.hpp>
#include <boost/circular_buffer.hpp>

#include "lexical_cast.hh"
#include "constants.hh"
#include "platform.hh"
#include "file_io.hh" // make_dir_for
#include "sanity.hh"
#include "simplestring_xform.hh"

using std::exception;
using std::locale;
using std::logic_error;
using std::ofstream;
using std::ostream;
using std::ostream_iterator;
using std::ostringstream;
using std::out_of_range;
using std::string;
using std::unique_ptr;
using std::vector;

using boost::format;
using boost::lexical_cast;

// set by sanity::initialize
string const * prog_name_ptr;

string
origin::type_to_string(origin::type t)
{
  switch (t)
    {
    case internal:
      return string("internal");
    case network:
      return string("network");
    case database:
      return string("database");
    case system:
      return string("system");
    case user:
      return string("user");
    case workspace:
      return string("workspace");
    case no_fault:
      return string("general");
    default:
      return string("invalid error type");
    }
}

struct sanity::impl
{
  int verbosity;
  // logically this should be "verbosity >= 1", but debug messages aren't
  // captured for automate output and doing so would probably be an
  // information leak in the case of remote_automate. So track debug-ness
  // separately so it can be unchanged when a subcommand changes the
  // verbosity level.
  bool is_debug;
  boost::circular_buffer<char> logbuf;
  string real_prog_name;
  string filename;
  string gasp_dump;
  bool already_dumping;
  vector<unique_ptr<MusingBase>> musings;

  void (*out_of_band_function)(char channel, std::string const& text, void *opaque);
  void *out_of_band_opaque;

  impl() :
    verbosity(0), is_debug(false), logbuf(0xffff),
    already_dumping(false), out_of_band_function(0), out_of_band_opaque(0)
  {}
};

// debugging / logging system

sanity::sanity()
{
  imp = nullptr;
}

sanity::~sanity()
{
  if (imp)
    delete imp;
}

void
sanity::initialize(int argc, char ** argv, char const * localename)
{
  imp = new impl;

  // set up some marked strings, so even if our logbuf overflows, we'll get
  // this data in a crash.  This (and subclass overrides) are probably the
  // only place PERM_MM should ever be used.

  string system_flavour;
  get_system_flavour(system_flavour);
  PERM_MM(system_flavour);
  L(FL("started up on %s") % system_flavour);

  string cmdline_string;
  {
    ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << '\'' << argv[i] << '\'';
      }
    cmdline_string = cmdline_ss.str();
  }
  PERM_MM(cmdline_string);
  L(FL("command line: %s") % cmdline_string);

  string lc_all(localename ? localename : "n/a");
  PERM_MM(lc_all);
  L(FL("set locale: LC_ALL=%s") % lc_all);

  // find base name of executable and save in the prog_name global. note that
  // this does not bother with conversion to utf8.
  {
    string av0 = argv[0];
    if (av0.size() > 4 && av0.rfind(".exe") == av0.size() - 4)
      av0.erase(av0.size() - 4);
    string::size_type last_slash = av0.find_last_of("/\\");
    if (last_slash != string::npos)
      av0.erase(0, last_slash+1);
    imp->real_prog_name = av0;
    prog_name_ptr = &imp->real_prog_name;
  }
}

void
sanity::dump_buffer()
{
  I(imp);
  if (!imp->filename.empty())
    {
      ofstream out(imp->filename.c_str());
      if (!out)
        {
          try
            {
              make_dir_for(system_path(imp->filename, origin::internal));
              out.open(imp->filename.c_str());
            }
          catch (...)
            {
              inform_message((FL("failed to create directory for %s")
                              % imp->filename).str());
            }
        }
      if (out)
        {
          copy(imp->logbuf.begin(), imp->logbuf.end(),
               ostream_iterator<char>(out));
          copy(imp->gasp_dump.begin(), imp->gasp_dump.end(),
               ostream_iterator<char>(out));
          inform_message((FL("wrote debugging log to %s\n"
                        "if reporting a bug, please include this file")
                       % imp->filename).str());
        }
      else
        inform_message((FL("failed to write debugging log to %s")
                        % imp->filename).str());
    }
  else
    inform_message("discarding debug log, because I have nowhere to write it\n"
                   "(maybe you want -v -v or --dump?)");
}

int
sanity::set_verbosity(int level, bool allow_debug_change)
{
  I(imp);
  int ret = imp->verbosity;
  imp->verbosity = level;

  if (allow_debug_change)
    {
      imp->is_debug = (level >= 1);

      if (imp->is_debug)
        {
          // it is possible that some pre-setting-of-debug data
          // accumulated in the log buffer (during earlier option processing)
          // so we will dump it now
          ostringstream oss;
          vector<string> lines;
          copy(imp->logbuf.begin(), imp->logbuf.end(), ostream_iterator<char>(oss));
          split_into_lines(oss.str(), lines);
          for (vector<string>::const_iterator i = lines.begin(); i != lines.end(); ++i)
            inform_log((*i) + "\n");
        }
    }
  return ret;
}
void
sanity::set_debug()
{
  set_verbosity(1, true);
}
int
sanity::get_verbosity() const
{
  I(imp);
  return imp->verbosity;
}

void
sanity::set_dump_path(std::string const & path)
{
  I(imp);
  if (imp->filename.empty())
    {
      L(FL("setting dump path to %s") % path);
      imp->filename = path;
    }
}

string
sanity::do_format(format_base const & fmt, char const * file, int line)
{
  try
    {
      return fmt.str();
    }
  catch (exception & e)
    {
      inform_error((F("fatal: formatter failed on %s:%d: %s")
                % file
                % line
                % e.what()).str());
      throw;
    }
}

bool
sanity::debug_p()
{
  if (!imp)
    throw std::logic_error("sanity::debug_p called "
                            "before sanity::initialize");
  return imp->is_debug;
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
  copy(str.begin(), str.end(), back_inserter(imp->logbuf));
  if (str[str.size() - 1] != '\n')
    imp->logbuf.push_back('\n');

  inform_log(str);
}

void
sanity::progress(i18n_format const & i18nfmt,
                 char const * file, int line)
{
  string str = do_format(i18nfmt, file, line);

  if (maybe_write_to_out_of_band_handler('p', str))
    return;

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(imp->logbuf));
  if (str[str.size() - 1] != '\n')
    imp->logbuf.push_back('\n');

  inform_message(str);
}

void
sanity::warning(i18n_format const & i18nfmt,
                char const * file, int line)
{
  string str = do_format(i18nfmt, file, line);

  if (maybe_write_to_out_of_band_handler('w', str))
    return;

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  string str2 = "warning: " + str;
  copy(str2.begin(), str2.end(), back_inserter(imp->logbuf));
  if (str[str.size() - 1] != '\n')
    imp->logbuf.push_back('\n');

  inform_warning(str);
}

void
sanity::generic_failure(char const * expr,
                        origin::type caused_by,
                        i18n_format const & explain,
                        char const * file, int line)
{
  if (!imp)
    throw std::logic_error("sanity::generic_failure occured "
                           "before sanity::initialize");

  log(FL("Encountered an error while musing upon the following:"),
      file, line);
  gasp();
  log(FL("%s:%d: detected %s error, '%s' violated")
      % file % line % origin::type_to_string(caused_by) % expr,
      file, line);

  string prefix;
  if (caused_by == origin::user)
    {
      prefix = _("misuse: ");
    }
  else
    {
      prefix = _("error: ");
    }
  string message;
  prefix_lines_with(prefix, do_format(explain, file, line), message);
  switch (caused_by)
    {
    case origin::database:
    case origin::internal:
      throw unrecoverable_failure(caused_by, message);
    default:
      throw recoverable_failure(caused_by, message);
    }
}

void
sanity::index_failure(char const * vec_expr,
                      char const * idx_expr,
                      unsigned long sz,
                      unsigned long idx,
                      char const * file, int line)
{
  char const * pattern
    = N_("%s:%d: index '%s' = %d overflowed vector '%s' with size %d");
  if (!imp)
    throw std::logic_error("sanity::index_failure occured "
                            "before sanity::initialize");
  if (debug_p())
    log(FL(pattern) % file % line % idx_expr % idx % vec_expr % sz,
        file, line);
  gasp();
  throw out_of_range((F(pattern)
                      % file % line % idx_expr % idx % vec_expr % sz).str());
}

// Last gasp dumps

void
sanity::push_musing(unique_ptr<MusingBase> && musing)
{
  I(imp);
  if (!imp->already_dumping)
    imp->musings.push_back(move(musing));
}

void
sanity::pop_musing()
{
  I(imp);
  if (!imp->already_dumping)
    imp->musings.pop_back();
}


void
sanity::gasp()
{
  if (!imp)
    return;
  if (imp->already_dumping)
    {
      L(FL("ignoring request to give last gasp; already in process of dumping"));
      return;
    }
  imp->already_dumping = true;
  L(FL("saving current work set: %i items") % imp->musings.size());
  ostringstream out;
  out << (F("Current work set: %i items") % imp->musings.size())
      << '\n'; // final newline is kept out of the translation
  for (unique_ptr<MusingBase> const & musing : imp->musings)
    {
      string tmp;
      try
        {
          musing->gasp(tmp);
          out << tmp;
        }
      catch (logic_error)
        {
          out << tmp;
          out << "<caught logic_error>\n";
          L(FL("ignoring error trigged by saving work set to debug log"));
        }
      catch (recoverable_failure)
        {
          out << tmp;
          out << "<caught informative_failure>\n";
          L(FL("ignoring error trigged by saving work set to debug log"));
        }
    }
  imp->gasp_dump = out.str();
  L(FL("finished saving work set"));
  if (debug_p())
    {
      inform_log("contents of work set:");
      inform_log(imp->gasp_dump);
    }
  imp->already_dumping = false;
}

void sanity::set_out_of_band_handler(void (*out_of_band_function)(char, std::string const&, void *), void *opaque_data)
{
  imp->out_of_band_function= out_of_band_function;
  imp->out_of_band_opaque= opaque_data;
}

bool sanity::maybe_write_to_out_of_band_handler(char channel, std::string const& str)
{
  if (imp->out_of_band_function)
    {
      (*imp->out_of_band_function)(channel, str, imp->out_of_band_opaque);
      return true;
    }
  return false;
}

template <> void
dump(string const & obj, string & out)
{
  out = obj;
}
template<> void
dump(char const * const & obj, string & out)
{
  out = obj;
}
template<> void
dump(bool const & obj, string & out)
{
  out = (obj ? "true" : "false");
}
template <> void
dump(int const & val, string & out)
{
  out = lexical_cast<string>(val);
}
template <> void
dump(unsigned int const & val, string & out)
{
  out = lexical_cast<string>(val);
}
template <> void
dump(long const & val, string & out)
{
  out = lexical_cast<string>(val);
}
template <> void
dump(unsigned long const & val, string & out)
{
  out = lexical_cast<string>(val);
}
#ifdef USING_LONG_LONG
template <> void
dump(long long const & val, string & out)
{
  out = lexical_cast<string>(val);
}
template <> void
dump(unsigned long long const & val, string & out)
{
  out = lexical_cast<string>(val);
}
#endif

void
sanity::print_var(std::string const & value, char const * var,
                  char const * file, int const line, char const * func)
{
  inform_message((FL("----- begin '%s' (in %s, at %s:%d)")
                  % var % func % file % line).str());
  inform_message(value);
  inform_message((FL("-----   end '%s' (in %s, at %s:%d)\n\n")
                  % var % func % file % line).str());
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

format_base::format_base(char const * pattern, bool use_locale)
  : pimpl(use_locale ? new impl(pattern, get_user_locale())
                     : new impl(pattern))
{}

format_base::format_base(std::string const & pattern, bool use_locale)
  : pimpl(use_locale ? new impl(pattern, get_user_locale())
                     : new impl(pattern))
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

void format_base::put_and_flush_signed(s64 const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(s32 const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(s16 const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(s8  const & s) const { pimpl->fmt % s; }

void format_base::put_and_flush_unsigned(u64 const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(u32 const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(u16 const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(u8  const & u) const { pimpl->fmt % u; }

void format_base::put_and_flush_float(float const & f) const { pimpl->fmt % f; }
void format_base::put_and_flush_double(double const & d) const { pimpl->fmt % d; }

std::string
format_base::str() const
{
  return pimpl->fmt.str();
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
