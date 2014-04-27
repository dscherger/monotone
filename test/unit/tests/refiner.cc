// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/refiner.hh"
#include "../randomizer.hh"

#include <deque>
#include <boost/shared_ptr.hpp>

using std::deque;
using std::set;
using std::string;
using boost::shared_ptr;

struct
refiner_pair
{
  // This structure acts as a mock netsync session. It's only purpose is to
  // construct two refiners that are connected to one another, and route
  // refinement calls back and forth between them.

  struct
  refiner_pair_callbacks : refiner_callbacks
  {
    refiner_pair & p;
    bool is_client;
    refiner_pair_callbacks(refiner_pair & p, bool is_client)
      : p(p), is_client(is_client)
    {}

    virtual void queue_refine_cmd(refinement_type ty,
                                  merkle_node const & our_node)
    {
      p.events.push_back(shared_ptr<msg>(new msg(is_client, ty, our_node)));
    }

    virtual void queue_done_cmd(netcmd_item_type ty,
                                size_t n_items)
    {
      p.events.push_back(shared_ptr<msg>(new msg(is_client, n_items)));
    }
    virtual ~refiner_pair_callbacks() {}
  };

  refiner_pair_callbacks client_cb;
  refiner_pair_callbacks server_cb;
  refiner client;
  refiner server;

  struct msg
  {
    msg(bool is_client, refinement_type ty, merkle_node const & node)
      : op(refine),
        ty(ty),
        send_to_client(!is_client),
        node(node)
    {}

    msg(bool is_client, size_t items)
      : op(done),
        send_to_client(!is_client),
        n_items(items)
    {}

    enum { refine, done } op;
    refinement_type ty;
    bool send_to_client;
    size_t n_items;
    merkle_node node;
  };

  deque<shared_ptr<msg> > events;
  size_t n_msgs;

  void crank()
  {

    shared_ptr<msg> m = events.front();
    events.pop_front();
    ++n_msgs;

    switch (m->op)
      {

      case msg::refine:
        if (m->send_to_client)
          client.process_refinement_command(m->ty, m->node);
        else
          server.process_refinement_command(m->ty, m->node);
        break;

      case msg::done:
        if (m->send_to_client)
          client.process_done_command(m->n_items);
        else
          server.process_done_command(m->n_items);
        break;
      }
  }

  refiner_pair(set<id> const & client_items,
               set<id> const & server_items) :
    client_cb(*this, true),
    server_cb(*this, false),
    // The item type here really doesn't matter.
    client(file_item, client_voice, client_cb),
    server(file_item, server_voice, server_cb),
    n_msgs(0)
  {
    for (set<id>::const_iterator i = client_items.begin();
         i != client_items.end(); ++i)
      client.note_local_item(*i);

    for (set<id>::const_iterator i = server_items.begin();
         i != server_items.end(); ++i)
      server.note_local_item(*i);

    client.reindex_local_items();
    server.reindex_local_items();
    client.begin_refinement();

    while (! events.empty())
      crank();

    // Refinement should have completed by here.
    UNIT_TEST_CHECK(client.done);
    UNIT_TEST_CHECK(server.done);

    check_set_differences("client", client);
    check_set_differences("server", server);
    check_no_redundant_sends("client->server",
                             client.items_to_send,
                             server.get_local_items());
    check_no_redundant_sends("server->client",
                             server.items_to_send,
                             client.get_local_items());
    UNIT_TEST_CHECK(client.items_to_send.size() == server.items_to_receive);
    UNIT_TEST_CHECK(server.items_to_send.size() == client.items_to_receive);
    L(FL("stats: %d total, %d cs, %d sc, %d msgs")
      % (server.items_to_send.size() + client.get_local_items().size())
      % client.items_to_send.size()
      % server.items_to_send.size()
      % n_msgs);
  }

  void print_if_unequal(char const * context,
                        char const * name1,
                        set<id> const & set1,
                        char const * name2,
                        set<id> const & set2)
  {
    if (set1 != set2)
      {
        L(FL("WARNING: Unequal sets in %s!") % context);
        for (set<id>::const_iterator i = set1.begin(); i != set1.end(); ++i)
          {
            L(FL("%s: %s") % name1 % *i);
          }

        for (set<id>::const_iterator i = set2.begin(); i != set2.end(); ++i)
          {
            L(FL("%s: %s") % name2 % *i);
          }
        L(FL("end of unequal sets"));
      }
  }

  void check_no_redundant_sends(char const * context,
                                set<id> const & src,
                                set<id> const & dst)
  {
    for (set<id>::const_iterator i = src.begin(); i != src.end(); ++i)
      {
        set<id>::const_iterator j = dst.find(*i);
        if (j != dst.end())
          {
            L(FL("WARNING: %s transmission will send redundant item %s")
              % context % *i);
          }
        UNIT_TEST_CHECK(j == dst.end());
      }
  }

  void check_set_differences(char const * context, refiner const & r)
  {
    set<id> tmp;
    set_difference(r.get_local_items().begin(), r.get_local_items().end(),
                   r.get_peer_items().begin(), r.get_peer_items().end(),
                   inserter(tmp, tmp.begin()));
    print_if_unequal(context,
                     "diff(local,peer)", tmp,
                     "items_to_send", r.items_to_send);

    UNIT_TEST_CHECK(tmp == r.items_to_send);
  }
};


