// Copyright 2010 Timothy Brownawell <tbrownaw@prjek.net>
// You may use, copy, modify, distribute, etc, this file with no other
// restriction than that this notice not be removed.

#include <boost/shared_ptr.hpp>
#include <vector>

// This is *NOT* a normal STL-compatible container! It pretends well enough
// to work with parallel_iter and such, but it does not have find(),
// insert(), erase().
//
// Because this is copy-on-write, and the copying is per-node instead of for
// the whole object, the nodes can not have parent pointers (also, having
// parent pointers would I think make the size 2^n+4 instead of 2^n, which
// AIUI would waste almost equal space with common memory allocators).
// This lack of parent pointers means that iterators are expensive, so they're
// not used except for, well, iteration.

template<typename _Key, typename _Value, int _Bits>
class cow_trie
{
public:
  typedef _Key key_type;
  //typedef _Value value_type;
  typedef std::pair<_Key, _Value> value_type;
private:
  enum { mask = (1<<_Bits)-1 };
  enum { levels = (sizeof(_Key) * 8 + _Bits - 1) / _Bits };

  struct middle_node_type
  {
    boost::shared_ptr<void> contents[(1<<_Bits)];
  };
  struct leaf_node_type
  {
    _Value contents[1<<_Bits];
  };

  _Value _empty_value;
  unsigned _count;
  boost::shared_ptr<void> _data;

  bool walk(boost::shared_ptr<void> & d, _Key key, int level, _Value **ret)
  {
    if (!d)
      {
	if (level > 0)
	  d.reset(new middle_node_type());
	else
	  d.reset(new leaf_node_type());
      }
    if (!d.unique())
      {
	if (level > 0)
	  d.reset(new middle_node_type(*boost::static_pointer_cast<middle_node_type>(d)));
	else
	  d.reset(new leaf_node_type(*boost::static_pointer_cast<leaf_node_type>(d)));
      }
    unsigned idx = (key >> (_Bits * level)) & mask;
    if (level > 0)
      return walk(boost::static_pointer_cast<middle_node_type>(d)->contents[idx],
		  key, level-1, ret);
    else
      {
	*ret = &boost::static_pointer_cast<leaf_node_type>(d)->contents[idx];
	return true;
      }
  }

  bool walk(boost::shared_ptr<void> const & d, _Key key, int level, _Value **ret) const
  {
    if (!d)
      {
	return false;
      }
    unsigned idx = (key >> (_Bits * level)) & mask;
    if (level > 0)
      return walk(boost::static_pointer_cast<middle_node_type>(d)->contents[idx],
		  key, level-1, ret);
    else
      {
	*ret = &boost::static_pointer_cast<leaf_node_type>(d)->contents[idx];
	return true;
      }
  }
public:
  cow_trie() : _count(0) { }
  unsigned size() const { return _count; }
  bool empty() const { return _count == 0; }
  void clear()
  {
    _count = 0;
    _data.reset();
  }
  _Value const & set(_Key key, _Value const & value) {
    _Value *p;
    walk(_data, key, levels-1, &p);
    bool b = (*p != _empty_value);
    bool a = (value != _empty_value);
    if (b && !a)
      --_count;
    else if (a && !b)
      ++_count;
    *p = value;
    return *p;
  }
  bool set_if_missing(_Key key, _Value const & value)
  {
    _Value *p;
    walk(_data, key, levels-1, &p);
    if (*p != _empty_value)
      return false;
    if (value != _empty_value)
      {
	++_count;
	*p = value;
      }
    return true;
  }
  void unset(_Key key) {
    set(key, _empty_value);
  }
  _Value const &get_if_present(_Key key) const {
    _Value *p;
    if (walk(_data, key, levels-1, &p))
      return *p;
    else
      return _empty_value;
  }
  // This is actually not the same as above.
  // It's non-const, so it calls the other walk().
  _Value const &get_unshared_if_present(_Key key)
  {
    _Value *p;
    if (walk(_data, key, levels-1, &p))
      return *p;
    else
      return _empty_value;
  }

  class const_iterator
  {
    struct stack_item
    {
      boost::shared_ptr<void> ptr;
      unsigned idx;
      bool operator==(stack_item const & other) const
      {
	return ptr == other.ptr && idx == other.idx;
      }
    };
    std::vector<stack_item> stack;
    friend class cow_trie;
    explicit const_iterator(cow_trie const & t)
    {
      if (t._data)
	{
	  stack_item item;
	  item.ptr = t._data;
	  item.idx = (unsigned)-1;
	  stack.push_back(item);
	  ++(*this);
	}
    }
    _Value _empty_value;
  private:
    value_type _ret;
  public:
    bool operator==(const_iterator const & other)
    {
      return stack == other.stack;
    }
    bool operator!=(const_iterator const & other)
    {
      return stack != other.stack;
    }
    const_iterator() { }
    const_iterator const & operator++()
    {
      while (!stack.empty())
	{
	  stack_item & item = stack.back();
	  boost::shared_ptr<middle_node_type> middle
	    = boost::static_pointer_cast<middle_node_type>(item.ptr);
	  boost::shared_ptr<leaf_node_type> leaf
	    = boost::static_pointer_cast<leaf_node_type>(item.ptr);
	  for (++item.idx; item.idx < (1<<_Bits); ++item.idx)
	    {
	      if (stack.size() == levels)
		{
		  if (leaf->contents[item.idx] != _empty_value)
		    {
		      _ret.first = (_ret.first & ~mask) | item.idx;
		      _ret.second = leaf->contents[item.idx];
		      return *this;
		    }
		}
	      else
		{
		  if (middle->contents[item.idx])
		    {
		      int shifts = levels - stack.size();
		      int bits = shifts * _Bits;
		      _ret.first = (_ret.first & ~(mask<<bits)) | (item.idx<<bits);
		      stack_item i;
		      i.ptr = middle->contents[item.idx];
		      i.idx = (unsigned)-1;
		      stack.push_back(i);
		      break;
		    }
		}
	    }
	  if (item.idx == (1 << _Bits))
	    stack.pop_back();
	}
      return *this;
    }
    value_type const & operator*() const
    {
      return _ret;
    }
    value_type const * operator->() const
    {
      return &_ret;
    }
  };
  friend class const_iterator;

  const_iterator begin() const { return const_iterator(*this); }
  const_iterator end() const { return const_iterator(); }
};


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
