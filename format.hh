#ifndef __FORMAT_HH__
#define __FORMAT_HH__

// copyright (C) 2005 R.Ghetta <birrachiara@tin.it>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"

#include <ostream>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>

// base class of all formatters
class BaseFormatter 
{
public:
  BaseFormatter(app_state &app); 
  virtual ~BaseFormatter(); 

  virtual void apply(const revision_id &rid) = 0;

protected:
  app_state &app;
};

// functor formatter
// IMPORTANT: to complete formatting, it *must* go out of scope (i.e. its destructor
// called)
class FormatFunc : public std::unary_function<revision_id, void>
{
public:
  FormatFunc(std::ostream &out, app_state &app);
  ~FormatFunc();
  void operator ()(const revision_id &rid) { fmt->apply(rid); }
private:
  std::auto_ptr<BaseFormatter> fmt;   
};


class PrintFormatter : public BaseFormatter 
{
public:
  PrintFormatter(std::ostream & out, app_state &app, const utf8 &fmtstring);
  ~PrintFormatter();

  // applies the fmt string to the given revision
  void apply(const revision_id &rid);

private:
  // changeset fmt strs indexing enum
  enum FMTIDX
    {
      FMTIDX_REVISION,
      FMTIDX_ANCESTORS,
      FMTIDX_DELFILES,
      FMTIDX_DELDIRS,
      FMTIDX_RENFILES,
      FMTIDX_RENDIRS,
      FMTIDX_ADDFILES,
      FMTIDX_MODFILES
    };
  
  void print_cert (std::vector < revision < cert > >&certs, const std::string &name,
                   bool from_start=false, bool from_end=false, const std::string &sep="");
  void print_changeset(const revision_set & rev);

  std::string::const_iterator find_cset_fmt_end(std::string::const_iterator i, 
                                       const std::string::const_iterator &e);
  void print_cset_ancestors(const std::string::const_iterator &startfmt, 
                           const std::string::const_iterator & e,
                           const std::set<revision_id> &data);
  void print_cset_single(const std::string::const_iterator &startfmt, 
                         const std::string::const_iterator & e,
                         const std::set<file_path> &data);
  void print_cset_pair(const std::string::const_iterator &startfmt, 
                       const std::string::const_iterator & e,
                       const std::map<file_path, file_path> &data);
  FMTIDX decode_cset_fmtid(const std::string::const_iterator &i);
  std::string::const_iterator handle_cset(const std::string::const_iterator &it, 
                                         const std::string::const_iterator & fmt_e,
                                         const changes_summary &csum);

  void handle_control(std::string::const_iterator &it, const std::string::const_iterator &end);

private:
  std::ostream & out; 
  utf8 fmtstr;  
  std::string::const_iterator startpoint;
};


// a very rudimentary XML writer
class XMLWriter
{
public:
  explicit XMLWriter (std::ostream & );
  ~XMLWriter ();      

  void tag(const utf8 &tagname);
  void tag(const std::string &tagname) { tag(utf8(tagname)); }
  void cdata(const utf8 &opq);
  void end();
  void attr(const utf8 &attrname, const utf8 &value);
  void attr(const std::string &attrname, const utf8 &value){ attr(utf8(attrname), value);}
  void attr(const utf8 &attrname, const file_path &value)
    { 
      attr(utf8(attrname), boost::lexical_cast<std::string>(value));
    }
  void attr(const std::string &attrname, const file_path &value)
    { 
      attr(utf8(attrname), boost::lexical_cast<std::string>(value));
    }

private:  
  void encode(const utf8 & opq);

private:
  std::ostream &out;
  std::vector<utf8> open_tags;
  bool decl_emitted;
  bool empty_tag;
};

class XMLFormatter: public BaseFormatter 
{
public:
  XMLFormatter(std::ostream &out, app_state &app);
  ~XMLFormatter();

  // applies the formatting to the given revision
  void apply(const revision_id &rid);

private:
  void xml_revision_id(const revision_id & rid);
  void xml_manifest(const manifest_id & mid);
  void xml_file_id(const file_id & fid);
  void xml_certs (const revision_id & rid);
  void xml_ancestors(const revision_set & rev);
  void xml_delta(const file_path& f, const change_set::delta_map &dm);
  void xml_changeset(const revision_set & rev);

private:
  XMLWriter xw;
};

#endif  // header guard
