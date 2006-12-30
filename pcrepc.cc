#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstring>  // for strerror
#include <cerrno>   // for errno

#include "pcre.h"

using std::cerr;
using std::string;
using std::ifstream;
using std::ofstream;
using std::strerror;

static char const * progname;

// Serializing a pcre_extra structure (along with the pcre_study_data block
// it points to) is tricksome.  We don't want to do _any_ work at load time;
// in particular we do not want to have to call malloc or operator new.  We
// also don't want to make the users of this mechanism include pcre.h, as it
// is a mess namespace-wise.  The compiler will not love us if we declare
// the same struct twice.  pcre_study does not bother to clear out fields of
// the pcre_extra block that it doesn't use, and we don't want to write out
// garbage.  And finally, the _types_ of the fields of struct pcre_extra are
// part of the documented ABI, but their order is not, nor is it documented
// whether there may be any additional fields in between them.
//
// Thus the following function, which computes three strings that can be
// written to the output file verbatim.  One contains a declaration for a
// structure named pcre::extra_data, which is layout-compatible with struct
// pcre_extra plus a pcre_study_data block tacked on its end, properly
// guarded by #ifdefs so that the compiler will only see it once even if we
// blat it into more than one header read by the same file.  The other two
// contain two of the four components of a partial initialization for that
// structure, such that
//
//   stream << "pcre::extra_data const " << name
//          << pi1 << name << pi2 << studyblock << "}};"
//
// writes to STREAM a correctly declared and initialized pcre::extra_data
// object.  We assume that a char array receives no extra alignment.

static void
prepare_fake_pcre_extra(string & declaration,
                        string & partial_initialization_1,
                        string & partial_initialization_2)
{
  std::ostringstream dcl, pi1, pi2;
  std::ostringstream * pi = &pi1;

  size_t flagoff = offsetof(struct pcre_extra, flags);
  size_t studyoff = offsetof(struct pcre_extra, study_data);

  size_t pad1 = std::min(flagoff, studyoff);
  size_t pad2 = (flagoff < studyoff
                 ? studyoff - (flagoff + sizeof(unsigned long))
                 : flagoff - (studyoff + sizeof(void *)));
  size_t pad3 = (sizeof(struct pcre_extra)
                 - (flagoff < studyoff
                    ? studyoff + sizeof(void *)
                    : flagoff + sizeof(unsigned long)));

  dcl << ("#ifndef _PCREWRAP_EXTRA_DATA\n"
          "#define _PCREWRAP_EXTRA_DATA\n"
          "namespace pcre\n"
          "{\n"
          "  struct extra_data\n"
          "  {\n");

  *pi << " = {\n";
  
  if (pad1 > 0)
    {
      dcl << "    char pad1[" << pad1 << "];\n";
      *pi << "  {0},";
    }

  if (flagoff < studyoff)
    {
      dcl << "    unsigned long int flags;\n";
      *pi << "  " << PCRE_EXTRA_STUDY_DATA << ",\n";
    }
  else
    {
      dcl << "    void const * study_data;\n";
      *pi << "  (void const *)(((char const *)&";
      pi = &pi2;
      *pi << ") + " << sizeof(struct pcre_extra) << "),\n";
    }
        
  if (pad2 > 0)
    {
      dcl << "    char pad2[" << pad2 << "];\n";
      *pi << "  {0},\n";
    }

  if (flagoff < studyoff)
    {
      dcl << "    void const * study_data;\n";
      *pi << "  (void const *)(((char const *)&";
      pi = &pi2;
      *pi << ") + " << sizeof(struct pcre_extra) << "),\n";
    }
  else
    {
      dcl << "    unsigned long int flags;\n";
      *pi << "  " << PCRE_EXTRA_STUDY_DATA << ",\n";
    }

  if (pad3 > 0)
    {
      dcl << "    char pad3[" << pad3 << "];\n";
      *pi << "  {0},\n";
    }

  dcl << ("    unsigned char study_bytes[];\n"
          "  }\n"
          "}\n"
          "#endif // _PCREWRAP_EXTRA_DATA\n");
  *pi << "  {\n\t";

  declaration = dcl.str();
  partial_initialization_1 = pi1.str();
  partial_initialization_2 = pi2.str();
}

