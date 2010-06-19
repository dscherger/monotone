// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "options_applicator.hh"

#include "options.hh"
#include "sanity.hh"
#include "ui.hh"

class options_applicator_impl
{
public:
  options_applicator::for_what what;
  bool were_timestamps_enabled;
  int prev_verbosity;
  bool set_verbosity;
  user_interface::ticker_type tick_type;
};

options_applicator::options_applicator(options const & opts,
				       options_applicator::for_what what)
  : _impl(new options_applicator_impl())
{
  _impl->what = what;

  // --dump is handled in monotone.cc
  // --log is handled in monotone.cc

  _impl->were_timestamps_enabled = ui.enable_timestamps(opts.timestamps);

  // debug messages are not captured for automate, so don't allow
  // changing the debug-ness for automate commands
  if (what == for_primary_cmd ||
      (!global_sanity.debug_p() && (opts.verbosity < 2)))
    {
      _impl->prev_verbosity = global_sanity.set_verbosity(opts.verbosity);
      _impl->set_verbosity = true;
    }
  else
    _impl->set_verbosity = false;

  _impl->tick_type = ui.get_ticker_type();
  if (global_sanity.get_verbosity() < 0)
    ui.set_tick_write_nothing();
  else
    {
      if (opts.ticker == "none")
	ui.set_tick_write_nothing();
      else if (opts.ticker == "dot")
	ui.set_tick_write_dot();
      else if (opts.ticker == "count")
	ui.set_tick_write_count();
      else if (opts.ticker == "stdio")
	ui.set_tick_write_stdio();
      else
	I(opts.ticker.empty());
    }
}

options_applicator::~options_applicator()
{
  ui.enable_timestamps(_impl->were_timestamps_enabled);

  if (_impl->set_verbosity)
    global_sanity.set_verbosity(_impl->prev_verbosity);

  ui.set_ticker_type(_impl->tick_type);

  delete _impl;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
