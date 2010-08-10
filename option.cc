// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "file_io.hh"
#include "option.hh"
#include "sanity.hh"

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace option {

option_error::option_error(std::string const & str)
 : std::invalid_argument((F("option error: %s") % str).str())
{}

unknown_option::unknown_option(std::string const & opt)
 : option_error((F("unknown option '%s'") % opt).str())
{}

missing_arg::missing_arg(std::string const & opt)
 : option_error((F("missing argument to option '%s'") % opt).str())
{}

extra_arg::extra_arg(std::string const & opt)
 : option_error((F("option '%s' does not take an argument") % opt).str())
{}

bad_arg::bad_arg(std::string const & opt, arg_type const & arg)
 : option_error((F("bad argument '%s' to option '%s'") % arg() % opt).str())
{}

bad_arg::bad_arg(std::string const & opt,
                 arg_type const & arg,
                 std::string const & reason)
 : option_error((F("bad argument '%s' to option '%s': %s")
                   % arg() % opt % reason).str())
{}

bad_arg_internal::bad_arg_internal(string const & str)
 : reason(str)
{}



void splitname(char const * f, string & name, string & n, string & cancel)
{
  string from(f);
  if (from.find("/") != string::npos)
    {
      string::size_type slash = from.find("/");
      cancel = from.substr(slash+1);
      from.erase(slash);
    }
  // from looks like "foo" or "foo,f"
  string::size_type comma = from.find(',');
  name = from.substr(0, comma);
  if (comma != string::npos)
    n = from.substr(comma+1, 1);
  else
    n = "";

  // "o" is equivalent to ",o"; it gives an option
  // with only a short name
  if (name.size() == 1)
    {
      I(n.empty());
      n = name;
      name = "";
    }
}


concrete_option::concrete_option()
  : has_arg(false)
{}

concrete_option::concrete_option(char const * names,
                                 char const * desc,
                                 bool arg,
                                 boost::function<void (std::string)> set,
                                 boost::function<void ()> reset,
                                 bool hide,
                                 char const * deprecate)
{
  description = desc;
  splitname(names, longname, shortname, cancelname);
  I((desc && desc[0]) || !longname.empty() || !shortname.empty());
  // not sure how to display if it can only be reset (and what would that mean?)
  I((!longname.empty() || !shortname.empty()) || cancelname.empty());
  // If an option has a name (ie, can be set), it must have a setter function
  I(set || (longname.empty() && shortname.empty()));
  // If an option can be canceled, it must have a resetter function
  I(reset || cancelname.empty());
  has_arg = arg;
  setter = set;
  resetter = reset;
  hidden = hide;
  deprecated = deprecate;
}

bool concrete_option::operator<(concrete_option const & other) const
{
  if (longname != other.longname)
    return longname < other.longname;
  if (shortname != other.shortname)
    return shortname < other.shortname;
  if (cancelname != other.cancelname)
    return cancelname < other.cancelname;
  return description < other.description;
}

concrete_option_set
operator | (concrete_option const & a, concrete_option const & b)
{
  return concrete_option_set(a) | b;
}

concrete_option_set::concrete_option_set()
{}

concrete_option_set::concrete_option_set(std::set<concrete_option> const & other)
  : options(other)
{}

concrete_option_set::concrete_option_set(concrete_option const & opt)
{
  options.insert(opt);
}

// essentially the opposite of std::bind1st
class discard_argument
{
  boost::function<void()> functor;
 public:
  discard_argument(boost::function<void()> const & from)
    : functor(from)
    {}
    void operator()(std::string const &)
    { return functor(); }
};

concrete_option_set &
concrete_option_set::operator()(char const * names,
                                char const * desc,
                                boost::function<void ()> set,
                                boost::function<void ()> reset,
                                bool hide,
                                char const * deprecate)
{
  options.insert(concrete_option(names, desc, false, discard_argument(set),
                                 reset, hide, deprecate));
  return *this;
}

concrete_option_set &
concrete_option_set::operator()(char const * names,
                                char const * desc,
                                boost::function<void (string)> set,
                                boost::function<void ()> reset,
                                bool hide,
                                char const * deprecate)
{
  options.insert(concrete_option(names, desc, true, set, reset, hide, deprecate));
  return *this;
}

concrete_option_set
concrete_option_set::operator | (concrete_option_set const & other) const
{
  concrete_option_set combined;
  std::set_union(options.begin(), options.end(),
                 other.options.begin(), other.options.end(),
                 std::inserter(combined.options, combined.options.begin()));
  return combined;
}

void concrete_option_set::reset() const
{
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      if (i->resetter)
        i->resetter();
    }
}