static void
write_prebuilt_regex(std::ostream & f,
		     string const & name,
		     string const & pat,
                     bool & this_file_has_extra_dcl)
{
  static string extra_dcl;
  static string extra_pi1;
  static string extra_pi2;
  static bool extra_known = false;
  
  char const * error;
  int erroffset;
  pcre const * pc = pcre_compile(pat.c_str(), 0, &error, &erroffset, 0);
  if (error)
    {
      cerr << "compiling regex '" << name << "':\n"
	   << error << ", at position " << erroffset << "in string\n";
      return;
    }
  pcre_extra const * pcx = pcre_study(pc, 0, &error);
  if (error)
    {
      cerr << "studying: " << error << "\n";
      return;
    }

  size_t pc_len;
  size_t study_len = 0; 
  int rc = pcre_fullinfo(pc, pcx, PCRE_INFO_SIZE, &pc_len);
  if (rc)
    {
      cerr << progname << ": " << name
	   << ": PCRE_INFO_SIZE failure, code " << rc << "\n";
      return;
    }

  // The public header doesn't say what's inside a pcre, so we can't just
  // ask for its alignment.  However, we know via code inspection that it
  // contains one char pointer (which, in our use, will always be null)
  // several integers of equal or smaller size to a pointer, and a bunch of
  // characters.  Thus it is a relatively safe assumption that the required
  // alignment is no greater than that of a char *.
  f << "\nunsigned char const __attribute__ ((aligned ("
    << __alignof__(char *) << "))) "
    << name << "_data[] = {\n\t";

  unsigned char const * pcdata = reinterpret_cast<unsigned char const *>(pc);
  for (size_t i = 0; i < pc_len; i++)
    {
      f << static_cast<int>(pcdata[i]) << ", ";
      if (i && i % 14 == 0)
	f << "\n\t";
    }
  f << "\n};\n";

  if (pcx)
    {
      if (!extra_known)
        {
          prepare_fake_pcre_extra(extra_dcl, extra_pi1, extra_pi2);
          extra_known = true;
        }

      rc = pcre_fullinfo(pc, pcx, PCRE_INFO_STUDYSIZE, &study_len);
      if (rc)
	{
	  cerr << progname << ": " << name
	       << ": PCRE_INFO_STUDYSIZE failure, code " << rc << "\n";
	  return;
	}

      if (!this_file_has_extra_dcl)
        {
          f << extra_dcl;
          this_file_has_extra_dcl = true;
        }

      f << "pcre::extra_data const " << name << "_extra_data"
        << extra_pi1 << name << "_extra_data"
        << extra_pi2;

      unsigned char const * sdata
	= reinterpret_cast<unsigned char const *>(pcx->study_data);
      for (size_t i = 0; i < study_len; i++)
	{
	  f << static_cast<int>(sdata[i]) << ", ";
	  if (i && i % 14 == 0)
	    f << "\n\t";
	}
      f << "\n  }\n};\npcre::precompiled_regex const "
	<< name << "(\n\tstatic_cast<void const *>("
        << name << "_data),\n\tstatic_cast<void const *>(&"
        << name << "_extra_data)\n);\n";
    }
  else // no extra data
    f << "pcre::precompiled_regex const "
      << name << "(\n\tstatic_cast<void const *>("
      << name << "_data), 0\n);\n";

  if (pc)
    pcre_free(const_cast<pcre *>(pc));
  if (pcx)
    pcre_free(const_cast<pcre_extra *>(pcx));
}
		     
static void
move_if_change(string const & newname, string const & oldname)
{
  using std::remove;
  using std::rename;
  // The inner block ensures that fnew and fold are both closed by the
  // time we try to delete or rename them (this is necessary because
  // Windows filesystem semantics are wrong).
  {
    ifstream fold(oldname.c_str());
    ifstream fnew(newname.c_str());

    if (!fnew)
      {
	cerr << progname << ": cannot open " << newname << ": "
	     << strerror(errno) << '\n';
	return;
      }
    if (!fold)
      {
	if (errno == ENOENT)
	  goto different;
	cerr << progname << ": cannot open " << oldname << ": "
	     << strerror(errno) << '\n';
	return;
      }

    while (!fnew.eof() && !fold.eof())
      {
	char cnew, cold;
	fnew.get(cnew);
	fold.get(cold);
	if (cnew != cold)
	  goto different;
      }
    if (fnew.eof() != fold.eof())
      goto different;
  }

  // if we get here, they are the same, so we just delete new.
  cerr << progname << ": " << oldname << " is unchanged\n";
  remove(newname.c_str());
  return;

 different:
  remove(oldname.c_str());
  rename(newname.c_str(), oldname.c_str());
}

static void
ensure_only_trailing_comment(string::const_iterator p, string const & fname,
                             size_t lineno)
{
  while (std::isspace(*p))
    p++;
  if ((p[0] == '/' && p[1] == '/')
      || p[0] == '\0')
    return;
  cerr << fname << ':' << lineno
       << ": unexpected text at end of line:" << *p << "\n";
}

