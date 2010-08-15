// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __OPTIONS_HH__
#define __OPTIONS_HH__

/*
 * This defines 'struct options', which includes the variables and options
 * defined in options_list.hh as members. Options and optsets are available
 * statically as options::opts::<name>, and option variables are available
 * as options::<name>.
 */

#include <algorithm>
#include <list>
#include <climits>

#include "branch_name.hh"
#include "option.hh"
#include "paths.hh"
#include "dates.hh"

#include "lexical_cast.hh"

template<long low>
class restricted_long
{
  long value;
public:
  restricted_long()
  {
    if (-1 < low)
      value = -1;
    else
      value = low - 1;
  }
  restricted_long(std::string const & x, origin::type o)
  {
    value = boost::lexical_cast<long>(x);
    if (value < low)
      throw option::bad_arg_internal((F("must not be less than %d") % low).str());
  }
  operator long() const { return value; }
};

class enum_string
{
  std::vector<std::string> allowed;
  std::string allowed_str;
  std::string value;
public:
  enum_string() { }
  enum_string(std::string a) : allowed_str(a)
  {
    size_t x = a.find(",");
    while (x != std::string::npos)
      {
        allowed.push_back(a.substr(0, x));
        a.erase(0, x + 1);
        x = a.find(",");
      }
    allowed.push_back(a);
    I(allowed.size() >= 2);
    value = idx(allowed, 0);
  }
  void set(std::string const & v)
  {
    if (std::find(allowed.begin(), allowed.end(), v) == allowed.end())
      {
        throw option::bad_arg_internal((F("must be one of the following: %s")
                                        % allowed_str).str());
      }
    else
      value = v;
  }
  void unchecked_set(std::string const & v) { value = v; }
  operator std::string() const { return value; }
  bool operator<(enum_string const & e) const { return value < e.value; }
  bool operator==(std::string const & s) const { return value == s; }
  bool empty() const { return value.empty(); }
};
class enum_string_set
{
  std::string allowed;
  std::set<enum_string> value;
public:
  enum_string_set() { }
  enum_string_set(std::string const & a) : allowed(a) { }
  void add(std::string const & v)
  {
    enum_string e(allowed);
    e.set(v);
    value.insert(e);
  }
  operator std::set<enum_string>() const { return value; }
  std::set<enum_string>::const_iterator find(std::string const & s) const
  {
    enum_string e(allowed);
    e.set(s);
    return value.find(e);
  }
  std::set<enum_string>::const_iterator end() const
  {
    return value.end();
  }
};

struct options
{
  options();
  const options & operator = (options const & other);

  typedef boost::function<void()> reset_function;
  typedef option::option<options> option_type;
  typedef option::option_set<options> options_type;
  typedef options_type const & (*static_options_fun)();

  static std::map<static_options_fun, std::set<static_options_fun> > &children();
  static std::map<static_options_fun, std::list<void(options::*)()> > &var_membership();
  static std::map<static_options_fun, bool> &hidden();
  static std::map<static_options_fun, char const *> &deprecated();

  void reset_optset(static_options_fun opt);

  struct opts
  {
    static options_type const & none ();
    static options_type const & all_options ();
# define OPTSET(name)        \
    static options_type const & name ();

# define OPTVAR(optset, type, name, default_)

#define OPTION(optset, name, hasarg, optstring, description)     \
    static options_type const & name ## _opt ();

# define OPTSET_REL(parent, child)
# define HIDE(option)
# define DEPRECATE(option, reason, deprecated_in, will_remove_in)

# include "options_list.hh"

# undef OPTSET
# undef OPTVAR
# undef OPTION
# undef OPTSET_REL
# undef HIDE
# undef DEPRECATE
  };

# define OPTSET(name)                           \
  private:                                      \
  void reset_optset_ ## name ();

# define OPTVAR(optset, type, name, default_)   \
  public:                                       \
  type name;                                    \
  void reset_ ## name ();

#define OPTION(optset, name, hasarg, optstring, description)     \
  public:                                                        \
  bool name ## _given;                                           \
private:                                                         \
  void set_ ## name (std::string arg);                           \
  void real_set_ ## name (std::string arg);                      \
  void reset_opt_ ## name ();

# define OPTSET_REL(parent, child)
# define HIDE(option)
# define DEPRECATE(option, reason, deprecated_in, will_remove_in)

# include "options_list.hh"

# undef OPTSET
# undef OPTVAR
# undef OPTION
# undef OPTSET_REL
# undef HIDE
# undef DEPRECATE
};

option::option_set<options>
operator | (option::option_set<options> const & opts,
            option::option_set<options> const & (*fun)());

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
