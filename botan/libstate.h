/*************************************************
* Library Internal/Global State Header File      *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_LIB_STATE_H__
#define BOTAN_LIB_STATE_H__

#include <botan/base.h>
#include <string>
#include <vector>
#include <map>

namespace Botan {

/*************************************************
* Global State Container Base                    *
*************************************************/
class Library_State
   {
   public:
      class Engine_Iterator
         {
         public:
            class Engine* next();
            Engine_Iterator(const Library_State& l) : lib(l) { n = 0; }
         private:
            const Library_State& lib;
            u32bit n;
         };
      friend class Engine_Iterator;

      Allocator* get_allocator(const std::string& = "") const;
      void add_allocator(const std::string&, Allocator*);

      void set_prng(RandomNumberGenerator*);
      void randomize(byte[], u32bit);
      void add_entropy_source(EntropySource*, bool = false);
      void add_entropy(const byte[], u32bit);
      void add_entropy(EntropySource&, bool);
      u32bit seed_prng(bool, u32bit);

      u64bit system_clock() const;

      void set_option(const std::string&, const std::string&,
                              const std::string&, bool = true);
      std::string get_option(const std::string&, const std::string&) const;
      bool option_set(const std::string&, const std::string&) const;

      void add_engine(class Engine*);

      class Mutex* get_mutex();

      Library_State(class Mutex_Factory*, class Timer*);
      ~Library_State();
   private:
      Library_State(const Library_State&) {}
      Library_State& operator=(const Library_State&) { return (*this); }

      class Engine* get_engine_n(u32bit) const;
      void set_default_policy();

      std::map<std::string, class Mutex*> locks;

      class Mutex_Factory* mutex_factory;
      class Timer* timer;

      std::map<std::string, std::string> settings;
      std::map<std::string, Allocator*> alloc_factory;
      mutable Allocator* cached_default_allocator;

      RandomNumberGenerator* rng;
      std::vector<EntropySource*> entropy_sources;
      std::vector<class Engine*> engines;
   };

/*************************************************
* Global State                                   *
*************************************************/
Library_State& global_state();
void set_global_state(Library_State*);

}

#endif
