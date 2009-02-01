#ifndef __BRANCH_NAME_HH__
#define __BRANCH_NAME_HH__

#include "origin_type.hh"

class branch_name;

bool operator < (branch_name const & lhs,
                 branch_name const & rhs);

bool operator == (branch_name const & lhs,
                  branch_name const & rhs);

inline bool operator > (branch_name const & lhs,
                        branch_name const & rhs)
{ return rhs < lhs; }

inline bool operator != (branch_name const & lhs,
                         branch_name const & rhs)
{ return !(lhs == rhs); }


class branch_name : public origin_aware
{
  friend bool operator < (branch_name const & lhs,
                          branch_name const & rhs);

  friend bool operator == (branch_name const & lhs,
                           branch_name const & rhs);

  std::string data;
public:
  bool matches_prefix;

  branch_name();
  branch_name(std::string const & s, origin::type made_from);
  std::string operator()() const;

  std::string::size_type size() const;

  // note that a branch name is a prefix of itself
  bool has_prefix(branch_name const & pre) const;
  bool strip_prefix(branch_name const & pre);

  void prepend(branch_name const & pre);
  void append(branch_name const & post);
};

std::ostream & operator << (std::ostream & s, branch_name const & b);

template<> void dump(branch_name const & obj, std::string & out);

#endif
