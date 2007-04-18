// copyright (C) 2005-2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
#include "mtncvs_state.hh"

using cvs_sync::cvs_edge;
using cvs_sync::cvs_repository;

size_t const cvs_edge::cvs_window;

// whether time is below span or (within span and lesser author,changelog)
bool cvs_sync::operator<(const file_state &s,const cvs_edge &e)
{ return s.since_when<e.time ||
    (s.since_when<=e.time2 && (s.author<e.author ||
    (s.author==e.author && s.log_msg<e.changelog)));
}

// whether time is below span or (within span and lesser/equal author,changelog)
bool 
cvs_sync::operator<=(const file_state &s,const cvs_edge &e)
{ return s.since_when<e.time ||
    (s.since_when<=e.time2 && (s.author<=e.author ||
    (s.author==e.author && s.log_msg<=e.changelog)));
}


bool cvs_edge::similar_enough(cvs_edge const & other) const
{
  if (changelog != other.changelog)
    return false;
  if (author != other.author)
    return false;
  if (labs(time - other.time) > long(cvs_window)
      && labs(time2 - other.time) > long(cvs_window))
    return false;
  return true;
}

bool cvs_edge::operator<(cvs_edge const & other) const
{
  return time < other.time ||

    (time == other.time 
     && author < other.author) ||

    (time == other.time 
     && author == other.author 
     && changelog < other.changelog);
}

void cvs_repository::check_split(const cvs_file_state &s, const cvs_file_state &end, 
          const std::set<cvs_edge>::iterator &e)
{ cvs_file_state s2=s;
  ++s2;
  if (s2==end) return;
  MM(boost::lexical_cast<std::string>(s->since_when));
  MM(boost::lexical_cast<std::string>(s2->since_when));
  I(s->since_when!=s2->since_when);
  // checkins must not overlap (next revision must lie beyond edge)
  if ((*s2) <= (*e))
  { W(F("splitting edge %s-%s at %s\n") % time_t2human(e->time) 
        % time_t2human(e->time2) % time_t2human(s2->since_when));
    cvs_edge new_edge=*e;
    MM(boost::lexical_cast<std::string>(e->time));
    I(s2->since_when-1>=e->time);
    e->time2=s2->since_when-1;
    new_edge.time=s2->since_when;
    edges.insert(new_edge);
  }
}

void cvs_repository::join_edge_parts(std::set<cvs_edge>::iterator i)
{ for (;i!=edges.end();)
  { std::set<cvs_edge>::iterator j=i;
    j++; // next one
    if (j==edges.end()) break;
    
    MM(boost::lexical_cast<std::string>(j->time2));
    MM(boost::lexical_cast<std::string>(j->time));
    MM(boost::lexical_cast<std::string>(i->time2));
    MM(boost::lexical_cast<std::string>(i->time));
    I(j->time2==j->time); // make sure we only do this once
    I(i->time2<=j->time); // should be sorted ...
    if (!i->similar_enough(*j)) 
    { ++i; continue; }
    I((j->time-i->time2)<=time_t(cvs_edge::cvs_window)); // just to be sure
    I(i->author==j->author);
    I(i->changelog==j->changelog);
    I(i->time2<j->time); // should be non overlapping ...
    L(FL("joining %s-%s+%s\n") % time_t2human(i->time) % time_t2human(i->time2) % time_t2human(j->time));
    i->time2=j->time;
    edges.erase(j);
  }
}

cvs_edge::cvs_edge(const revision_id &rid, mtncvs_state &app)
 : changelog_valid(), time(), time2()
{ revision=rid;
  // get author + date 
  std::vector<mtn_automate::certificate> certs=app.get_revision_certs(rid);
  
  for (std::vector<mtn_automate::certificate>::const_iterator c=certs.begin();
            c!=certs.end();++c)
  { if (!c->trusted || c->signature!=mtn_automate::certificate::ok) continue;
    if (c->name=="date")
    { L(FL("date cert %s\n")% c->value);
      time=time2=cvs_repository::posix2time_t(c->value);
    }
    else if (c->name=="author")
    { author=c->value;
    }
    else if (c->name=="changelog")
    { changelog=c->value;
      changelog_valid=true;
    }
  }
}

