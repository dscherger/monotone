// Copyright (C) 2010 Thomas Keller <me@thomaskeller.biz>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "colorizer.hh"
#include "platform.hh"

using std::string;
using std::map;
using std::make_pair;


string colorizer::purpose_to_name(colorizer::purpose const p) const
{
  switch (p)
  {
    case normal:
      return "normal";
    case reset:
      return "reset";

    case add:
      return "add";
    case change:
      return "change";
    case comment:
      return "comment";
    case encloser:
      return "encloser";
    case log_revision:
      return "log_revision";
    case remove:
      return "remove";
    case rename:
      return "rename";
    case rev_header:
      return "rev_header";
    case separator:
      return "separator";
    case set:
      return "set";
    case unset:
      return "unset";

    default:
      I(false); // should never get here
  }
}

std::pair<colorizer::purpose, boost::tuple<string, string, string> > colorizer::map_output_color(
  purpose const p)
{
  string fg, bg, style;
  string purpose_name = purpose_to_name(p);

  if (p == reset)
    {
      // the user doesn't need to know about reset - it's an implementation
      // detail for us to handle
      fg = bg = style = "";
    }
  else
    {
      lua.hook_get_output_color(purpose_name, fg, bg, style);
    }

  return std::make_pair(p, boost::make_tuple(fg_to_code(fg),
                                             bg_to_code(bg),
                                             style_to_code(style)));
}

string colorizer::fg_to_code(string const color) const
{
  if (color == "black")
    return "\033[30m";
  else if (color == "red")
    return "\033[31m";
  else if (color == "green")
    return "\033[32m";
  else if (color == "yellow")
    return "\033[33m";
  else if (color == "blue")
    return "\033[34m";
  else if (color == "magenta")
    return "\033[35m";
  else if (color == "cyan")
    return "\033[36m";
  else if (color == "white")
    return "\033[37m";
  else
    return "\033[39m"; // default
}

string colorizer::bg_to_code(string const color) const
{
  if (color == "black")
    return "\033[40m";
  else if (color == "red")
    return "\033[41m";
  else if (color == "green")
    return "\033[42m";
  else if (color == "yellow")
    return "\033[43m";
  else if (color == "blue")
    return "\033[44m";
  else if (color == "magenta")
    return "\033[45m";
  else if (color == "cyan")
    return "\033[46m";
  else if (color == "white")
    return "\033[47m";
  else
    return "\033[49m"; // default
}

string colorizer::style_to_code(string const style) const
{
  if (style == "none")
    return "\033[22m\033[23m\033[24m";
  else if (style == "bold")
    return "\033[1m";
  else if (style == "italic")
    return "\033[3m";
  else if (style == "underline")
    return "\033[4m";
  else
    return "\033[22m\033[23m\033[24m"; // all off
}

colorizer::colorizer(bool enable, lua_hooks & lh) 
  : lua(lh)
{
  if (!have_smart_terminal())
    enable = false;

  if (enable)
    {
      colormap.insert(map_output_color(normal));
      colormap.insert(map_output_color(reset));

      colormap.insert(map_output_color(add));
      colormap.insert(map_output_color(change));
      colormap.insert(map_output_color(comment));
      colormap.insert(map_output_color(encloser));
      colormap.insert(map_output_color(log_revision));
      colormap.insert(map_output_color(remove));
      colormap.insert(map_output_color(rename));
      colormap.insert(map_output_color(rev_header));
      colormap.insert(map_output_color(separator));
      colormap.insert(map_output_color(set));
      colormap.insert(map_output_color(unset));
    }
}

string
colorizer::colorize(string const & in, purpose p) const
{
  if (colormap.find(p) == colormap.end())
    return in;

   return get_format(p) + in + get_format(reset);
}

string
colorizer::get_format(purpose const p) const
{
  boost::tuple<string, string, string> format = colormap.find(p)->second;

  return format.get<0>() + format.get<1>() + format.get<2>();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
