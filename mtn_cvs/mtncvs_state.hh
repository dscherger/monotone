#ifndef T78D1F4C_BB8F_4043_96E3_3E2A5097F77F
#define T78D1F4C_BB8F_4043_96E3_3E2A5097F77F

// Copyright (C) 2006 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "options.hh"
#include <app_state.hh>
#include <mtn_automate.hh>

struct mtncvs_state : private app_state, public mtn_automate
{ /*bool full;
  utf8 since;
  std::vector<revision_id> revisions;
  utf8 mtn_binary;
  std::vector<utf8> mtn_options;
  utf8 branch;
  utf8 domain; */

  options opts;
  
//  mtncvs_state() : full(), mtn_binary("mtn"), domain("cvs") {}
 
// to access the private base class (only to pass it around)
  app_state& downcast() { return *this; }
  static mtncvs_state& upcast(app_state &app) 
  { return static_cast<mtncvs_state&>(app); }
  
  void open();
  void dump();
};

#endif