static void
tokenize_for_command_line(string const & from, args_vector & to)
{
  // Unfortunately, the tokenizer in basic_io is too format-specific
  to.clear();
  enum quote_type {none, one, two};
  string cur;
  quote_type type = none;
  bool have_tok(false);

  for (string::const_iterator i = from.begin(); i != from.end(); ++i)
    {
      if (*i == '\'')
        {
          if (type == none)
            type = one;
          else if (type == one)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '"')
        {
          if (type == none)
            type = two;
          else if (type == two)
            type = none;
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else if (*i == '\\')
        {
          if (type != one)
            ++i;
          E(i != from.end(), origin::user, F("Invalid escape in --xargs file"));
          cur += *i;
          have_tok = true;
        }
      else if (string(" \n\t").find(*i) != string::npos)
        {
          if (type == none)
            {
              if (have_tok)
                to.push_back(arg_type(cur, origin::user));
              cur.clear();
              have_tok = false;
            }
          else
            {
              cur += *i;
              have_tok = true;
            }
        }
      else
        {
          cur += *i;
          have_tok = true;
        }
    }
  if (have_tok)
    to.push_back(arg_type(cur, origin::user));
}

void concrete_option_set::from_command_line(int argc,
                                            char const * const * argv)
{
  args_vector arguments;
  for (int i = 1; i < argc; ++i)
    arguments.push_back(arg_type(argv[i], origin::user));
  from_command_line(arguments);
}

static concrete_option const &
getopt(map<string, concrete_option> const & by_name, string const & name)
{
  // try to match the option name as a whole first, so if the user
  // specified "--foo" and we have "--foo" and "--foo-bar", don't
  // display both choices
  map<string, concrete_option>::const_iterator i = by_name.find(name);
  if (i != by_name.end())
    return i->second;


  // try to find the option by partial name
  vector<string> candidates;
  for (i = by_name.begin(); i != by_name.end(); ++i)
    {
      if (i->first.find(name) == 0)
        candidates.push_back(i->first);
    }

  if (candidates.size() == 0)
    throw unknown_option(name);

  if (candidates.size() == 1)
    {
       i = by_name.find(candidates[0]);
       I(i != by_name.end());
       L(FL("expanding option '%s' to '%s'") % name % candidates[0]);
       return i->second;
    }

  string err = (F("option '%s' has multiple ambiguous expansions:")
                % name).str();

  for (vector<string>::const_iterator j = candidates.begin();
       j != candidates.end(); ++j)
    {
        i = by_name.find(*j);
        I(i != by_name.end());

        err += "\n--" + *j + " (";
        if (*j != i->second.longname)
          err += (F("negation of --%s") % i->second.longname).str();
        else
          err +=  + i->second.description;
        err += ")";
    }

  E(false, origin::user, i18n_format(err));
}

// helper for get_by_name
// Make sure that either:
//   * There are no duplicate options, or
//   * If we're only parsing options (and not applying them), any duplicates
//     are consistent WRT whether they take an option
typedef pair<map<string, concrete_option>::iterator, bool> by_name_res_type;
static void check_by_name_insertion(by_name_res_type const & res,
                                    concrete_option const & opt,
                                    concrete_option_set::preparse_flag pf)
{
  switch (pf)
    {
    case concrete_option_set::preparse:
      if (!res.second)
        {
          string const & name = res.first->first;
          concrete_option const & them = res.first->second;
          bool const i_have_arg = (name != opt.cancelname && opt.has_arg);
          bool const they_have_arg = (name != them.cancelname && them.has_arg);
          I(i_have_arg == they_have_arg);
        }
      break;
    case concrete_option_set::no_preparse:
      I(res.second);
      break;
    }
}

// generate an index that lets us look options up by name
static map<string, concrete_option>
get_by_name(std::set<concrete_option> const & options,
            concrete_option_set::preparse_flag pf)
{
  map<string, concrete_option> by_name;
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      if (!i->longname.empty())
        check_by_name_insertion(by_name.insert(make_pair(i->longname, *i)),
                                *i, pf);
      if (!i->shortname.empty())
        check_by_name_insertion(by_name.insert(make_pair(i->shortname, *i)),
                                *i, pf);
      if (!i->cancelname.empty())
        check_by_name_insertion(by_name.insert(make_pair(i->cancelname, *i)),
                                *i, pf);
    }
  return by_name;
}

