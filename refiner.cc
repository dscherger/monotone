// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <set>
#include <utility>

#include <boost/shared_ptr.hpp>

#include "refiner.hh"
#include "vocab.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"

using std::inserter;
using std::make_pair;
using std::set;
using std::set_difference;
using std::string;

using boost::dynamic_bitset;

// Our goal is to learn the complete set of items to send. To do this
// we exchange two types of refinement commands: queries and responses.
//
//  - On receiving a 'query' refinement for a node (p,l) you have:
//    - Compare the query node to your node (p,l), noting all the leaves
//      you must send as a result of what you learn in comparison.
//    - For each slot, if you have a subtree where the peer does not
//      (or you both do, and yours differs) send a sub-query for that
//      node, incrementing your query-in-flight counter.
//    - Send a 'response' refinement carrying your node (p,l)
//
//  - On receiving a 'query' refinement for a node (p,l) you don't have:
//    - Send a 'response' refinement carrying an empty synthetic node (p,l)
//
//  - On receiving a 'response' refinement for (p,l)
//    - Compare the query node to your node (p,l), noting all the leaves
//      you must send as a result of what you learn in comparison.
//    - Decrement your query-in-flight counter.
//
// The client kicks the process off by sending a query refinement for the
// root node. When the client's query-in-flight counter drops to zero,
// the client sends a done command, stating how many items it will be
// sending.
//
// When the server receives a done command, it echoes it back stating how
// many items *it* is going to send.
//
// When either side receives a done command, it transitions to
// streaming send mode, sending all the items it's calculated.

void
refiner::note_local_item(id const & item)
{
  local_items.insert(item);
  insert_into_merkle_tree(table, type, item, 0);
}

void
refiner::reindex_local_items()
{
  recalculate_merkle_codes(table, prefix(""), 0);
}

void
refiner::load_merkle_node(size_t level, prefix const & pref,
                          merkle_ptr & node)
{
  merkle_table::const_iterator j = table.find(make_pair(pref, level));
  I(j != table.end());
  node = j->second;
}

bool
refiner::merkle_node_exists(size_t level,
                            prefix const & pref)
{
  merkle_table::const_iterator j = table.find(make_pair(pref, level));
  return (j != table.end());
}

void
refiner::calculate_items_to_send()
{
  if (calculated_items_to_send)
    return;

  items_to_send.clear();
  items_to_receive = 0;

  set_difference(local_items.begin(), local_items.end(),
                 peer_items.begin(), peer_items.end(),
                 inserter(items_to_send, items_to_send.begin()));

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  //   L(FL("%s determined %d %s items to send")
  //     % voicestr() % items_to_send.size() % typestr);
  calculated_items_to_send = true;
}


void
refiner::send_subquery(merkle_node const & our_node, size_t slot)
{
  prefix subprefix;
  our_node.extended_raw_prefix(slot, subprefix);
  merkle_ptr our_subtree;
  load_merkle_node(our_node.level + 1, subprefix, our_subtree);
  // L(FL("%s queueing subquery on level %d\n") % voicestr() % (our_node.level + 1));
  cb.queue_refine_cmd(refinement_query, *our_subtree);
  ++queries_in_flight;
}

void
refiner::send_synthetic_subquery(merkle_node const & our_node, size_t slot)
{
  id val;
  size_t subslot;
  dynamic_bitset<unsigned char> subprefix;

  our_node.get_raw_slot(slot, val);
  pick_slot_and_prefix_for_value(val, our_node.level + 1, subslot, subprefix);

  merkle_node synth_node;
  synth_node.pref = subprefix;
  synth_node.level = our_node.level + 1;
  synth_node.type = our_node.type;
  synth_node.set_raw_slot(subslot, val);
  synth_node.set_slot_state(subslot, our_node.get_slot_state(slot));

  // L(FL("%s queueing synthetic subquery on level %d\n") % voicestr() % (our_node.level + 1));
  cb.queue_refine_cmd(refinement_query, synth_node);
  ++queries_in_flight;
}

void
refiner::note_subtree_shared_with_peer(merkle_node const & our_node, size_t slot)
{
  prefix pref;
  our_node.extended_raw_prefix(slot, pref);
  collect_items_in_subtree(table, pref, our_node.level+1, peer_items);
}

refiner::refiner(netcmd_item_type type, protocol_voice voice, refiner_callbacks & cb)
  : type(type), voice (voice), cb(cb),
    sent_initial_query(false),
    queries_in_flight(0),
    calculated_items_to_send(false),
    done(false),
    items_to_receive(0)
{
  merkle_ptr root = merkle_ptr(new merkle_node());
  root->type = type;
  table.insert(make_pair(make_pair(prefix(""), 0), root));
}

void
refiner::note_item_in_peer(merkle_node const & their_node, size_t slot)
{
  I(slot < constants::merkle_num_slots);
  id slotval;
  their_node.get_raw_slot(slot, slotval);
  peer_items.insert(slotval);

  // Write a debug message
  /*
  {
    id slotval;
    their_node.get_raw_slot(slot, slotval);

    hexenc<prefix> hpref;
    their_node.get_hex_prefix(hpref);

    string typestr;
    netcmd_item_type_to_string(their_node.type, typestr);

    L(FL("%s's peer has %s '%s' at slot %d (in node '%s', level %d)")
      % voicestr() % typestr % slotval
      % slot % hpref % their_node.level);
  }
  */
}


void
refiner::begin_refinement()
{
  merkle_ptr root;
  load_merkle_node(0, prefix(""), root);
  // L(FL("%s queueing initial node\n") % voicestr());
  cb.queue_refine_cmd(refinement_query, *root);
  ++queries_in_flight;
  sent_initial_query = true;
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(FL("Beginning %s refinement on %s.") % typestr % voicestr());
}

