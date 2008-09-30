#include "base.hh"
#include "branch_name.hh"

using std::string;

branch_name::branch_name()
  : matches_prefix(false)
{ }

branch_name::branch_name(string const & s)
  : data(s), matches_prefix(false)
{ }

string
branch_name::operator()() const
{ return data; }

string::size_type
branch_name::size() const
{ return data.size(); }

bool
branch_name::has_prefix(branch_name const & pre) const
{
  if (pre.data.empty())
    return true;
  if (data.size() == pre.data.size())
    return data == pre.data;
  else if (data.size() > pre.data.size())
    return data.substr(0, pre.data.size()) == pre.data
      && data[pre.data.size()] == '.';
  else
    return false;
}

bool
branch_name::strip_prefix(branch_name const & pre)
{
  if (pre.data.empty())
    return true;
  if (!has_prefix(pre))
    return false;

  if (data.size() == pre.data.size())
    data.clear();
  else
    data.erase(0, pre.data.size() + 1);
  return true;
}

void
branch_name::prepend(branch_name const & pre)
{
  if (pre.data.empty())
    return;
  if (data.empty())
    data = pre.data;
  else
    data = pre.data + "." + data;
}

void
branch_name::append(branch_name const & post)
{
  if (post.data.empty())
    return;
  if (!data.empty())
    data += ".";
  data += post.data;
}

bool
operator < (branch_name const & lhs,
            branch_name const & rhs)
{
  return lhs != rhs && lhs.data < rhs.data;
}
bool
operator == (branch_name const & lhs,
             branch_name const & rhs)
{
  if (lhs.matches_prefix && lhs.has_prefix(rhs))
    return true;
  if (rhs.matches_prefix && rhs.has_prefix(lhs))
    return true;
  return rhs.data == lhs.data;
}

std::ostream &
operator << (std::ostream & s, branch_name const & b)
{
  return s << b();
}

template <> void dump(branch_name const & obj, string & out)
{
  out = obj();
}