void
check_combinations_of_sets(set<id> const & s0,
                           set<id> const & a,
                           set<id> const & b)
{
  // Having composed our two input sets s0 and s1, we now construct the 2
  // auxilary union-combinations of them -- {} and {s0 U s1} -- giving 4
  // basic input sets. We then run 9 "interesting" pairwise combinations
  // of these input sets.

  set<id> e, u, v;
  set_union(s0.begin(), s0.end(), a.begin(), a.end(), inserter(u, u.begin()));
  set_union(s0.begin(), s0.end(), b.begin(), b.end(), inserter(v, v.begin()));

  { refiner_pair x(e, u); }   // a large initial transfer
  { refiner_pair x(u, e); }   // a large initial transfer

  { refiner_pair x(s0, u); }  // a mostly-shared superset/subset
  { refiner_pair x(u, s0); }  // a mostly-shared superset/subset

  { refiner_pair x(a, u); }   // a mostly-unshared superset/subset
  { refiner_pair x(u, a); }   // a mostly-unshared superset/subset

  { refiner_pair x(u, v); }   // things to send in both directions
  { refiner_pair x(v, u); }   // things to send in both directions

  { refiner_pair x(u, u); }   // a large no-op
}


void
build_random_set(set<id> & s, size_t sz, bool clumpy, randomizer & rng)
{
  while (s.size() < sz)
    {
      string str(constants::merkle_hash_length_in_bytes, ' ');
      for (size_t i = 0; i < constants::merkle_hash_length_in_bytes; ++i)
        str[i] = static_cast<char>(rng.uniform(0xff));
      s.insert(id(str, origin::internal));
      if (clumpy && rng.flip())
        {
          size_t clumpsz = rng.uniform(7) + 1;
          size_t pos = rng.flip() ? str.size() - 1 : rng.uniform(str.size());
          for (size_t i = 0; s.size() < sz && i < clumpsz; ++i)
            {
              char c = str[pos];
              if (c == static_cast<char>(0xff))
                break;
              ++c;
              str[pos] = c;
              s.insert(id(str, origin::internal));
            }
        }
    }
}

size_t
perturbed(size_t n, randomizer & rng)
{
  // we sometimes perturb sizes to deviate a bit from natural word-multiple sizes
  if (rng.flip())
    return n + rng.uniform(5);
  return n;
}

size_t
modulated_size(size_t base_set_size, size_t i)
{
  if (i < 3)
    return i+1;
  else
    return static_cast<size_t>((static_cast<double>(i - 2) / 5.0)
                               * static_cast<double>(base_set_size));
}


void
check_with_count(size_t base_set_size, randomizer & rng)
{
  if (base_set_size == 0)
    return;

  L(FL("running refinement check with base set size %d") % base_set_size);

  // Our goal here is to construct a base set of a given size, and two
  // secondary sets which will be combined with the base set in various
  // ways.
  //
  // The secondary sets will be built at the following sizes:
  //
  // 1 element
  // 2 elements
  // 3 elements
  // 0.2 * size of base set
  // 0.4 * size of base set
  // 0.8 * size of base set
  //
  // The base set is constructed in both clumpy and non-clumpy forms,
  // making 6 * 6 * 2 = 72 variations.
  //
  // Since each group of sets creates 9 sync scenarios, each "size" creates
  // 648 sync scenarios.

  for (size_t c = 0; c < 2; ++c)
    {
      set<id> s0;
      build_random_set(s0, perturbed(base_set_size, rng), c == 0, rng);

      for (size_t a = 0; a < 6; ++a)
        {
          set<id> sa;
          build_random_set(sa, modulated_size(perturbed(base_set_size, rng), a), false, rng);

          for (size_t b = 0; b < 6; ++b)
            {
              set<id> sb;
              build_random_set(sb, modulated_size(perturbed(base_set_size, rng), b), false, rng);
              check_combinations_of_sets(s0, sa, sb);
            }
        }
    }
}

// We run 3 primary counts, giving 1944 tests. Note that there is some
// perturbation within the test, so we're not likely to feel side effects
// of landing on such pleasant round numbers.

UNIT_TEST(count_0_1)
{
  {
    // Once with zero-zero, for good measure.
    set<id> s0;
    refiner_pair x(s0, s0);
  }

  randomizer rng;
  check_with_count(1, rng);
}

UNIT_TEST(count_128)
{
  randomizer rng;
  check_with_count(128, rng);
}

UNIT_TEST(count_1024)
{
#ifndef __CYGWIN__
  // Something in this test is very slow on Cygwin; so slow that the
  // buildbot master thinks the slave is hung and terminates it. So we don't
  // run this test on Cygwin.
  randomizer rng;
  check_with_count(1024, rng);
#endif
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
