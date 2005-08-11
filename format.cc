// copyright (C) 2005 R.Ghetta <birrachiara@tin.it>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <app_state.hh>
#include <revision.hh>
#include <transforms.hh>
#include <format.hh>

using namespace std;

// helper functions to navigate the given revision - shamelessy copied from 'log' command
void
walk_edges (const app_state & app, const revision_set & rev,
	    set < revision_id > &ancestors, vector < change_set > &changes,
	    set < file_path > &modified_files)
{
  for (edge_map::const_iterator e = rev.edges.begin ();
       e != rev.edges.end (); ++e)
    {
      ancestors.insert (edge_old_revision (e));

      change_set const &cs = edge_changes (e);
      change_set::path_rearrangement const &pr = cs.rearrangement;

      changes.push_back (cs);

      for (change_set::delta_map::const_iterator i = cs.deltas.begin ();
           i != cs.deltas.end (); i++)
      {
        if (pr.added_files.find (i->first ()) == pr.added_files.end ())
          modified_files.insert (i->first ());
      }
    }
}

// ---------------------- formatting functor ----------------------------
// IMPORTANT: to complete formatting, it *must* go out of scope (i.e. its destructor
// called)
FormatFunc::FormatFunc(std::ostream &out, app_state &app)
{
  if (app.xml_enabled)
    fmt = auto_ptr<BaseFormatter>(new XMLFormatter(out, app));
  else 
    fmt = auto_ptr<BaseFormatter>(new PrintFormatter(out, app, app.format_string));
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
out(o)
{
  assign_format_strings(fmt);
}

PrintFormatter::~PrintFormatter()
{
}

// splits the given format string into the revision fmt string and 
// one or more optional changeset fmt strings1
void
PrintFormatter::assign_format_strings(const utf8 &fmt)
{
  // establishing defaults
  fmtstrs[FMTIDX_REVISION]=utf8("");
  fmtstrs[FMTIDX_ANCESTORS]=utf8("%f\n");
  fmtstrs[FMTIDX_DELFILES]=utf8("%f\n");
  fmtstrs[FMTIDX_DELDIRS]=utf8("%f\n");
  fmtstrs[FMTIDX_RENFILES]=utf8("%o -> %f\n");
  fmtstrs[FMTIDX_RENDIRS]=utf8("%o -> %f\n");
  fmtstrs[FMTIDX_ADDFILES]=utf8("%f\n");
  fmtstrs[FMTIDX_MODFILES]=utf8("%f\n");
  
  // quick parse of the formatting string
  string::const_iterator e=fmt().end();
  string::const_iterator i=fmt().begin();
  string::const_iterator start_current_fmt=fmt().begin();
  FMTIDX current_fmt = FMTIDX_REVISION;
  string buf;
  while (i != e)
  {
    switch (*i)
      {
      case '@':
        {
          // seems a start of a changeset format
          // stores the current fmt string (trying to work around ATOMIC limits)
          buf.assign(start_current_fmt, i);
          fmtstrs[current_fmt] = utf8(buf);
          
          ++i;
          N (i!=e, F ("A format string could not end with '@'\n"));

          // prepare for new fmt string
          current_fmt = decode_cset_fmtid(i);
          N (current_fmt != FMTIDX_REVISION, F ("invalid changeset string specifier\n"));

          start_current_fmt=i;
          ++start_current_fmt;
        }
        break;
       
      case '\\':
      case '%':
        // escape or fmt specifier, skipping
        i++;
        break;
      }
    if (i != e)  
      ++i;
  }

  // final string  
  buf.assign(start_current_fmt, i);
  fmtstrs[current_fmt] = utf8(buf);
}


void
PrintFormatter::print_cert (vector < revision < cert > >&certs, const string &name,
                            bool from_start, bool from_end, const string &sep)
{
  for (vector < revision < cert > >::const_iterator i = certs.begin ();
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
PrintFormatter::print_cset_ancestor(const utf8 &fmtstring, const revision_id &rid)
{
  string::const_iterator i = fmtstring ().begin ();
  while (i != fmtstring ().end ())
    {
    if ((*i) == '%')
      {
        ++i;
        if (i == fmtstring ().end ())
          break;
        N (*i == 'f', F ("invalid ancestor format string\n"));
        out << rid.inner ()();
      }
    else if ( (*i) == '\\')
      handle_control(i, fmtstring ().end ());
    else
      out << (*i);
    
    ++i;
    }
}

void
PrintFormatter::print_cset_single(const utf8 &fmtstring, const set<file_path> &data)
{
  for (set<file_path>::const_iterator f = data.begin (); f != data.end (); ++f)
    {
    string::const_iterator i = fmtstring ().begin ();
    while (i != fmtstring ().end ())
      {
      if ((*i) == '%')
        {
          ++i;
          if (i == fmtstring ().end ())
            break;
          N (*i == 'f', F ("invalid file format string\n"));
          out << (*f)();
        }
      else if ( (*i) == '\\')
        handle_control(i, fmtstring ().end ());
      else
        out << (*i);
      
      ++i;
      }
    }        
}

void
PrintFormatter::print_cset_pair(const utf8 &fmtstring, const map<file_path, file_path> &data)
{
  for (map<file_path, file_path>::const_iterator f = data.begin (); f != data.end (); ++f)
    {
    string::const_iterator i = fmtstring ().begin ();
    while (i != fmtstring ().end ())
      {
      if ((*i) == '%')
        {
          ++i;
          if (i == fmtstring ().end ())
            break;
          N (*i == 'o' || *i == 'f', F ("invalid rename format string\n"));
          if (*i == 'o')
            out << f->first();
          else
            out << f->second();
        }
      else if ( (*i) == '\\')
        handle_control(i, fmtstring ().end ());
      else
        out << (*i);
      
      ++i;
      }
            
    }        
}

void
PrintFormatter::handle_cset(const string::const_iterator &fmt_i, const revision_set & rev)
{
    FMTIDX curfmt = decode_cset_fmtid(fmt_i);
    N (curfmt != FMTIDX_REVISION, F ("invalid format specifier"));

    for (edge_map::const_iterator e = rev.edges.begin ();
       e != rev.edges.end (); ++e)
    {
      change_set const &cs = edge_changes (e);
      change_set::path_rearrangement const &pr = cs.rearrangement;

      switch (curfmt)
        {
        case FMTIDX_ANCESTORS:
          print_cset_ancestor(fmtstrs[curfmt], edge_old_revision (e));
          break;
        case FMTIDX_DELFILES:
          print_cset_single(fmtstrs[curfmt], pr.deleted_files);
          break;
        case FMTIDX_DELDIRS:
          print_cset_single(fmtstrs[curfmt], pr.deleted_dirs);
          break;
        case FMTIDX_ADDFILES:
          print_cset_single(fmtstrs[curfmt], pr.added_files);
          break;
        case FMTIDX_MODFILES:
          {
            std::set<file_path> modified_files;
            for (change_set::delta_map::const_iterator i = cs.deltas.begin ();
                 i != cs.deltas.end (); i++)
            {
              if (pr.added_files.find (i->first ()) == pr.added_files.end ())
                modified_files.insert (i->first ());
            }
            print_cset_single(fmtstrs[curfmt], modified_files);
          }
          break;
        case FMTIDX_RENFILES:
          print_cset_pair(fmtstrs[curfmt], pr.renamed_files);
          break;
        case FMTIDX_RENDIRS:
          print_cset_pair(fmtstrs[curfmt], pr.renamed_dirs);
          break;
        
        default:
          break;
      }
    }
    
}

void 
PrintFormatter::handle_control(string::const_iterator &it, const string::const_iterator &end)
{
  ++it;
  if (it == end)
    return;
  switch (*it)
    {
    case '\\':
      out << '\\';
      break;
    case '%':
      out << '%';
      break;
    case '@':
      out << '@';
      break;
    case 'n':
      out<< endl;
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
PrintFormatter::decode_cset_fmtid(const string::const_iterator &i)
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
  if (!app.db.revision_exists (rid))
    {
      L (F ("revision %s does not exist in db\n") % rid);
      return;
    }

  revision_set rev;
  app.db.get_revision (rid, rev);

  vector < revision < cert > >certs;
  app.db.get_revision_certs (rid, certs);
  erase_bogus_certs (certs, app);

  string::const_iterator i = fmtstrs[FMTIDX_REVISION]().begin ();
  string::const_iterator e = fmtstrs[FMTIDX_REVISION]().end();
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
          }
          if (i == e)
            return;

          switch (*i)
            {
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
            case 's':
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
            default:
              N(!short_form, F("no short form for changelog specifier"));
              // unrecognized specifier, perhaps is a changeset one ?
              handle_cset(i, rev);
            }
        }
      else if ( (*i) == '\\')
        handle_control(i, e);
      else
        out << (*i);
      
      ++i;
    }
}


// --------------- XML support -----------------

XMLWriter::XMLWriter (ostream & o):
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
  for (string::const_iterator i = opq().begin(); i != opq().end(); ++i)
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
     out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>" << endl;
     decl_emitted=true;
  }

  if (empty_tag)
    out << ">" << endl;

  out << "<" << tagname;
  open_tags.push_back (tagname);
  empty_tag = true; // right now, the tag is empty
}

void
XMLWriter::end ()
{
  I (open_tags.size () > 0);
  if (empty_tag)
    out << "/>" << endl;
  else  
    out << "</" << open_tags.back () << ">" << endl;
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
     out << ">" << endl;
     empty_tag=false; 
  }
  encode(opq);
}

// ---------------- the xml formatter -----------------------
XMLFormatter::XMLFormatter(ostream &out, app_state &a):
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
  vector < revision < cert > >certs;

  app.db.get_revision_certs (rid, certs);
  erase_bogus_certs (certs, app);
  for (vector < revision < cert > >::const_iterator i = certs.begin ();
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
      for (m=pr.renamed_dirs.begin(); m != pr.renamed_dirs.end() ; ++f)
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
      for (m=pr.renamed_files.begin(); m != pr.renamed_files.end() ; ++f)
      {
         xw.tag("rename_file");
         xw.attr("name", m->second());
         xw.attr("old-name", m->first());
         xml_delta(m->second, cs.deltas);
         xml_delta(m->first, cs.deltas);
         xw.end();
      }
      
      set < file_path > modified_files;
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
