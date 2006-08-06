// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"
#include "xdelta.hh"

namespace
{
  template <typename Key, typename Value> struct LRUCache_placeholder
  {
    bool exists(Key const & key) const;
    void touch(Key const & key);
    Value const & fetch(Key const & key); // I()'s out if !exists(key)
    void insert(Key const & key, Value const & value);
    // _not_ called by insert():
    void prune(size_t minimum_size);
  }

  struct text_t
  {
    file_id const me;
    file_id const base; // can be null
    data const dat;
    static shared_ptr<text_t> create(file_id const & me, file_id const & base,
                                     data const & dat);
    ~text_t();
    size_t memory_size();
  private:
    text_t(file_id const & me, file_id const & base, data const & dat)
      : me(me), base(base), dat(dat)
    {}
  };

  struct extent_t
  {
    size_t const offset, const length;
    shared_ptr<text_t> text;
  };

  struct script_t
  {
    std::vector<extent_t> const extents;
    size_t const precursors_memory_size;

    void clear() { extents.clear() }

    size_t memory_size()
    {
      return (extents.capacity() * sizeof(extent_t)) + sizeof(script_t);
    }
    size_t total_memory_size()
    {
      return precursors_memory_size + memory_size();
    }

    static shared_ptr<size_t> create(std::vector<extent_t> const & extents,
                                     size_t precursors_memory_size);
    ~script_t();
  private:
    script_t(std::vector<extent_t> const & extents,
             size_t precursors_memory_size)
      // attempt to convince our vector to allocate exactly the right amount
      // of space, since it will never grow (FIXME is this correct, and/or
      // useful?)
      : extents(extents.begin(), extents.end()),
        precursors_memory_size(precursors_memory_size)
    {}
  };

  // for now, we support only a single global cache
  size_t total_cached_size = 0;
  std::map<file_id, weak_ptr<text_t> > cached_texts;
  LRUCache_placeholder<file_id, shared_ptr<script_t> > script_cache;

  // Maintain the global size and text cache

  size_t
  text_t::memory_size()
  {
    return this->dat().size()
      + this->me.inner()().size()
      + this->base.inner()().size()
      + sizeof(text_t);
  }

  shared_ptr<text_t>
  text_t::create(file_id const & me, file_id const & base, data const & dat)
  {
    shared_ptr<text_t> t = shared_ptr<text_t>(new text_t(me, base, dat));

    total_cached_size += t->memory_size();
    safe_insert(cached_texts, std::make_pair(me, weak_ptr<text_t>(t)));

    return t;
  }

  text_t::~text_t()
  {
    total_cached_size -= this->memory_size();
    safe_remove(cached_texts, me);
  }

  shared_ptr<script_t>
  script_t::create(file_id const & fid,
                   std::vector<extent_t> const & extents,
                   size_t precursors_memory_size)
  {
    shared_ptr<script_t> s = shared_ptr<script_t>(new script_t(extents,
                                                               precursors_memory_size));
    total_cached_size += s->memory_size();
    script_cache.insert(fid, s);

    return s;
  }

  script_t::~script_t()
  {
    total_cached_size -= this->memory_size();
  }


  void
  reconstruct_into_string(script_t const & script, std::string & s)
  {
    size_t length = 0;
    for (std::vector<extent_t>::const_iterator i = script.extents.begin();
         i != script.extents.end(); ++i)
      length += i->length;
    s.clear();
    s.reserve(length);
    for (std::vector<extent_t>::const_iterator i = script.extents.begin();
         i != script.extents.end(); ++i)
      s.append(i->text.begin() + i->offset,
               i->text.begin() + i->offset + i->length);
  }

  // Cache size management strategy: our goal is that whenever we load in some
  // chain ending in version V, we end up with all the texts needed to
  // reconstruct V and also all the scripts used to construct V in the cache.
  // This requires some careful fiddling.
  //   -- first we load all the texts, which raises the cache fill line
  //      (possibly above its high-water mark).
  //   -- then we load in the scripts, one by one, which again raises the
  //      cache fill line (possibly above its high-water mark).
  //   -- however, we do not want to push anything out of the cache during
  //      this process, because we might be pushing out the very stuff we are
  //      looking for!  (Especially during the initial search for a
  //      reconstruction path.)
  //   -- only after loading everything do we want to adjust the cache high
  //      water mark (to be above all the stuff we just loaded in, or perhaps
  //      tweaking it downwards if the stuff we just loaded was not so large),
  //      and prune back to below this mark.

  shared_ptr<text_t>
  swap_in_text(file_id const & fid, database & db)
  {
    std::map<file_id, weak_ptr<text_t> >::const_iterator i;
    i = cached_texts.find(fid);
    if (i != cached_texts.end())
      {
        // already loaded
        shared_ptr<text_t> text = i->second.lock();
        I(text);
        return text;
      }
    else
      {
        // FIXME TODO
        I(false);
      }
  }

  shared_ptr<script_t>
  swap_in_script(file_id const & fid, database & db)
  {
    I(!null_id(fid));
    // FIXME: currently requires that there be unique paths through db,
    // consider whether to fix this code or make this assumption be true
    typedef std::vector<shared_ptr<text_t> > texts_t;
    texts_t texts;
    {
      std::set<file_id> seen;
      file_id curr = fid;
      while (!(null_id(curr) || script_cache.exists(curr)))
        {
          safe_insert(seen, curr);
          shared_ptr<text_t> text = swap_in_text(curr, db);
          curr = text->base;
          texts.append(text);
        }
    }
    // now texts has our final delta at index 0, and the first delta that can
    // be constructed without further disk operations at the end
    for (texts_t::reverse_iterator i = texts.rbegin(); i != texts.rend(); ++i)
      {
        // i->base is already loaded into the cache
        // we need to build a script for *i, and put this script into the
        // cache
        parse_script(*i);
      }

    shared_ptr<script_t> script = script_cache.fetch(fid);
    script_cache.prune(script->total_memory_size());
    return script;
  }

  // precondition: null_id(text->base) || script_cache.exists(text->base)
  shared_ptr<script_t>
  parse_script(shared_ptr<text_t> const & text)
  {
    std::vector<extent_t> extents;
    size_t precursors_memory_size = text->memory_size();
    if (null_id(text->base))
      {
        // this is a raw file
        // construct a script with exactly the right amount of memory
        // allocated for it
        extent_t extent;
        extent.offset = 0;
        extent.length = text->dat().size();
        extent.text = text;
        extents.push_back(extent);
      }
    else
      {
        // this is a delta, which we get to parse
        shared_ptr<script_t> base_script = script_cache.fetch(text->base);
        
        // FIXME
        I(false);
        
        precursors_memory_size += base_script->total_memory_size();
      }
    return script_t::create(text->me, extents, precursors_memory_size);
  }


  // TODO:
  //   -- a cheap, public, write-to-db function
  //   -- a public fetch-from-db function
  //   -- implement the lru
  //   -- implement the database interface