void
refiner::process_done_command(size_t n_items)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);

  calculate_items_to_send();
  items_to_receive = n_items;

  L(FL("%s finished %s refinement: %d to send, %d to receive")
    % voicestr() % typestr % items_to_send.size() % items_to_receive);

  /*
  if (local_items.size() < 25)
    {
      // Debugging aid.
      L(FL("+++ %d items in %s") % local_items.size() % voicestr());
      for (set<id>::const_iterator i = local_items.begin();
           i != local_items.end(); ++i)
        {
          L(FL("%s item %s") % voicestr() % *i);
        }
      L(FL("--- items in %s") % voicestr());
    }
  */

  if (voice == server_voice)
    {
      //       L(FL("server responding to [done %s %d] with [done %s %d]")
      //         % typestr % n_items % typestr % items_to_send.size());
      cb.queue_done_cmd(type, items_to_send.size());
    }

  done = true;

  // we can clear up the merkle trie's memory now
  table.clear();
}


void
refiner::process_refinement_command(refinement_type ty,
                                    merkle_node const & their_node)
{
  prefix pref;
  hexenc<prefix> hpref;
  their_node.get_raw_prefix(pref);
  their_node.get_hex_prefix(hpref);
  string typestr;

  netcmd_item_type_to_string(their_node.type, typestr);
  size_t lev = static_cast<size_t>(their_node.level);

  //   L(FL("%s received refinement %s netcmd on %s node '%s', level %d") %
  //   voicestr() % (ty == refinement_query ? "query" : "response") %
  //   typestr % hpref % lev);

  merkle_ptr our_node;

  if (merkle_node_exists(their_node.level, pref))
    load_merkle_node(their_node.level, pref, our_node);
  else
    {
      // Synthesize empty node if we don't have one.
      our_node = merkle_ptr(new merkle_node);
      our_node->pref = their_node.pref;
      our_node->level = their_node.level;
      our_node->type = their_node.type;
    }

  for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
    {
      // Note any leaves they have.
      if (their_node.get_slot_state(slot) == leaf_state)
        note_item_in_peer(their_node, slot);

      if (ty == refinement_query)
        {
          // This block handles the interesting asymmetric cases of subtree
          // vs. leaf.
          //
          // Note that in general we're not allowed to send a new query
          // packet when we're looking at a response. This wrinkle is both
          // why this block appears to do slightly more work than necessary,
          // and why it's predicated on "ty == refinement_query". More detail
          // in the cases below.

          if (their_node.get_slot_state(slot) == leaf_state
              && our_node->get_slot_state(slot) == subtree_state)
            {
              // If they have a leaf and we have a subtree, we need to look
              // in our subtree to find if their leaf is present, and send
              // them a "query" that will inform them, in passing, of the
              // presence of our node.

              id their_slotval;
              their_node.get_raw_slot(slot, their_slotval);
              size_t snum;
              merkle_ptr mp;
              if (locate_item(table, their_slotval, snum, mp))
                {
                  cb.queue_refine_cmd(refinement_query, *mp);
                  ++queries_in_flight;
                }

            }

          else if (their_node.get_slot_state(slot) == subtree_state
                   && our_node->get_slot_state(slot) == leaf_state)
            {
              // If they have a subtree and we have a leaf, we need to
              // arrange for a subquery to explore the subtree looking for
              // the leaf in *their* subtree. The tricky part is that we
              // cannot have this subquery triggered by our response
              // packet. We need to initiate a new (redundant) query here to
              // prompt our peer to explore the subtree.
              //
              // This is purely for the sake of balancing the bracketing of
              // queries and responses: if they were to reply to our
              // response packet, our query-in-flight counter would have
              // temporarily dropped to zero and we'd have initiated
              // streaming send mode.
              //
              // Yes, the need to invert the sense of queries in this case
              // represents a misdesign in this generation of the netsync
              // protocol. It still contains much less hair than it used to,
              // so I'm willing to accept it.

              send_synthetic_subquery(*our_node, slot);
            }

          // Finally: if they had an empty slot in either case, there's no
          // subtree exploration to perform; the response packet will inform
          // the peer of everything relevant know about this node: namely
          // that they're going to receive a complete subtree, we know
          // what's in it, and we'll tell them how many nodes to expect in
          // the aggregate count of the 'done' commane.

        }

      // Compare any subtrees, if we both have subtrees.
      if (their_node.get_slot_state(slot) == subtree_state
          && our_node->get_slot_state(slot) == subtree_state)
        {
          id our_slotval, their_slotval;
          their_node.get_raw_slot(slot, their_slotval);
          our_node->get_raw_slot(slot, our_slotval);

          // Always note when you share a subtree.
          if (their_slotval == our_slotval)
            note_subtree_shared_with_peer(*our_node, slot);

          // Send subqueries when you have a different subtree
          // and you're answering a query message.
          else if (ty == refinement_query)
            send_subquery(*our_node, slot);
        }
    }

  if (ty == refinement_response)
    {
      E((queries_in_flight > 0), origin::network,
        F("underflow on query-in-flight counter"));
      --queries_in_flight;

      // Possibly this signals the end of refinement.
      if (voice == client_voice && queries_in_flight == 0)
        {
          string typestr;
          netcmd_item_type_to_string(their_node.type, typestr);
          calculate_items_to_send();
          // L(FL("client sending [done %s %d]") % typestr % items_to_send.size());
          cb.queue_done_cmd(type, items_to_send.size());
        }
    }
  else
    {
      // Always reply to every query with the current node.
      I(ty == refinement_query);
      // L(FL("%s queueing response to query on %d\n") % voicestr() % our_node->level);
      cb.queue_refine_cmd(refinement_response, *our_node);
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
