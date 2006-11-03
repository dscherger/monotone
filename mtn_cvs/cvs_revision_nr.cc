// copyright (C) 2005-2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
#include <boost/lexical_cast.hpp>

using cvs_sync::cvs_revision_nr;

bool cvs_revision_nr::operator==(const cvs_revision_nr &b) const
{ return parts==b.parts;
}

// is this strictly correct? feels ok for now (and this is last ressort)
bool cvs_revision_nr::operator<(const cvs_revision_nr &b) const
{ return parts<b.parts;
}

cvs_revision_nr::cvs_revision_nr(const std::string &x)
{ std::string::size_type begin=0;
  do
  { std::string::size_type end=x.find(".",begin);
    std::string::size_type len=end-begin;
    if (end==std::string::npos) len=std::string::npos;
    parts.push_back(atoi(x.substr(begin,len).c_str()));
    begin=end;
    if (begin!=std::string::npos) ++begin;
  } while(begin!=std::string::npos);
};

// we cannot guess whether the revision following 1.3 is 1.3.2.1 or 1.4 :-(
// so we can only hope, that this is the expected result
void cvs_revision_nr::operator++()
{ if (parts.empty()) return;
  if (parts.size()==4 && get_string()=="1.1.1.1") *this=cvs_revision_nr("1.2");
  else parts.back()++;
}

std::string cvs_revision_nr::get_string() const
{ std::string result;
  for (std::vector<int>::const_iterator i=parts.begin();i!=parts.end();++i)
  { if (!result.empty()) result+=".";
    result+=boost::lexical_cast<std::string>(*i);
  }
  return result;
}

bool cvs_revision_nr::is_parent_of(const cvs_revision_nr &child) const
{ unsigned cps=child.parts.size();
  unsigned ps=parts.size();
  if (cps<ps) 
  { if (child==cvs_revision_nr("1.2") && *this==cvs_revision_nr("1.1.1.1"))
      return true;
    return false;
  }
  if (is_branch() || child.is_branch()) return false;
  unsigned diff=0;
  for (;diff<ps;++diff) if (child.parts[diff]!=parts[diff]) break;
  if (cps==ps)
  { if (diff+1!=cps) return false;
    if (parts[diff]+1 != child.parts[diff]) return false;
  }
  else // ps < cps
  { if (diff!=ps) return false;
    if (ps+2!=cps) return false;
    if (child.parts[diff]&1 || !child.parts[diff]) return false;
    if (child.parts[diff+1]!=1) return false;
  }
  return true;
}

// impair number of numbers => branch tag
bool cvs_revision_nr::is_branch() const 
{ return parts.size()&1;
}

cvs_revision_nr cvs_revision_nr::get_branch_root() const
{ I(parts.size()>=4); 
  I(!(parts.size()&1)); // even number of digits
  I(!parts[parts.size()-2]); // but last digit is zero
  I(!(parts[parts.size()-1]&1)); // last digit is even
  cvs_revision_nr result;
  result.parts=std::vector<int>(parts.begin(),parts.end()-2);
  return result;
}

