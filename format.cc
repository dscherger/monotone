// copyright (C) 2005 R.Ghetta <birrachiara@tin.it>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <app_state.hh>
#include <revision.hh>
#include <transforms.hh>
#include <format.hh>

// ---------------------- formatting functor ----------------------------
// IMPORTANT: to complete formatting, it *must* go out of scope (i.e. its destructor
// called)
FormatFunc::FormatFunc(std::ostream &out, app_state &app)
{
  if (app.xml_enabled)
    fmt = std::auto_ptr<BaseFormatter>(new XMLFormatter(out, app));
  else 
    fmt = std::auto_ptr<BaseFormatter>(new PrintFormatter(out, app, app.format_string));
}

FormatFunc::~FormatFunc()
{
}
   
// ---------------------- base formatter ----------------------------
BaseFormatter::BaseFormatter(app_state &a):
app(a)
{
}

BaseFormatter::~BaseFormatter()
{
}

// ---------------------- format string support ----------------------------
PrintFormatter::PrintFormatter(std::ostream & o, app_state &a, const utf8 &fmt):
BaseFormatter(a),
out(o),
fmtstr(fmt),
startpoint(fmtstr().begin ())
{
}

PrintFormatter::~PrintFormatter()
{
}

void
PrintFormatter::print_cert (std::vector < revision < cert > >&certs, const std::string &name,
                            bool from_start, bool from_end, const std::string &sep)
{
  for (std::vector < revision < cert > >::const_iterator i = certs.begin ();
       i != certs.end (); ++i)
    {
      if (i->inner ().name () == name)
        {
          cert_value tv;
          decode_base64 (i->inner ().value, tv);
          std::string::size_type f = 0;
          std::string::size_type l = std::string::npos;
          if (from_start)
            l = tv().find_first_of(sep);
          if (from_end) {
            f = tv().find_last_of(sep);
            if (f == std::string::npos) f = 0;
          }
          out << tv().substr(f, l);
          return;
        }
    }
}

void
PrintFormatter::print_cset_ancestor(const std::string::const_iterator &startfmt, 
                                    const std::string::const_iterator &e,
                                    const revision_id &rid)
{
  std::string::const_iterator i(startfmt);
  while (i != e)
    {
    if ((*i) == '%')
      {
        ++i;
        if (i == e)
          break;
        switch (*i)
          {
          case 'f':
            out << rid.inner ()();
            break;
          case '%':
            out << '%';
            break;
          default:
            N (false, F ("invalid ancestor format specifier '%%%c'\n") % *i);
         }
      }
    else if ( (*i) == '\\')
      handle_control(i, e);
    else
      out << (*i);
    
    ++i;
    }
}

void
PrintFormatter::print_cset_single(const std::string::const_iterator &startfmt, 
                                  const std::string::const_iterator &e,
                                  const std::set<file_path> &data)
{
  for (std::set<file_path>::const_iterator f = data.begin (); f != data.end (); ++f)
    {
    std::string::const_iterator i(startfmt);
    while (i != e)
      {
      if ((*i) == '%')
        {
          ++i;
          if (i == e)
            break;
          switch (*i)
            {
            case 'f':
              out << (*f)();
              break;
            case '%':
              out << '%';
              break;
            default:
              N (false, F ("invalid file format specifier '%%%c'\n") % *i);
           }
        }
      else if ( (*i) == '\\')
        handle_control(i, e);
      else
        out << (*i);
      
      ++i;
      }
    }        
}

void
PrintFormatter::print_cset_pair(const std::string::const_iterator &startfmt, 
                                const std::string::const_iterator &e,
                                const std::map<file_path, file_path> &data)
{
  for (std::map<file_path, file_path>::const_iterator f = data.begin (); f != data.end (); ++f)
    {
    std::string::const_iterator i(startfmt);
    while (i != e)
      {
      if ((*i) == '%')
        {
          ++i;
          if (i == e)
            break;
          switch (*i)
            {
            case 'o':
              out << f->first();
              break;
            case 'f':
              out << f->second();
              break;
            case '%':
              out << '%';
              break;
            default:
              N (false, F ("invalid rename format specifier '%%%c'\n") % *i);
           }
        }
      else if ( (*i) == '\\')
        handle_control(i, e);
      else
        out << (*i);
      ++i;
      }
    }        
}

std::string::const_iterator
PrintFormatter::find_cset_fmt_end(std::string::const_iterator i, 
                                   const std::string::const_iterator &e)
{
  int level = 1; // we are already inside a parens
  while (i != e && level>0)
    {
      switch (*i)
        {
        case '\\':
        case '%':
           // just skip the next char
           ++i;
           break;
        case '{':
           // another parenthesis, inner level ...
           ++level;
           break;
        case '}': 
           // closing of a level
           --level;
           break;
        }
        if (i != e && level)
          ++i; // next char
    }
  N(i != e && *i =='}',F ("invalid changeset format expression"));
  return i;
}

