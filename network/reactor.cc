// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "network/reactor.hh"

#include "constants.hh"
#include "database.hh"
#include "network/reactable.hh"

using std::make_pair;
using std::map;
using std::set;
using std::vector;

using boost::shared_ptr;

void reactor::ready_for_io(shared_ptr<reactable> item,
                           transaction_guard & guard)
{
  try
    {
      if (item->do_work(guard))
        {
          if (item->arm())
            {
              ++have_armed;
            }
          item->add_to_probe(probe);
          vector<Netxx::socket_type> ss = item->get_sockets();
          for (vector<Netxx::socket_type>::iterator i = ss.begin();
               i != ss.end(); ++i)
            {
              lookup.insert(make_pair(*i, item));
            }
          if (item->can_timeout())
            can_have_timeout = true;
        }
      else
        {
          remove(item);
        }
    }
  catch (bad_decode & bd)
    {
      W(F("protocol error while processing peer %s: '%s'")
        % item->name() % bd.what);
      remove(item);
    }
  catch (recoverable_failure & rf)
    {
      W(F("recoverable '%s' error while processing peer %s: '%s'")
        % origin::type_to_string(rf.caused_by())
        % item->name() % rf.what());
      remove(item);
    }
}
reactor::reactor()
  : have_pipe(false),
    timeout(static_cast<long>(constants::netsync_timeout_seconds)),
    instant(0,1),
    readying(false),
    have_armed(0)
{ }
void reactor::add(shared_ptr<reactable> item, transaction_guard & guard)
{
  I(!have_pipe);
  if (item->is_pipe_pair())
    {
      I(items.size() == 0);
      have_pipe = true;
    }
  items.insert(item);
  if (readying)
    ready_for_io(item, guard);
}
void reactor::remove(shared_ptr<reactable> item)
{
  set<shared_ptr<reactable> >::iterator i = items.find(item);
  if (i != items.end())
    {
      items.erase(i);
      if (readying && !have_pipe)
        item->remove_from_probe(probe);
      have_pipe = false;
    }
}

int reactor::size() const
{
  return items.size();
}

void reactor::ready(transaction_guard & guard)
{
  readying = true;
  have_armed = 0;
  can_have_timeout = false;

  probe.clear();
  lookup.clear();
  set<shared_ptr<reactable> > todo = items;
  for (set<shared_ptr<reactable> >::iterator i = todo.begin();
       i != todo.end(); ++i)
    {
      ready_for_io(*i, guard);
    }
}
bool reactor::do_io()
{
  // so it doesn't get reset under us if we drop the session
  bool pipe = have_pipe;
  readying = false;
  bool timed_out = true;
  Netxx::Timeout how_long;
  if (!can_have_timeout)
    how_long = forever;
  else if (have_armed > 0)
    {
      how_long = instant;
      timed_out = false;
    }
  else
    how_long = timeout;

  L(FL("i/o probe with %d armed") % have_armed);
  Netxx::socket_type fd;
  do
    {
      Netxx::Probe::result_type res = probe.ready(how_long);
      how_long = instant;
      fd = res.first;
      Netxx::Probe::ready_type event = res.second;

      if (fd == -1)
        break;

      timed_out = false;

      map<Netxx::socket_type, shared_ptr<reactable> >::iterator r
        = lookup.find(fd);
      if (r != lookup.end())
        {
          if (items.find(r->second) != items.end())
            {
              if (!r->second->do_io(event))
                {
                  remove(r->second);
                }
            }
          else
            {
              L(FL("Got i/o on dead peer %s") % r->second->name());
            }
          if (!pipe)
            r->second->remove_from_probe(probe);
        }
      else
        {
          L(FL("got woken up for action on unknown fd %d") % fd);
        }
    }
  while (fd != -1 && !pipe);
  return !timed_out;
}
void reactor::prune()
{
  time_t now = ::time(NULL);
  set<shared_ptr<reactable> > todo = items;
  for (set<shared_ptr<reactable> >::iterator i = todo.begin();
       i != todo.end(); ++i)
    {
      if ((*i)->timed_out(now))
        {
          P(F("peer %s has been idle too long, disconnecting")
            % (*i)->name());
          remove(*i);
        }
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