static void
scan_convert_strconst(string::const_iterator & p, string & result,
                      string const & fname, size_t lineno)
{
  p++;  // caller left it pointing at the open quote
  while (*p != '"')
    {
      if (*p != '\\')
        {
          result += *p;
          p++;
        }
      else
        {
          switch(p[1])
            {
            case '"': case '\'': case '\\':
              result += p[1];
              break;

            case 'a': result += '\a'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'v': result += '\v'; break;

            default:
              cerr << fname << ':' << lineno
                   << "sorry, escape sequence \\" << p[1] << "not supported\n";
            }
          p += 2;
        }
    }
  p++;  // skip close quote
}

static void
process_file(char const * fname)
{
  using std::stringbuf;
  using std::ios;
  using std::isspace;
  using std::isalnum;
  
  ifstream f(fname);

  if (!f)
    {
      cerr << progname << ": cannot open " << fname
	   << ": " << strerror(errno) << '\n';
      return;
    }
  enum { want_ifdef, want_include, want_else, want_regex_dcls, want_regex_strs }
    state = want_ifdef;
  string includename;
  ofstream out;
  size_t lineno = 0;
  string rx;
  string rxname;
  bool has_extra_dcl = false;

  do
    {
      stringbuf sb;
      char newline;
      f.get(sb);
      // an empty line is not an error
      f.clear(f.rdstate() & ~ios::failbit);
      f.get(newline);
      string s(sb.str());

      lineno++;

      if (s == "")
        continue;
      
      switch (state)
	{
	case want_ifdef:
	  if (s == "#ifdef PCRE_PRECOMPILED")
	    state = want_include;
	  break;

	case want_include:
          if (s.compare(0, sizeof "#include \""-1, "#include \"") == 0
              && s[s.size() - 1] == '"')
	    {
	      includename = s.substr(sizeof "#include \"" - 1,
                                     s.size() - sizeof("#include \""));
	      out.open((includename + "T").c_str());
              state = want_else;
	    }
          else
            cerr << fname << ':' << lineno
                 << ": unexpected text while looking for '#include FILE'\n";
	  break;

        case want_else:
	  if (s == "#else")
	    state = want_regex_dcls;
	  else 
            cerr << fname << ':' << lineno
                 << ": unexpected text while looking for '#else'\n";
	  break;

        case want_regex_dcls:
	  if (s == "#endif")
	    {
	      out.close();
	      move_if_change(includename + "T", includename);
	      state = want_ifdef;
	    }
	  else if (s.compare(0, sizeof "static pcre::regex" - 1,
                             "static pcre::regex") == 0)
	    {
              // The next thing will be an identifier, and then an open
              // parenthesis.  A string may or may not follow.
              string::const_iterator idstart
                = s.begin() + sizeof "static pcre::regex" - 1;
              while (isspace(*idstart))
                idstart++;
              string::const_iterator idend = idstart;
              while (isalnum(*idend) || *idend == '_')
                idend++;
              if (idstart == idend)
                {
                  cerr << fname << ':' << lineno
                       << ": no identifier after 'static pcre::regex'\n";
                  break;
                }
              rxname = string(idstart, idend);

              while (isspace(*idend))
                idend++;
              if (*idend == '(')
                idend++;
              else
                {
                  cerr << fname << ':' << lineno
                       << ": no '(' found after regex name\n";
                  break;
                }
              state = want_regex_strs;
              rx.clear();

              while (isspace(*idend))
                idend++;
              if (*idend == '"')
                {
                  string::const_iterator argh = s.end();
                  s = string(idend, argh);
                  goto first_string;
                }
              else
                ensure_only_trailing_comment(idend, fname, lineno);
	    }
          else
            cerr << fname << ':' << lineno
                 << (": unexpected text while looking for "
                     "'static pcre::regex'\n");

          break;

        case want_regex_strs:
        first_string:
          {
            string::const_iterator p = s.begin();
            while (isspace(*p))
              p++;
            if (*p == '"')
              scan_convert_strconst(p, rx, fname, lineno);
            while (isspace(*p))
              p++;
            if (*p == ')')
              {
                p++;
                while (isspace(*p))
                  p++;
                if (*p != ';')
                  cerr << fname << ':' << lineno
                       << ": no semicolon after close parenthesis\n";
                else
                  p++;
                state = want_regex_dcls;
                write_prebuilt_regex(out, rxname, rx, has_extra_dcl);
              }
            ensure_only_trailing_comment(p, fname, lineno);
          }
          break;
	}
    }
  while (!f.eof());
  if (state != want_ifdef)
      cerr << progname << ": " << fname << ": unexpected end of file\n";
}

int
main(int argc, char ** argv)
{
  progname = argv[0];

  if (argc <= 1)
    {
      cerr << "usage: " << progname << " files-to-scan..." << "\n";
      return 2;
    }

  for (int i = 1; i < argc; i++)
    process_file(argv[i]);
  return 0;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