std::string::const_iterator
PrintFormatter::handle_cset(const std::string::const_iterator &startfmt, 
                             const std::string::const_iterator &endfmt, 
                             const revision_set & rev)
{
    std::string::const_iterator fmt_i(startfmt);
   
    FMTIDX curfmt = decode_cset_fmtid(fmt_i);
    N(curfmt != FMTIDX_REVISION, F("invalid changeset format specifier %%%c") % *fmt_i);
    N(++fmt_i != endfmt && *fmt_i == '{',F ("missing '{' following changeset format specifier"));
    N(++fmt_i != endfmt,F ("a format string could not end with '{'"));
   
    std::string::const_iterator fmt_e(find_cset_fmt_end(fmt_i, endfmt));
  
    std::string::const_iterator i; 
    for (edge_map::const_iterator e = rev.edges.begin ();
       e != rev.edges.end (); ++e)
    {
      change_set const &cs = edge_changes (e);
      change_set::path_rearrangement const &pr = cs.rearrangement;

      i = fmt_i; // reset the starting point
      switch (curfmt)
        {
        case FMTIDX_ANCESTORS:
          print_cset_ancestor(i, fmt_e, edge_old_revision (e));
          break;
        case FMTIDX_DELFILES:
          print_cset_single(i, fmt_e, pr.deleted_files);
          break;
        case FMTIDX_DELDIRS:
          print_cset_single(i, fmt_e, pr.deleted_dirs);
          break;
        case FMTIDX_ADDFILES:
          print_cset_single(i, fmt_e, pr.added_files);
          break;
        case FMTIDX_MODFILES:
          {
            std::set<file_path> modified_files;
            for (change_set::delta_map::const_iterator c = cs.deltas.begin ();
                 c != cs.deltas.end (); c++)
            {
              if (pr.added_files.find (c->first ()) == pr.added_files.end ())
                modified_files.insert (c->first ());
            }
            print_cset_single(i, fmt_e, modified_files);
          }
          break;
        case FMTIDX_RENFILES:
          print_cset_pair(i, fmt_e, pr.renamed_files);
          break;
        case FMTIDX_RENDIRS:
          print_cset_pair(i, fmt_e, pr.renamed_dirs);
          break;
        
        default:
          break;
      }
    }
    // go to end position
    return fmt_e;
}

void 
PrintFormatter::handle_control(std::string::const_iterator &it, const std::string::const_iterator &end)
{
  ++it;
  if (it == end)
    return;
  switch (*it)
    {
    case '\\':
      out << '\\';
      break;
    case '@':
      out << '@';
      break;
    case 'n':
      out<< std::endl;
      break;
    case 't':
      out << '\t';
      break;
    case 'a':
      out << '\a';
      break;
    case 'b':
      out << '\b';
      break;
    case 'f':
      out << '\f';
      break;
    case 'r':
      out << '\r';
      break;
    case 'v':
      out << '\v';
      break;
    default:
      N (false, F ("\ninvalid control char %c\n") % (*it));
      return;
    }
}

PrintFormatter::FMTIDX
PrintFormatter::decode_cset_fmtid(const std::string::const_iterator &i)
{
    switch (*i)
    {
      case 'P': // ancestors
        return FMTIDX_ANCESTORS;
      case 'D': // deleted files
        return FMTIDX_DELFILES;
      case 'R': // renamed files
        return FMTIDX_RENFILES;
      case 'A': // added files
        return FMTIDX_ADDFILES;
      case 'M': // modified files
        return FMTIDX_MODFILES;
      case 'E': // deleted dirs
        return FMTIDX_DELDIRS;
      case 'C': // renamed dirs
        return FMTIDX_RENDIRS;
    }
    
    // everything else is handled as revision fmt
    return FMTIDX_REVISION; 
}

