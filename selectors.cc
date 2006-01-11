// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "selectors.hh"
#include "sanity.hh"
#include "app_state.hh"
#include "constants.hh"

namespace selectors
{

  static void
  decode_selector(std::string const & orig_sel,
                  selector_type & type,
                  bool & get_heads,
                  std::string & sel,
                  app_state & app,
                  bool first_selector)
  {
    sel = orig_sel;

    L(F("decoding selector '%s'\n") % sel);

    if (first_selector)
      {
        if (sel[0] == 'H' && sel[1] == ':')
          {
            get_heads = true;
            sel.erase(0,2);
          }
      }

    std::string tmp;
    if (sel.size() < 2 || sel[1] != ':')
      {
        if (!app.lua.hook_expand_selector(sel, tmp))
          {
            L(F("expansion of selector '%s' failed\n") % sel);
          }
        else
          {
            P(F("expanded selector '%s' -> '%s'\n") % sel % tmp);
            sel = tmp;
          }
      }

    if (sel.size() >= 2 && sel[1] == ':')
      {
        switch (sel[0])
          {
          case 'a':
            type = sel_author;
            break;
          case 'b':
            type = sel_branch;
            break;
          case 'h':
            type = sel_head;
            break;
          case 'd':
            type = sel_date;
            break;
          case 'i':
            type = sel_ident;
            break;
          case 't':
            type = sel_tag;
            break;
          case 'c':
            type = sel_cert;
            break;
          case 'l':
            type = sel_later;
            break;
          case 'e':
            type = sel_earlier;
            break;
          default:
            W(F("unknown selector type: %c\n") % sel[0]);
            break;
          }
        sel.erase(0,2);

        /* a selector date-related should be validated */
        if (sel_date==type || sel_later==type || sel_earlier==type)
          {
            N (app.lua.hook_expand_date(sel, tmp), 
               F ("selector '%s' is not a valid date\n") % sel);

            if (tmp.size()<8 && (sel_later==type || sel_earlier==type))
              tmp += "-01T00:00:00";
            else if (tmp.size()<11 && (sel_later==type || sel_earlier==type))
              tmp += "T00:00:00";
            N(tmp.size()==19 || sel_date==type, F ("selector '%s' is not a valid date (%s)\n") % sel % tmp);
            if (sel != tmp)
              {
                P (F ("expanded date '%s' -> '%s'\n") % sel % tmp);
                sel = tmp;
              }
          }
      }
  }

  void
  complete_selector(std::string const & orig_sel,
                    std::vector<std::pair<selector_type, std::string> > const & limit,
                    selector_type & type,
                    bool & get_heads,
                    std::set<std::string> & completions,
                    app_state & app,
                    bool first_selector)
  {
    std::string sel;
    decode_selector(orig_sel, type, get_heads, sel, app, first_selector);
    app.db.complete(type, sel, limit, completions);
  }

  std::vector<std::pair<selector_type, std::string> >
  parse_selector(std::string const & str,
                 bool & get_heads,
                 app_state & app)
  {
    std::vector<std::pair<selector_type, std::string> > sels;

    // this rule should always be enabled, even if the user specifies
    // --norc: if you provide a revision id, you get a revision id.
    if (str.find_first_not_of(constants::legal_id_bytes) == std::string::npos
        && str.size() == constants::idlen)
      {
        sels.push_back(std::make_pair(sel_ident, str));
      }
    else
      {
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> slash("/");
        tokenizer tokens(str, slash);

        std::vector<std::string> selector_strings;
        copy(tokens.begin(), tokens.end(), back_inserter(selector_strings));

        bool first = true;
        for (std::vector<std::string>::const_iterator i = selector_strings.begin();
             i != selector_strings.end(); ++i)
          {
            std::string sel;
            selector_type type = sel_unknown;

            decode_selector(*i, type, get_heads, sel, app, first);
            sels.push_back(std::make_pair(type, sel));
            first = false;
          }
      }

    return sels;
  }

}; // namespace selectors