void concrete_option_set::from_command_line(args_vector & args,
                                            preparse_flag pf)
{
  map<string, concrete_option> by_name = get_by_name(options, pf);

  bool seen_dashdash = false;
  for (args_vector::size_type i = 0; i < args.size(); ++i)
    {
      concrete_option o;
      string name;
      arg_type arg;
      bool is_cancel;
      bool separate_arg(false);
      if (idx(args,i)() == "--" || seen_dashdash)
        {
          if (!seen_dashdash)
            {
              seen_dashdash = true;
              continue;
            }
          name = "--";
          o = getopt(by_name, name);
          arg = idx(args,i);
          is_cancel = false;
        }
      else if (idx(args,i)().substr(0,2) == "--")
        {
          string::size_type equals = idx(args,i)().find('=');
          if (equals == string::npos)
            name = idx(args,i)().substr(2);
          else
            name = idx(args,i)().substr(2, equals-2);

          o = getopt(by_name, name);
          is_cancel = (name == o.cancelname);
          if ((!o.has_arg || is_cancel) && equals != string::npos)
            throw extra_arg(is_cancel ? o.cancelname : o.longname);

          if (o.has_arg && !is_cancel)
            {
              if (equals == string::npos)
                {
                  separate_arg = true;
                  if (i+1 == args.size())
                    throw missing_arg(o.longname);
                  arg = idx(args,i+1);
                }
              else
                arg = arg_type(idx(args,i)().substr(equals+1), origin::user);
            }
        }
      else if (idx(args,i)().substr(0,1) == "-")
        {
          name = idx(args,i)().substr(1,1);

          o = getopt(by_name, name);
          is_cancel = (name == o.cancelname);
          I(!is_cancel);
          if (!o.has_arg && idx(args,i)().size() != 2)
            throw extra_arg(o.shortname);

          if (o.has_arg)
            {
              if (idx(args,i)().size() == 2)
                {
                  separate_arg = true;
                  if (i+1 == args.size())
                    throw missing_arg(o.shortname);
                  arg = idx(args,i+1);
                }
              else
                arg = arg_type(idx(args,i)().substr(2), origin::user);
            }
        }
      else
        {
          name = "--";
          o = getopt(by_name, name);
          arg = idx(args,i);
          is_cancel = false;
        }

      if (name == "xargs" || name == "@")
        {
          // expand the --xargs in place
          data dat;
          read_data_for_command_line(arg, dat);
          args_vector fargs;
          tokenize_for_command_line(dat(), fargs);

          args.erase(args.begin() + i);
          if (separate_arg)
            args.erase(args.begin() + i);
          args.insert(args.begin()+i, fargs.begin(), fargs.end());
          --i;
        }
      else
        {
          if (separate_arg)
            ++i;
          try
            {
              if (o.deprecated)
                W(F("deprecated option '%s' used: %s")
                  % o.longname % gettext(o.deprecated));
              if (!is_cancel)
                {
                  if (o.setter)
                    o.setter(arg());
                }
              else
                {
                  if (o.resetter)
                    o.resetter();
                }
            }
          catch (boost::bad_lexical_cast)
            {
              throw bad_arg(o.longname, arg);
            }
          catch (bad_arg_internal & e)
            {
              if (e.reason == "")
                throw bad_arg(o.longname, arg);
              else
                throw bad_arg(o.longname, arg, e.reason);
            }
        }
    }
}

void concrete_option_set::from_key_value_pairs(vector<pair<string, string> > const & keyvals)
{
  map<string, concrete_option> by_name = get_by_name(options, no_preparse);

  for (vector<pair<string, string> >::const_iterator i = keyvals.begin();
       i != keyvals.end(); ++i)
    {
      string const & key(i->first);
      arg_type const & value(arg_type(i->second, origin::user));

      concrete_option o = getopt(by_name, key);
      bool const is_cancel = (key == o.cancelname);

      try
        {
          if (o.deprecated)
            W(F("deprecated option '%s' used: %s")
              % o.longname % gettext(o.deprecated));

          if (!is_cancel)
            {
              if (o.setter)
                o.setter(value());
            }
          else
            {
              if (o.resetter)
                o.resetter();
            }
        }
      catch (boost::bad_lexical_cast)
        {
          throw bad_arg(o.longname, value);
        }
      catch (bad_arg_internal & e)
        {
          if (e.reason == "")
            throw bad_arg(o.longname, value);
          else
            throw bad_arg(o.longname, value, e.reason);
        }
    }
}

// Get the non-description part of the usage string,
// looks like "--long [ -s ] <arg> / --cancel".
static string usagestr(concrete_option const & opt)
{
  string out;
  if (opt.longname == "--")
    return "";
  if (!opt.longname.empty() && !opt.shortname.empty())
    out = "--" + opt.longname + " [ -" + opt.shortname + " ]";
  else if (!opt.longname.empty())
    out = "--" + opt.longname;
  else if (!opt.shortname.empty())
    out = "-" + opt.shortname;

  if (out.empty())
    return out;

  if (opt.has_arg)
    out += " <arg>";

  if (!opt.cancelname.empty())
    {
      if (!out.empty())
        out += " / ";
      out += "--" + opt.cancelname;
    }

  return out;
}

void
concrete_option_set::get_usage_strings(vector<string> & names,
                                       vector<string> & descriptions,
                                       unsigned int & maxnamelen,
                                       bool show_hidden) const
{
  unsigned int namelen = 0; // the longest option name string
  names.clear();
  descriptions.clear();
  for (std::set<concrete_option>::const_iterator i = options.begin();
       i != options.end(); ++i)
    {
      if (i->hidden && !show_hidden)
        continue;
      if (i->deprecated)
        continue;
      string name = usagestr(*i);
      if (name.size() > namelen)
        namelen = name.size();
      names.push_back(name);
      descriptions.push_back(gettext(i->description));
    }
  maxnamelen = namelen;
}

} // namespace option



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