void
PrintFormatter::apply(const revision_id & rid)
{
  if (null_id(rid))
     return; // not a "real" revision, exiting
  
  if (!app.db.revision_exists (rid))
    {
      L (F ("revision %s does not exist in db\n") % rid);
      return;
    }

  revision_set rev;
  app.db.get_revision (rid, rev);

  std::vector < revision < cert > >certs;
  app.db.get_revision_certs (rid, certs);
  erase_bogus_certs (certs, app);

  std::string::const_iterator i = startpoint;
  std::string::const_iterator e = fmtstr().end();
  while (i != e)
    {
      if ((*i) == '%')
        {
          ++i;
          if (i == e)
            return;

          bool short_form = false;
          if (*i == 's') {
            short_form = true;
            ++i;
            N(i != e, F("%%s is not a valid format specifier"));
          }

          switch (*i)
            {
             case '%':
               N(!short_form, F("no short form for '%%%%'"));
               out << '%';
               break;
            case 'd':
              print_cert (certs, date_cert_name, short_form, false, "T");
              break;
            case 'a':
              print_cert (certs, author_cert_name, short_form, false, "@");
              break;
            case 't':
              N(!short_form, F("no short form for tag specifier"));
              print_cert (certs, tag_cert_name);
              break;
            case 'l':
              N(!short_form, F("no short form for changelog specifier"));
              print_cert (certs, changelog_cert_name);
              break;
            case 'e':
              N(!short_form, F("no short form for comment specifier"));
              print_cert (certs, comment_cert_name);
              break;
            case 'r':
              N(!short_form, F("no short form for testresult specifier"));
              print_cert (certs, testresult_cert_name);
              break;
            case 'b':
              print_cert (certs, branch_cert_name, false, short_form, ".");
              break;
            case 'm':
              if (short_form)
                out << rev.new_manifest.inner()().substr(0, 8);
              else
                out << rev.new_manifest.inner()();
              break;
            case 'i':
              if (short_form)
                out << rid.inner()().substr(0, 8);
              else
                out << rid.inner()();
              break;
            case '+':
              N(!short_form, F("no short form for the '%%+' formatting specifier"));
              startpoint = ++i; // predispone next starting point
              N(startpoint != e, F("A format string can't terminate with '%%+'"));
              return; // exit directly from the function, skipping the rest
            default:
              N(!short_form, F("no short form for changelog specifier"));
              // unrecognized specifier, perhaps is a changeset one ?
              i = handle_cset(i, e, rev);
              break;
            }
        }
      else if ( (*i) == '\\')
        handle_control(i, e);
      else
        out << (*i);
      
      I(i!=e); // just to make sure
      ++i;
    }

    // resets fmt str starting point
    startpoint = fmtstr().begin ();
}


// --------------- XML support -----------------

XMLWriter::XMLWriter (std::ostream & o):
out (o),
open_tags(),
decl_emitted(false),
empty_tag(false)
{
}

XMLWriter::~XMLWriter ()
{
  I (open_tags.size () == 0);	// forgot to closing some tags ?
}

void
XMLWriter::encode(const utf8 & opq)
{
  for (std::string::const_iterator i = opq().begin(); i != opq().end(); ++i)
  {
     switch ((*i))
    {
        case '<': out << "&lt;"; break;
        case '>': out << "&gt;"; break;
        case '&': out << "&amp;"; break;
        case '"': out << "&quot;"; break;
        case '\'': out << "&apos;"; break;
        default: out << *i; break;
    }
  }     
}

void
XMLWriter::tag (const utf8 & tagname)
{
  if (!decl_emitted)
  {
     out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>" << std::endl;
     decl_emitted=true;
  }

  if (empty_tag)
    out << ">" << std::endl;

  out << "<" << tagname;
  open_tags.push_back (tagname);
  empty_tag = true; // right now, the tag is empty
}

void
XMLWriter::end ()
{
  I (open_tags.size () > 0);
  if (empty_tag)
    out << "/>" << std::endl;
  else  
    out << "</" << open_tags.back () << ">" << std::endl;
  empty_tag=false; // the containing tag is not empty1
  open_tags.pop_back ();
}

void
XMLWriter::attr(const utf8 & attrname, const utf8 &value)
{
  I (open_tags.size () > 0);
  I (empty_tag);
  
  out << " " << attrname << "=\"";
  encode(value);
  out << "\"";
}

void
XMLWriter::cdata (const utf8 & opq)
{
  I (open_tags.size () > 0);
  
  if (empty_tag)
  {
     // tag was empty until now, close it
     out << ">" << std::endl;
     empty_tag=false; 
  }
  encode(opq);
}

// ---------------- the xml formatter -----------------------
XMLFormatter::XMLFormatter(std::ostream &out, app_state &a):
BaseFormatter(a),
xw(out)
{
   xw.tag("monotone"); // docroot
}

XMLFormatter::~XMLFormatter()
{
   xw.end();
}

void
XMLFormatter::xml_revision_id(const revision_id & rid)
{
  xw.tag("id");
  xw.cdata (rid.inner ()());	
  xw.end();
}

void
XMLFormatter::xml_manifest(const manifest_id & mid)
{
  xw.tag("manifest");
  xw.cdata (mid.inner ()());	
  xw.end();
}

void
XMLFormatter::xml_file_id(const file_id & fid)
{
  xw.tag("file-id");
  xw.cdata (fid.inner ()());	
  xw.end();
}

