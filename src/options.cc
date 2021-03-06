// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>

#include "charset.hh"
#include "constants.hh"
#include "options.hh"
#include "platform.hh"
#include "sanity.hh"
#include "ui.hh"

using std::list;
using std::map;
using std::set;
using std::string;

using option::bad_arg_internal;

template<typename T>
bool has_arg() { return true; }
template<>
bool has_arg<bool>() { return false; }



std::map<options::static_options_fun, std::set<options::static_options_fun> > &
options::children()
{
  static map<static_options_fun, set<static_options_fun> > val;
  static bool first(true);
  if (first)
    {
#     define OPTSET(name) \
      val[&options::opts::all_options].insert(&options::opts::name);
#     define OPTVAR(optset, type, name, default_)
#     define OPTION(optset, name, hasarg, optstring, description) \
      val[&options::opts:: optset].insert(&options::opts:: name ## _opt); \
      val[&options::opts::all_options].insert(&options::opts::name ## _opt);
#     define OPTSET_REL(parent, child) \
      val[&options::opts:: parent].insert(&options::opts:: child);
#     define HIDE(option)
#     define DEPRECATE(option, reason, deprecated_in, will_remove_in)

#     include "options_list.hh"

#     undef OPTSET
#     undef OPTVAR
#     undef OPTION
#     undef OPTSET_REL
#     undef HIDE
#     undef DEPRECATE

      first = false;
    }
  return val;
}

std::map<options::static_options_fun, std::list<void(options::*)()> > &
options::var_membership()
{
  static map<static_options_fun, std::list<void(options::*)()> > val;
  static bool first(true);
  if (first)
    {
#     define OPTSET(name)
#     define OPTVAR(optset, type, name, default_) \
      val[&opts:: optset ].push_back(&options::reset_ ## name );
#     define OPTION(optset, name, hasarg, optstring, description)
#     define OPTSET_REL(parent, child)
#     define HIDE(option)
#     define DEPRECATE(option, reason, deprecated_in, will_remove_in)

#     include "options_list.hh"

#     undef OPTSET
#     undef OPTVAR
#     undef OPTION
#     undef OPTSET_REL
#     undef HIDE
#     undef DEPRECATE

      first = false;
    }
  return val;
}

std::map<options::static_options_fun, bool> &
options::hidden()
{
  static map<static_options_fun, bool> val;
  static bool first(true);
  if (first)
    {
#     define OPTSET(name)
#     define OPTVAR(optset, type, name, default_)
#     define OPTION(optset, name, hasarg, optstring, description)
#     define OPTSET_REL(parent, child)
#     define HIDE(option)                       \
      val[&opts:: option ## _opt ] = true;
#     define DEPRECATE(option, reason, deprecated_in, will_remove_in)

#     include "options_list.hh"

#     undef OPTSET
#     undef OPTVAR
#     undef OPTION
#     undef OPTSET_REL
#     undef HIDE
#     undef DEPRECATE
      first = false;
    }
  return val;
}

std::map<options::static_options_fun, char const *> &
options::deprecated()
{
  static map<static_options_fun, char const *> val;
  static bool first(true);
  if (first)
    {
#     define OPTSET(name)
#     define OPTVAR(optset, type, name, default_)
#     define OPTION(optset, name, hasarg, optstring, description)
#     define OPTSET_REL(parent, child)
#     define HIDE(option)
#     define DEPRECATE(option, reason, deprecated_in, will_remove_in)   \
      val[&opts:: option ## _opt ] = reason ;

#     include "options_list.hh"

#     undef OPTSET
#     undef OPTVAR
#     undef OPTION
#     undef OPTSET_REL
#     undef HIDE
#     undef DEPRECATE
      first = false;
    }
  return val;
}


options::options()
{
# define OPTSET(name)
# define OPTVAR(group, type, name, default_)    \
    name = type ( default_ );
# define OPTION(optset, name, hasarg, optstring, description)   \
    name ## _given = false;
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
}

const options &
options::operator = (options const & other)
{
# define OPTSET(name)
# define OPTVAR(group, type, name, default_)    \
    name = other.name;
# define OPTION(optset, name, hasarg, optstring, description)   \
    name ## _given = other.name ## _given;
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

  return *this;
}

static options::options_type
collect_children(options::static_options_fun opt)
{
  options::options_type out;
  set<options::static_options_fun> const & ch = options::children()[opt];
  for (set<options::static_options_fun>::const_iterator i = ch.begin();
       i != ch.end(); ++i)
    {
      if (*i != opt)
        out = out | (*(*i))();
    }
  return out;
}

void options::reset_optset(options::static_options_fun opt)
{
  list<void(options::*)()> const & vars = var_membership()[opt];
  for (list<void(options::*)()>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      (this->*(*i))();
    }
}

options::options_type const & options::opts::none()
{
  static options::options_type val;
  return val;
}

options::options_type const & options::opts::all_options()
{
  static options::options_type val = collect_children(&options::opts::all_options);
  return val;
}

# define OPTSET(name) \
  options::options_type const & options::opts::name()                   \
  {                                                                     \
    static options::options_type val =                                  \
      collect_children(&options::opts::name)                            \
      | options::option_type("", #name, false, 0,                       \
                             &options::reset_optset_ ## name ,          \
                             true, 0);                                  \
    return val;                                                         \
  }                                                                     \
  void options::reset_optset_ ## name ()                                \
  {                                                                     \
    reset_optset(&opts:: name);                                         \
  }

# define OPTVAR(optset, type, name, default_)                 \
  void options::reset_ ## name ()                             \
  {                                                           \
    name = type ( default_ );                                 \
  }

# define OPTION(optset, name, hasarg, optstring, description)           \
  options::options_type const & options::opts::name ## _opt()           \
  {                                                                     \
    static options::options_type val(optstring,                         \
                                     description, hasarg,               \
                                     &options::set_ ## name ,           \
                                     &options::reset_opt_ ## name ,     \
                                     hidden()[&opts:: name ## _opt],    \
                                     deprecated()[&opts:: name ## _opt]); \
    return val;                                                         \
  }                                                                     \
  void options::reset_opt_ ## name ()                                   \
  {                                                                     \
    name ## _given = false;                                             \
    reset_optset(&opts:: optset);                                       \
  }                                                                     \
  void options::set_ ## name (std::string arg)                          \
  {                                                                     \
    name ## _given = true;                                              \
    real_set_ ## name (arg);                                            \
  }                                                                     \
  void options::real_set_ ## name (std::string arg)

# define OPTSET_REL(parent, child)
# define HIDE(option)
# define DEPRECATE(option, reason, deprecated_in, will_remove_in)

#define option_bodies
# include "options_list.hh"
#undef option_bodies

# undef OPTSET
# undef OPTVAR
# undef OPTION
# undef OPTSET_REL
# undef HIDE
# undef DEPRECATE

option::option_set<options>
operator | (option::option_set<options> const & opts,
            option::option_set<options> const & (*fun)())
{
  return opts | fun();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
