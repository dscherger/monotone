// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __COMMANDS_HH__
#define __COMMANDS_HH__

#include "vector.hh"
#include "options.hh"
class app_state;
class utf8;

// this defines a global function which processes command-line-like things,
// possibly from the command line and possibly internal scripting if we ever
// bind tcl or lua or something in here

namespace commands {
  typedef std::vector< utf8 > command_id;
  class command;
  class automate;

  command_id make_command_id(std::string const & path);
  void explain_usage(command_id const & cmd, bool show_hidden, std::ostream & out);
  command_id complete_command(args_vector const & args);
  void remove_command_name_from_args(command_id const & ident,
                                     args_vector & args,
                                     size_t invisible_length = 1);
  void reapply_options(app_state & app,
                       command const * cmd,
                       command_id const & cmd_ident,
                       command const * subcmd = 0,
                       command_id const & subcmd_full_ident = command_id(),
                       size_t subcmd_invisible_length = 1,
                       args_vector const & subcmd_cmdline = args_vector(),
                       std::vector<std::pair<std::string, std::string> >
                       const * const separate_params = 0);
  // really no good place to put this
  // used by 'automate stdio' and automate_session::do_work
  void automate_stdio_shared_setup(app_state & app,
                                   std::vector<std::string> const & cmdline,
                                   std::vector<std::pair<std::string,std::string> >
                                   const & params,
                                   command_id & id,
                                   /* reference-to-pointer here is intentional */
                                   automate const * & acmd);
  void process(app_state & app, command_id const & ident,
               args_vector const & args);
  options::options_type command_options(command_id const & ident);
};

struct usage
{
  usage(commands::command_id const & w) : which(w) {}
  commands::command_id which;
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