// dumps all *valid* certs associated to a revision
// FIXME: could be useful to optionally dump an invalid cert marking it with another
// tag (invalid_cert) or an attribute (valid=true/false)
void
XMLFormatter::xml_certs (const revision_id & rid)
{
  std::vector < revision < cert > >certs;

  app.db.get_revision_certs (rid, certs);
  erase_bogus_certs (certs, app);
  for (std::vector < revision < cert > >::const_iterator i = certs.begin ();
       i != certs.end (); ++i)
    {
      xw.tag ("cert");
      xw.attr("name", i->inner ().name ());

      xw.tag ("value");
      cert_value tv;
      decode_base64 (i->inner ().value, tv);
      xw.cdata (tv ());
      xw.end ();

      xw.tag ("key-id");
      xw.cdata (i->inner ().key ());
      xw.end ();

      xw.tag ("signature");
      xw.cdata (i->inner ().sig ());	// only makes sense if encoded
      xw.end ();

      xw.end ();
    }

}

void
XMLFormatter::xml_ancestors(const revision_set & rev)
{
  for (edge_map::const_iterator e = rev.edges.begin ();
       e != rev.edges.end (); ++e)
    {
      const revision_id &rid = edge_old_revision (e);
      xw.tag("ancestor");
      xml_revision_id(rid);
      xw.end();
    }
}

void
XMLFormatter::xml_delta(const file_path& f, const change_set::delta_map &dm)
{
  change_set::delta_map::const_iterator i = dm.find(f);
  if (i != dm.end())
  {
     xw.tag("delta");
     xw.tag("old");
     xml_file_id( i->second.first);
     xw.end();
     xw.tag("new");
     xml_file_id( i->second.second);
     xw.end();
     xw.end();
  }
}

void
XMLFormatter::xml_changeset(const revision_set & rev)
{
  xml_ancestors(rev);

  xw.tag("changeset");

  for (edge_map::const_iterator e = rev.edges.begin ();
       e != rev.edges.end (); ++e)
    {
      change_set const &cs = edge_changes (e);
      change_set::path_rearrangement const &pr = cs.rearrangement;

      std::set<file_path>::const_iterator f;
      std::map<file_path, file_path>::const_iterator m;
      for (f=pr.deleted_dirs.begin(); f != pr.deleted_dirs.end() ; ++f)
      {
         xw.tag("delete-dir");
         xw.attr("name", (*f)());
         xml_delta(*f, cs.deltas);
         xw.end();
      }
      for (m=pr.renamed_dirs.begin(); m != pr.renamed_dirs.end() ; ++m)
      {
         xw.tag("rename-dir");
         xw.attr("name", m->second());
         xw.attr("old-name", m->first());
         xml_delta(m->second, cs.deltas);
         xml_delta(m->first, cs.deltas);
         xw.end();
      }
      for (f=pr.added_files.begin(); f != pr.added_files.end() ; ++f)
      {
         xw.tag("add-file");
         xw.attr("name", (*f)());
         xml_delta(*f, cs.deltas);
         xw.end();
      }
      for (f=pr.deleted_files.begin(); f != pr.deleted_files.end() ; ++f)
      {
         xw.tag("delete_file");
         xw.attr("name", (*f)());
         xml_delta(*f, cs.deltas);
         xw.end();
      }
      for (m=pr.renamed_files.begin(); m != pr.renamed_files.end() ; ++m)
      {
         xw.tag("rename_file");
         xw.attr("name", m->second());
         xw.attr("old-name", m->first());
         xml_delta(m->second, cs.deltas);
         xml_delta(m->first, cs.deltas);
         xw.end();
      }
      
      std::set < file_path > modified_files;
      for (change_set::delta_map::const_iterator i = cs.deltas.begin ();
           i != cs.deltas.end (); i++)
      {
        if (pr.added_files.find (i->first ()) == pr.added_files.end ())
          modified_files.insert (i->first ());
      }
      for (f=modified_files.begin(); f != modified_files.end() ; ++f)
      {
         xw.tag("change-file");
         xw.attr("name", (*f)());
         xml_delta(*f, cs.deltas);
         xw.end();
      }
      
    }

    xw.end();
}

// dumps recursively a revision
void
XMLFormatter::apply(const revision_id & rid)
{
  if (null_id(rid))
     return; // not a "real" revision, exiting

  if (!app.db.revision_exists (rid))
    {
      L (F ("revision %s does not exist in db\n") % rid);
      return;
    }

  revision_set rev;
  app.db.get_revision (rid, rev);

  xw.tag ("revision");
  xml_revision_id(rid);
  xml_manifest(rev.new_manifest);
  xml_certs (rid);
  xml_changeset(rev);
   
  xw.end ();
}
